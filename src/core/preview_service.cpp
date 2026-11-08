// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview_service.h"
#include "apng_codec.h"
#include "plugin_store.h"
#include "localization.h"
#include "image_memory.h"
#include "image_storage.h"
#include <QCache>
#include <QHash>
#include <QColorSpace>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QLibrary>
#include <QMutex>
#include <QPointer>
#include <QSet>
#include <QScopeGuard>
#include <QWaitCondition>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <memory>
#include <utility>
#include <thread>
namespace ix {
namespace {
QMutex cacheMutex;
// Keep the path separate from the opaque revision. Filenames may contain '|',
// and eviction must not touch object() and reorder unrelated LRU entries.
using CacheKey = QPair<QString, QString>;
QCache<CacheKey, QImage> previews(16 * 1024 * 1024);
QCache<CacheKey, ImageDocument> documents(192 * 1024 * 1024);
struct Presentation { QImage pixels; qint64 original; bool manageColor, sharpen; };
QCache<CacheKey, Presentation> presentations(0);
struct DecodedPreview {
    ImageLoadCache::PreviewPixels image;
    QString revision;
    QByteArray format;
    qint64 cost;
    int priority;
    quint64 order;
};
QHash<QString, DecodedPreview> decodedPreviews;
qint64 decodedCost = 0;
quint64 decodedOrder = 0;
std::atomic_bool haveDecodedPreviews = false;
auto leastUsefulDecoded() {
    auto worst = decodedPreviews.end();
    for (auto it = decodedPreviews.begin(); it != decodedPreviews.end(); ++it)
        if (worst == decodedPreviews.end() || it->priority > worst->priority ||
            (it->priority == worst->priority && it->order < worst->order)) worst = it;
    return worst;
}
void removeDecoded(QHash<QString, DecodedPreview>::iterator it) {
    decodedCost -= it->cost;
    decodedPreviews.erase(it);
}
void trimDecoded(qint64 limit) {
    while (decodedCost > std::max<qint64>(0, limit)) removeDecoded(leastUsefulDecoded());
}
int documentBudget = 192 * 1024 * 1024, previewBudget = 16 * 1024 * 1024;
bool cacheConfigured = false;
std::atomic<qint64> retainedCacheBytes = 0;
qint64 cacheBudget() { return std::min<qint64>(qint64(documentBudget) + previewBudget, imageMemoryLimit()); }
void publishUsage() {
    retainedCacheBytes.store(qint64(documents.totalCost()) + presentations.totalCost() + decodedCost + previews.totalCost());
    haveDecodedPreviews.store(!decodedPreviews.isEmpty());
}
int presentationCapacity(qint64 originalBytes) {
    return int(std::max<qint64>(0, std::min(qint64(documentBudget) - originalBytes - decodedCost,
        cacheBudget() - originalBytes - decodedCost - previews.totalCost())));
}
qint64 decodedCapacity(qint64 originalBytes) {
    return std::max<qint64>(0, std::min(qint64(documentBudget) - originalBytes - presentations.totalCost(),
        cacheBudget() - originalBytes - presentations.totalCost() - previews.totalCost()));
}
// Foreground originals take priority over recomputable presentations/previews.
// QCache retains its own LRU order. Only bounded speculative-entry bookkeeping
// runs under the lock; decoding, scaling and metadata work stay outside it.
bool makeRoomForOriginal(qint64 cost) {
    if (cost > std::min<qint64>(documentBudget, cacheBudget())) return false;
    const auto wanted = std::min<qint64>(documentBudget, qint64(documents.totalCost()) + cost);
    trimDecoded(decodedCapacity(wanted));
    previews.setMaxCost(int(std::min<qint64>(previewBudget, std::max<qint64>(0, cacheBudget() - wanted - decodedCost))));
    presentations.setMaxCost(presentationCapacity(wanted));
    documents.setMaxCost(int(std::min<qint64>(documentBudget, cacheBudget() - previews.totalCost() - decodedCost)));
    return true;
}
quint64 cacheEpoch = 0; // All cache ownership is protected by cacheMutex.
// Bumped only when retained content is dropped (clear/invalidate), never by a
// budget reconfiguration: joining a decode in flight is about which pixels are
// being computed right now, not about which of them the cache owns.
quint64 contentGeneration = 0;
struct ActiveDecode {
    QWaitCondition ready;
    ImageLoadCache::PreviewPixels result;
    std::thread::id owner = std::this_thread::get_id();
    QThread *thread = QThread::currentThread();
    QThread::Priority priority = thread->priority();
    bool boosted = false;
    quint64 epoch;
    bool finished = false;
};
QHash<CacheKey, std::shared_ptr<ActiveDecode>> activeDecodes;
// A speculative decode (startup prewarm, neighbour prefetch) can race the open
// it was meant to accelerate. Readers join the running decode of the same
// revision instead of reading and decoding the same bytes a second time. Joining
// is refused after dropped content (contentGeneration) so a cleared cache is
// never repopulated from an older in-flight decode.
struct ActiveRead {
    QWaitCondition ready;
    std::shared_ptr<ImageDocument> document; // null when the owner failed
    std::thread::id owner = std::this_thread::get_id();
    quint64 generation = 0;
    bool finished = false;
};
QHash<CacheKey, std::shared_ptr<ActiveRead>> activeReads;
ImageLoadCache::DecodeActivity decodeCounters;
std::atomic_bool rawPreviewsEnabled = true;
constexpr qint64 CacheCostLimit = ImageCostLimit;
qint64 addCost(qint64 first, qint64 second) {
    return std::min(CacheCostLimit, first + second);
}
qint64 stringCost(const QString &text) {
    return std::min<qint64>(std::max(text.size(), text.capacity()), ImageCostLimit / 2) * 2;
}
} // namespace
bool isRawImage(const QString &path) {
    return isRawFormat(QFileInfo(path).suffix().toLatin1());
}
QImage loadPreview(const QString &path, QSize bounds, const std::atomic_bool *cancel) {
    return loadPreview(path, bounds, cancel, 0);
}
QImage loadPreview(const QString &path, QSize bounds, const std::atomic_bool *cancel, int priority) {
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
    ImageLoadCache::PreviewPixels decoded;
    QByteArray decodedFormat;
    QByteArray cachedFormat;
    quint64 epoch = 0;
    bool cacheEnabled = false;
    bool allowDecoded = false;
    {
        QMutexLocker lock(&cacheMutex);
        epoch = cacheEpoch;
        cacheEnabled = previewBudget > 0;
        allowDecoded = previewBudget > 0 && documentBudget > 0;
        if (auto *image = previews.object(key)) {
            auto decoded = decodedPreviews.find(absolute);
            if (decoded != decodedPreviews.end() && decoded->revision == revision) {
                decoded->priority = priority; decoded->order = ++decodedOrder;
            }
            return *image;
        }
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
#ifdef IX_PDF_MODULE
    if (QFileInfo(path).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
        static QLibrary module(optionalModulePath(QStringLiteral(IX_PDF_MODULE)));
        using Read = QImage (*)(const QString &, QSize, qint64);
        static const auto read = reinterpret_cast<Read>(module.resolve("ixReadPdfPreview"));
        if (read) image = read(path, bounds, imageMemoryLimit());
        if (image.isNull()) return {};
    }
#endif
    if (image.isNull()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (!PluginStore::formatEnabled(reader.format()))
            return {};
        const auto size = reader.size();
        const auto format = reader.format();
        const auto transform = reader.transformation();
        const auto bytes = (format == "png" || format == "bmp") ? imageBufferBytes(size, reader.imageFormat()) : 0;
        const bool share = bytes >= MiB && bytes <= 96 * MiB &&
            !reader.supportsOption(QImageIOHandler::ScaledSize) && transform == QImageIOHandler::TransformationNone &&
            reader.imageCount() == 1 && (format != "png" || !ApngCodec().accepts(path));
        if (size.isValid() && !safeSize(size) && reader.format() != "jpeg" && reader.format() != "jpg")
            return {};
        if (size.isValid() && !share) {
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
        if (share) {
            auto full = ImageLoadCache::decodePreviewPixels(path, size, reader.imageFormat(), format, [&] {
                auto pixels = reader.read();
                return ImageLoadCache::PreviewPixels{pixels, std::max(20, reader.nextImageDelay()), reader.loopCount()};
            }, cancel);
            image = full.pixels;
            if (allowDecoded && image.sizeInBytes() >= MiB && image.sizeInBytes() <= 96 * MiB) {
                decoded = std::move(full); decodedFormat = format;
            }
        } else image = reader.read();
    }
    if (image.isNull() || (cancel && cancel->load()))
        return {};
    image = image.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // Scaling can allocate or outlive cancellation/a source rewrite too.
    if (image.isNull() || (cancel && cancel->load()) || revision != imageFileKey(path))
        return {};
    if (cacheEnabled || !decoded.pixels.isNull()) {
        const auto cost = addCost(imageMemoryCost(image), addCost(stringCost(key.second), addCost(stringCost(absolute), 64)));
        QMutexLocker lock(&cacheMutex);
        if (epoch == cacheEpoch) {
            previews.setMaxCost(int(std::min<qint64>(previewBudget, std::max<qint64>(0,
                cacheBudget() - documents.totalCost() - presentations.totalCost() - decodedCost))));
            if (cacheEnabled && cost <= previews.maxCost()) previews.insert(key, new QImage(image), int(cost));
            if (!decoded.pixels.isNull() && !documents.contains({absolute, revision + QStringLiteral("/0")})) {
                const auto pixelsCost = addCost(imageMemoryCost(decoded.pixels), addCost(stringCost(absolute), addCost(stringCost(revision), 192)));
                auto old = decodedPreviews.find(absolute);
                if (old != decodedPreviews.end() && old->revision != revision) { removeDecoded(old); old = decodedPreviews.end(); }
                if (old != decodedPreviews.end()) { old->priority = priority; old->order = ++decodedOrder; }
                else {
                    const auto capacity = decodedCapacity(documents.totalCost());
                    qint64 protectedCost = 0;
                    for (const auto &entry : std::as_const(decodedPreviews)) if (entry.priority < priority) protectedCost += entry.cost;
                    if (pixelsCost <= capacity - protectedCost) {
                        while (decodedCost + pixelsCost > capacity) removeDecoded(leastUsefulDecoded());
                        decodedPreviews.insert(absolute, {decoded, revision, decodedFormat, pixelsCost, priority, ++decodedOrder});
                        decodedCost += pixelsCost;
                    }
                }
            }
            publishUsage();
        }
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
    request(path, bounds, generation, 0);
}
void PreviewService::request(const QString &path, QSize bounds, int generation, int priority) {
    pending_.enqueue({path, bounds, generation, priority});
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
            auto result = watcher->future().takeResult();
            watcher->deleteLater();
            --running_;
            const QPointer<PreviewService> alive(this);
            if (!token->load())
                emit ready(path, result, generation);
            if (alive)
                startNext();
        });
        watcher->setFuture(QtConcurrent::run(&pool_, [path, bounds, token, priority = request.priority] {
            try {
                return loadPreview(path, bounds, token.get(), priority);
            } catch (...) {
                return QImage();
            }
        }));
    }
}
ImageLoadCache::PreviewPixels ImageLoadCache::decodePreviewPixels(const QString &path, QSize size,
    QImage::Format format, const QByteArray &codec, const std::function<PreviewPixels()> &decode,
    const std::atomic_bool *cancel) {
    if (cancel && cancel->load()) return {};
    const auto bytes = imageBufferBytes(size, format);
    if ((codec != "png" && codec != "bmp") || bytes < MiB || bytes > 96 * MiB) return decode();
    const auto revision = imageFileKey(path);
    const CacheKey key{QFileInfo(path).absoluteFilePath(), revision +
        QStringLiteral("/%1/%2x%3/%4").arg(QString::fromLatin1(codec)).arg(size.width()).arg(size.height()).arg(int(format))};
    std::shared_ptr<ActiveDecode> active;
    {
        QMutexLocker lock(&cacheMutex);
        active = activeDecodes.value(key);
        if (active && active->epoch == cacheEpoch && active->owner != std::this_thread::get_id()) {
            // Foreground work must not inherit a thumbnail worker's low Qt
            // priority. Restore the pool worker's priority when decoding ends.
            if (!cancel && active->priority < QThread::NormalPriority && !active->boosted) {
                active->thread->setPriority(QThread::NormalPriority); active->boosted = true;
            }
            ++decodeCounters.waiting;
            while (!active->finished && !(cancel && cancel->load())) active->ready.wait(&cacheMutex, 20);
            --decodeCounters.waiting;
            if (cancel && cancel->load()) return {};
            auto result = active->result;
            lock.unlock();
            // A failed owner must not replace this reader's diagnostics or
            // permanently cache failure. Retry using the caller's own device.
            return result.pixels.isNull() ? decode() : result;
        }
        if (active && active->owner == std::this_thread::get_id()) {
            lock.unlock(); return decode(); // Reentrant decoding must never wait on itself.
        }
        active = std::make_shared<ActiveDecode>(); active->epoch = cacheEpoch;
        activeDecodes.insert(key, active); ++decodeCounters.active;
    }
    const auto finish = qScopeGuard([&] {
        QMutexLocker lock(&cacheMutex);
        active->finished = true;
        if (active->boosted) active->thread->setPriority(active->priority);
        --decodeCounters.active;
        if (activeDecodes.value(key) == active) activeDecodes.remove(key);
        active->ready.wakeAll();
    });
    auto result = decode(); // No locks across decoding, source I/O or pixel work.
    if (!result.pixels.isNull() && result.pixels.size() == size && result.pixels.format() == format &&
        result.pixels.sizeInBytes() >= MiB && result.pixels.sizeInBytes() <= 96 * MiB && imageFileKey(path) == revision) {
        QMutexLocker lock(&cacheMutex);
        active->result = result;
    }
    return result;
}
ImageLoadCache::DecodeActivity ImageLoadCache::decodeActivity() {
    QMutexLocker lock(&cacheMutex);
    return decodeCounters;
}
ImageResult ImageLoadCache::read(const CodecRegistry &registry, const QString &path, int page,
                                 const std::function<void()> &beforeDecode) {
    auto result = read(registry, path, page, beforeDecode, nullptr);
    return std::move(*result);
}
std::optional<ImageResult> ImageLoadCache::read(const CodecRegistry &registry, const QString &path, int page,
                                 const std::function<void()> &beforeDecode, const std::atomic_bool *cancel) {
    if (cancel && cancel->load()) return {};
    const auto revision = imageFileKey(path);
    const auto absolute = QFileInfo(path).absoluteFilePath();
    const CacheKey key{absolute, revision + QLatin1Char('/') + QString::number(page)};
    quint64 epoch = 0;
    bool cacheEnabled = false;
    std::shared_ptr<ActiveRead> active;
    std::shared_ptr<ActiveRead> running;
    {
        QMutexLocker lock(&cacheMutex);
        epoch = cacheEpoch;
        cacheEnabled = documentBudget > 0;
        if (auto *d = documents.object(key))
            return ImageResult{*d, {}};
        running = activeReads.value(key);
        if (running && running->generation != contentGeneration)
            running.reset(); // content was dropped: decode fresh, never reuse
        if (running && running->owner == std::this_thread::get_id())
            running.reset(); // reentrant decoding must never wait on itself
        if (!running) {
            active = std::make_shared<ActiveRead>();
            active->owner = std::this_thread::get_id();
            active->generation = contentGeneration;
            activeReads.insert(key, active);
        }
    }
    bool previewPublished = false;
    if (running) {
        // A joined reader still publishes the stage pixels it can get (the RAW
        // preview) before it waits. The owning decode may run for seconds, and
        // joining without that preview would leave the window empty until then.
        if (beforeDecode) {
            beforeDecode();
            previewPublished = true;
        }
        if (cancel && cancel->load()) return {};
        QMutexLocker lock(&cacheMutex);
        while (!running->finished && !(cancel && cancel->load()))
            running->ready.wait(&cacheMutex, 20);
        if (cancel && cancel->load()) return {};
        if (running->document) {
            auto joined = *running->document; // shared pixels, no second copy
            return ImageResult{std::move(joined), {}};
        }
        // The owner failed; decode here so this reader keeps its own diagnostics
        // instead of inheriting another thread's error.
    }
    const auto finishRead = qScopeGuard([&] {
        if (!active) return;
        QMutexLocker lock(&cacheMutex);
        active->finished = true;
        if (activeReads.value(key) == active) activeReads.remove(key);
        active->ready.wakeAll();
    });
    if (beforeDecode && !previewPublished)
        beforeDecode();
    if (cancel && cancel->load()) return {};
    auto result = registry.read(path, page);
    if (result.ok() && imageFileKey(path) != revision) {
        if (cancel && cancel->load()) return {};
        return ImageResult{{}, IX_TEXT("Die Datei wurde während des Lesens verändert. Bitte erneut öffnen.")};
    }
    if (result.ok())
        result.document.sourceRevision = revision;
    if (result.ok() && cacheEnabled && result.document.pixels.sizeInBytes() <= 96 * 1024 * 1024) {
        const auto cost = addCost(documentMemoryCost(result.document), addCost(stringCost(key.second), addCost(stringCost(absolute), 64)));
        QMutexLocker lock(&cacheMutex);
        if (epoch == cacheEpoch) {
            auto decoded = decodedPreviews.find(absolute);
            if (decoded != decodedPreviews.end()) removeDecoded(decoded);
            if (makeRoomForOriginal(cost)) {
                presentations.remove(key);
                documents.insert(key, new ImageDocument(result.document), int(cost));
            }
            publishUsage();
        }
    }
    // Waiters receive the decoded document even when this owner was asked to
    // stop after the pixels were ready: the work is done and sharing it cannot
    // leak into any GUI state.
    if (active && result.ok())
        active->document = std::make_shared<ImageDocument>(result.document);
    if (cancel && cancel->load()) return {};
    return result;
}
std::optional<ImageLoadCache::PreviewPixels> ImageLoadCache::takePreviewPixels(const QString &path, QSize size,
                                                                            QImage::Format format, const QByteArray &codec) {
    if (!haveDecodedPreviews.load()) return {};
    const auto absolute = QFileInfo(path).absoluteFilePath();
    QString revision;
    quint64 epoch;
    {
        QMutexLocker lock(&cacheMutex);
        const auto it = decodedPreviews.constFind(absolute);
        if (it == decodedPreviews.cend()) return {};
        revision = it->revision; epoch = cacheEpoch;
    }
    const auto current = imageFileKey(path);
    QMutexLocker lock(&cacheMutex);
    auto it = decodedPreviews.find(absolute);
    if (epoch != cacheEpoch || it == decodedPreviews.end() || it->revision != revision) return {};
    if (current != revision || it->format != codec || it->image.pixels.size() != size ||
        (format != QImage::Format_Invalid && it->image.pixels.format() != format)) {
        removeDecoded(it); publishUsage(); return {};
    }
    auto result = std::move(it->image);
    removeDecoded(it); publishUsage();
    return result;
}
QImage ImageLoadCache::prepareDisplay(const QString &path, int page, const ImageDocument &document,
                                     bool manageColor, bool sharpen, const std::function<QImage()> &prepare) {
    const CacheKey key{QFileInfo(path).absoluteFilePath(), document.sourceRevision + QLatin1Char('/') + QString::number(page)};
    const auto original = document.pixels.cacheKey();
    quint64 epoch;
    bool eligible = false;
    {
        QMutexLocker lock(&cacheMutex);
        epoch = cacheEpoch;
        const auto *cached = documents.object(key);
        eligible = cached && cached->pixels.cacheKey() == original;
        if (eligible) {
            if (const auto *display = presentations.object(key); display && display->original == original &&
                display->manageColor == manageColor && display->sharpen == sharpen) return display->pixels;
        }
    }
    auto display = prepare();
    if (!eligible || display.isNull() || display.cacheKey() == original) return display;
    const auto cost = addCost(imageMemoryCost(display), addCost(stringCost(key.first), addCost(stringCost(key.second), 128)));
    QMutexLocker lock(&cacheMutex);
    const auto *cached = documents.object(key);
    if (epoch == cacheEpoch && cached && cached->pixels.cacheKey() == original) {
        presentations.setMaxCost(presentationCapacity(documents.totalCost()));
        if (cost <= presentations.maxCost())
            presentations.insert(key, new Presentation{display, original, manageColor, sharpen}, int(cost));
        publishUsage();
    }
    return display;
}
ImageLoadCache::Usage ImageLoadCache::usage() {
    QMutexLocker lock(&cacheMutex);
    return {documents.totalCost(), presentations.totalCost(), decodedCost, previews.totalCost(), cacheBudget()};
}
qint64 ImageLoadCache::retainedBytes() { return retainedCacheBytes.load(); }
bool ImageLoadCache::hasDocument(const QString &path, int page) {
    const auto absolute = QFileInfo(path).absoluteFilePath();
    const auto revision = imageFileKey(path);
    QMutexLocker lock(&cacheMutex);
    return documents.contains({absolute, revision + QLatin1Char('/') + QString::number(page)});
}
qint64 ImageLoadCache::speculativeAllowance() {
    QMutexLocker lock(&cacheMutex);
    constexpr qint64 propertyReserve = 64 * 1024;
    return std::max<qint64>(0, std::min({64 * MiB,
        qint64(documentBudget) - documents.totalCost() - propertyReserve,
        cacheBudget() - retainedCacheBytes.load() - propertyReserve}));
}
void ImageLoadCache::configure(int imageMiB, int previewMiB, bool rawPreviews) {
    QMutexLocker lock(&cacheMutex);
    ++cacheEpoch;
    const auto requestedDocuments = std::clamp(imageMiB, 0, 512) * 1024 * 1024;
    const auto requestedPreviews = std::clamp(previewMiB, 0, 64) * 1024 * 1024;
    if (cacheConfigured && requestedDocuments == documentBudget && requestedPreviews == previewBudget) {
        rawPreviewsEnabled.store(rawPreviews);
        return;
    }
    cacheConfigured = true;
    documentBudget = requestedDocuments;
    previewBudget = requestedPreviews;
    // Trim optional pixels before originals when the requested limits shrink.
    trimDecoded(0);
    presentations.setMaxCost(0);
    documents.setMaxCost(int(std::min<qint64>(documentBudget, cacheBudget())));
    previews.setMaxCost(int(std::min<qint64>(previewBudget, cacheBudget() - documents.totalCost())));
    publishUsage();
    rawPreviewsEnabled.store(rawPreviews);
}
void ImageLoadCache::clear() {
    ApngCodec::clearSession();
    QMutexLocker lock(&cacheMutex);
    ++cacheEpoch;
    ++contentGeneration;
    documents.clear();
    presentations.clear();
    decodedPreviews.clear(); decodedCost = 0;
    previews.clear();
    publishUsage();
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
    ++contentGeneration;
    for (const auto &key : documents.keys())
        if (affected.contains(lookupPath(key.first))) documents.remove(key);
    for (const auto &key : presentations.keys())
        if (affected.contains(lookupPath(key.first))) presentations.remove(key);
    for (const auto &key : previews.keys())
        if (affected.contains(lookupPath(key.first))) previews.remove(key);
    for (auto it = decodedPreviews.begin(); it != decodedPreviews.end();)
        if (affected.contains(lookupPath(it.key()))) { decodedCost -= it->cost; it = decodedPreviews.erase(it); }
        else ++it;
    publishUsage();
}
} // namespace ix
