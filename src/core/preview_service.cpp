// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview_service.h"
#include "apng_codec.h"
#include "plugin_store.h"
#include "localization.h"
#include "image_memory.h"
#include <QCache>
#include <QColorSpace>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QLibrary>
#include <QMutex>
#include <QPointer>
#include <QSet>
#include <QtConcurrent>
#include <algorithm>
namespace ix {
namespace {
QMutex cacheMutex;
// Keep the path separate from the opaque revision. Filenames may contain '|',
// and eviction must not touch object() and reorder unrelated LRU entries.
using CacheKey = QPair<QString, QString>;
QCache<CacheKey, QImage> previews(16 * 1024 * 1024);
QCache<CacheKey, ImageDocument> documents(192 * 1024 * 1024);
quint64 cacheEpoch = 0; // Protected by cacheMutex, like both caches.
std::atomic_bool rawPreviewsEnabled = true;
constexpr qint64 CacheCostLimit = ImageMemoryLimit + 1; // Reject beyond the largest 512 MiB budget.
qint64 addCost(qint64 first, qint64 second) {
    return std::min(CacheCostLimit, first + second);
}
qint64 stringCost(const QString &text) {
    return std::min<qint64>(std::max(text.size(), text.capacity()), ImageMemoryLimit / 2) * 2;
}
} // namespace
bool isRawImage(const QString &path) {
    return isRawFormat(QFileInfo(path).suffix().toLatin1());
}
QImage loadPreview(const QString &path, QSize bounds, const std::atomic_bool *cancel) {
    if (cancel && cancel->load())
        return {};
    if (isRawImage(path) && !rawPreviewsEnabled.load())
        return {};
    if (!PluginStore::formatEnabled(QFileInfo(path).suffix().toLatin1().toLower()))
        return {};
    if (!bounds.isValid() || bounds.width() > 2048 || bounds.height() > 2048 || (cancel && cancel->load()))
        return {};
    const auto revision = imageFileKey(path);
    const auto absolute = QFileInfo(path).absoluteFilePath();
    const CacheKey key{absolute, revision + QStringLiteral("/%1x%2").arg(bounds.width()).arg(bounds.height())};
    QImage image;
    QByteArray cachedFormat;
    quint64 epoch = 0;
    bool cacheEnabled = false;
    {
        QMutexLocker lock(&cacheMutex);
        epoch = cacheEpoch;
        cacheEnabled = previews.maxCost() > 0;
        if (auto *image = previews.object(key))
            return *image;
        if (auto *document = documents.object({absolute, revision + QStringLiteral("/0")});
            document && document->sourceRevision == revision && !document->modified &&
            !document->animated && document->page == 0 && document->pageCount == 1) {
            const auto format = document->format.toLower();
            // These decoders scale after reading all pixels. Borrow the unchanged
            // original via implicit sharing; never scale while holding the lock.
            // JPEG's reduced decoder and RAW/animated pages keep their own paths.
            if (format == "png" || format == "bmp") {
                image = document->pixels;
                cachedFormat = format;
            }
        }
    }
    if (!cachedFormat.isEmpty() && !PluginStore::formatEnabled(cachedFormat))
        return {};
#ifdef IX_RAW_PREVIEW_MODULE
    if (isRawImage(path)) {
        static QLibrary module(optionalModulePath(QStringLiteral(IX_RAW_PREVIEW_MODULE)));
        using Read = bool (*)(const QString &, QSize, QImage &);
        static auto read = reinterpret_cast<Read>(module.resolve("ixReadRawPreviewSized"));
        if (read)
            read(path, bounds, image);
        // Do not fully develop RAW files just to populate an optional thumbnail.
        if (image.isNull())
            return {};
    }
#else
    if (isRawImage(path))
        return {};
#endif
    if (image.isNull()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (!PluginStore::formatEnabled(reader.format()))
            return {};
        const auto size = reader.size();
        if (size.isValid() && !safeSize(size))
            return {};
        if (size.isValid()) {
            // Decoder scaling precedes EXIF rotation. A non-square thumbnail
            // needs transposed bounds for 90/270 degrees, otherwise it can be
            // decoded too small and enlarged again after rotation.
            auto decodeBounds = bounds;
            if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90))
                decodeBounds.transpose();
            reader.setScaledSize(size.scaled(decodeBounds, Qt::KeepAspectRatio));
        }
        if (cancel && cancel->load())
            return {};
        image = reader.read();
    }
    if (image.isNull() || (cancel && cancel->load()))
        return {};
    image = image.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // Scaling can allocate or outlive cancellation/a source rewrite too.
    if (image.isNull() || (cancel && cancel->load()) || revision != imageFileKey(path))
        return {};
    if (cacheEnabled) {
        const auto cost = addCost(imageMemoryCost(image), addCost(stringCost(key.second), addCost(stringCost(absolute), 64)));
        QMutexLocker lock(&cacheMutex);
        if (epoch == cacheEpoch)
            previews.insert(key, new QImage(image), int(cost));
    }
    return image;
}
PreviewService::PreviewService(QObject *parent)
    : QObject(parent), cancelled_(std::make_shared<std::atomic_bool>(false)) {
    pool_.setMaxThreadCount(2);
    pool_.setExpiryTimeout(10000);
    pool_.setThreadPriority(QThread::LowPriority);
}
PreviewService::~PreviewService() {
    cancel();
    pool_.waitForDone();
}
void PreviewService::cancel() {
    cancelled_->store(true);
    pending_.clear();
    cancelled_ = std::make_shared<std::atomic_bool>(false);
}
void PreviewService::request(const QString &path, QSize bounds, int generation) {
    pending_.enqueue({path, bounds, generation});
    startNext();
}
void PreviewService::startNext() {
    // Only active decoders need futures/watchers. Cancelled queued work never
    // enters the pool or consumes GUI completion events during fast navigation.
    while (running_ < pool_.maxThreadCount() && !pending_.isEmpty()) {
        const auto request = pending_.dequeue();
        const auto path = request.path;
        const auto bounds = request.bounds;
        const auto generation = request.generation;
        ++running_;
        auto *watcher = new QFutureWatcher<QImage>(this);
        auto token = cancelled_;
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, path, generation, token] {
            auto result = watcher->result();
            watcher->deleteLater();
            --running_;
            const QPointer<PreviewService> alive(this);
            if (!token->load())
                emit ready(path, result, generation);
            if (alive)
                startNext();
        });
        watcher->setFuture(QtConcurrent::run(&pool_, [path, bounds, token] {
            try {
                return loadPreview(path, bounds, token.get());
            } catch (...) {
                return QImage();
            }
        }));
    }
}
ImageResult ImageLoadCache::read(const CodecRegistry &registry, const QString &path, int page,
                                 const std::function<void()> &beforeDecode) {
    const auto revision = imageFileKey(path);
    const auto absolute = QFileInfo(path).absoluteFilePath();
    const CacheKey key{absolute, revision + QLatin1Char('/') + QString::number(page)};
    quint64 epoch = 0;
    bool cacheEnabled = false;
    {
        QMutexLocker lock(&cacheMutex);
        epoch = cacheEpoch;
        cacheEnabled = documents.maxCost() > 0;
        if (auto *d = documents.object(key))
            return {*d, {}};
    }
    if (beforeDecode)
        beforeDecode();
    auto result = registry.read(path, page);
    if (result.ok() && imageFileKey(path) != revision)
        return {{}, IX_TEXT("Die Datei wurde während des Lesens verändert. Bitte erneut öffnen.")};
    if (result.ok())
        result.document.sourceRevision = revision;
    if (result.ok() && cacheEnabled && result.document.pixels.sizeInBytes() <= 96 * 1024 * 1024) {
        const auto cost = addCost(documentMemoryCost(result.document), addCost(stringCost(key.second), addCost(stringCost(absolute), 64)));
        QMutexLocker lock(&cacheMutex);
        if (epoch == cacheEpoch)
            documents.insert(key, new ImageDocument(result.document), int(cost));
    }
    return result;
}
void ImageLoadCache::configure(int imageMiB, int previewMiB, bool rawPreviews) {
    QMutexLocker lock(&cacheMutex);
    documents.setMaxCost(std::clamp(imageMiB, 0, 512) * 1024 * 1024);
    previews.setMaxCost(std::clamp(previewMiB, 0, 64) * 1024 * 1024);
    rawPreviewsEnabled.store(rawPreviews);
}
void ImageLoadCache::clear() {
    ApngCodec::clearSession();
    QMutexLocker lock(&cacheMutex);
    ++cacheEpoch;
    documents.clear();
    previews.clear();
}
void ImageLoadCache::invalidate(const QStringList &paths) {
    QStringList absolute;
    for (const auto &path : paths)
        if (!path.isEmpty()) absolute.append(QFileInfo(path).absoluteFilePath());
    if (absolute.isEmpty()) return;
    const auto lookupPath = [](const QString &path) {
#ifdef Q_OS_WIN
        return path.toCaseFolded();
#else
        return path;
#endif
    };
    QSet<QString> affected;
    affected.reserve(absolute.size());
    for (const auto &path : absolute) affected.insert(lookupPath(path));
    ApngCodec::invalidateSession(absolute);
    QMutexLocker lock(&cacheMutex);
    // One bounded epoch avoids an unbounded per-path generation map. Retained
    // unrelated entries stay usable, but earlier misses must not insert later.
    ++cacheEpoch;
    for (const auto &key : documents.keys())
        if (affected.contains(lookupPath(key.first))) documents.remove(key);
    for (const auto &key : previews.keys())
        if (affected.contains(lookupPath(key.first))) previews.remove(key);
}
} // namespace ix
