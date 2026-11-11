// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_storage.h"
#include "app_storage.h"
#include "image_document.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QStorageInfo>
#include <QCoreApplication>
#include <QThread>
#include <QThreadPool>
#include <QDebug>
#include <QUuid>
#include <QtConcurrent>
#include <QtEndian>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#ifdef Q_OS_MACOS
#include <sys/mount.h>
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace ix {
namespace {
constexpr qint64 HeaderBytes = 65536;
constexpr char Magic[] = "uxzView pixels v1";
struct State {
    std::mutex mutex;
    ImageStorageOptions options;
    qint64 allocated = 0;
    std::unordered_map<const uchar *, QString> files;
};
std::shared_ptr<State> state() { static auto value = std::make_shared<State>(); return value; }
QString storageRoot(const ImageStorageOptions &o) {
    return o.root.isEmpty() ? QDir(AppStorage::cacheRoot()).filePath(QStringLiteral("swap")) : o.root;
}
void requireBackingSpace(QFile &file, qint64 required) {
    bool valid = false, readOnly = false;
    qint64 available = -1;
#ifdef Q_OS_MACOS
    // Qt 6.8 resolves the directory to its CF volume URL before statfs().
    // APFS firmlinks may then refer to the sealed system volume instead of the
    // writable data volume. Query the opened backing file's actual filesystem.
    struct statfs info{};
    if (::fstatfs(file.handle(), &info) == 0) {
        readOnly = (info.f_flags & MNT_RDONLY) != 0;
        const quint64 blocks = info.f_bavail, blockSize = info.f_bsize;
        if (blockSize && blocks <= quint64(std::numeric_limits<qint64>::max()) / blockSize) {
            available = qint64(blocks * blockSize);
            valid = true;
        }
    }
#else
    const QStorageInfo disk(QFileInfo(file).absolutePath());
    valid = disk.isValid() && disk.isReady();
    readOnly = disk.isReadOnly();
    available = disk.bytesAvailable();
#endif
    if (!valid || readOnly || available < required) {
        qWarning().noquote() << "Image backing storage rejected:" << file.fileName()
            << "valid=" << valid << "read_only=" << readOnly
            << "available_bytes=" << available << "required_bytes=" << required;
        throw std::runtime_error("Nicht genügend freier Speicherplatz für die Auslagerung.");
    }
}
bool reserveFile(QFile &file, qint64 bytes) {
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(int(file.handle())));
    FILE_STANDARD_INFO standard{};
    if (!GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard))) return false;
    const DWORD flags = GetFileAttributesW(reinterpret_cast<LPCWSTR>(file.fileName().utf16()));
    if (flags == INVALID_FILE_ATTRIBUTES || (flags & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE))) return false;
    FILE_ALLOCATION_INFO allocation{};
    allocation.AllocationSize.QuadPart = bytes;
    if (!SetFileInformationByHandle(handle, FileAllocationInfo, &allocation, sizeof(allocation))) return false;
#elif defined(Q_OS_MACOS)
    fstore_t allocation{};
    allocation.fst_flags = F_ALLOCATEALL;
    allocation.fst_posmode = F_PEOFPOSMODE;
    allocation.fst_length = bytes;
    if (fcntl(file.handle(), F_PREALLOCATE, &allocation) == -1) return false;
#else
    if (posix_fallocate(file.handle(), 0, off_t(bytes)) != 0) return false;
#endif
    return file.resize(bytes);
}
constexpr qint64 kAsyncRemoveThreshold = 16 * 1024 * 1024;
QThreadPool &deletePool() {
    // Intentionally leaked: backing mappings can be destroyed during static
    // teardown, when a function-local static pool may already be gone.
    static QThreadPool *pool = [] {
        auto *created = new QThreadPool;
        created->setMaxThreadCount(1);
        created->setExpiryTimeout(5000);
        return created;
    }();
    return *pool;
}
bool onMainThread() {
    const auto *app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}
struct Mapping {
    std::shared_ptr<State> owner;
    QFile file;
    std::unique_ptr<QLockFile> lock;
    uchar *base = nullptr;
    qint64 bytes = 0;
    bool created = false;
    ~Mapping() {
        if (base) { std::lock_guard guard(owner->mutex); owner->files.erase(base + HeaderBytes); }
        if (base) file.unmap(base);
        file.close();
        if (created) {
            // Deleting a multi-gigabyte backing file can take a noticeable amount
            // of time. Offload it from the GUI thread so replacing a large image
            // or clearing history does not stall the window.
            // The returned QFuture is deliberately unused: start() is the
            // documented Qt API for fire-and-forget work and avoids the
            // nodiscard warning that QtConcurrent::run() produces here.
            const auto path = file.fileName();
            if (bytes >= kAsyncRemoveThreshold && onMainThread())
                deletePool().start([path] { QFile::remove(path); });
            else
                QFile::remove(path);
        }
        lock.reset();
        if (bytes) { std::lock_guard guard(owner->mutex); owner->allocated -= bytes; }
    }
};
void release(void *info) { delete static_cast<Mapping *>(info); }
bool useDisk(const ImageStorageOptions &o, qint64 bytes, qint64 resident) {
    return bytes > std::max<qint64>(0, imageMemoryLimit() - resident) ||
        (o.spillThresholdBytes > 0 && bytes > o.spillThresholdBytes);
}
}
ImageStorageOptions imageStorageOptions() { auto s = state(); std::lock_guard guard(s->mutex); return s->options; }
void configureImageStorage(const ImageStorageOptions &options) {
    auto s = state(); std::lock_guard guard(s->mutex);
    s->options = options;
    s->options.maxBytes = std::clamp<qint64>(options.maxBytes, 0, 1048576 * MiB);
}
void configureImageStorageFromSettings() {
    QSettings settings;
    auto o = imageStorageOptions();
    o.enabled = settings.value(QStringLiteral("performance/swapEnabled"), true).toBool();
    o.maxBytes = std::clamp<qint64>(settings.value(QStringLiteral("performance/swapLimitMiB"), 65536).toLongLong(), 0, 1048576) * MiB;
    configureImageStorage(o);
}
qint64 imageBufferBytes(QSize size, QImage::Format format) {
    if (size.width() <= 0 || size.height() <= 0 || size.width() > ImageAxisLimit || size.height() > ImageAxisLimit ||
        format <= QImage::Format_Invalid || format >= QImage::NImageFormats) return -1;
    const int depth = QImage::toPixelFormat(format).bitsPerPixel();
    if (depth <= 0) return -1;
    const qint64 stride = ((qint64(size.width()) * depth + 31) / 32) * 4;
    const qint64 bytes = stride * size.height();
    return bytes <= std::numeric_limits<qsizetype>::max() - HeaderBytes ? bytes : -1;
}
bool canAllocateImage(QSize size, QImage::Format format, qint64 resident) {
    const qint64 bytes = imageBufferBytes(size, format);
    if (bytes <= 0 || resident < 0 || resident > imageMemoryLimit()) return false;
    auto s = state(); std::lock_guard guard(s->mutex);
    if (!useDisk(s->options, bytes, resident)) return true;
    return s->options.enabled && bytes + HeaderBytes <= s->options.maxBytes - s->allocated;
}
QImage allocateImage(QSize size, QImage::Format format, qint64 resident, bool forceFile) {
    const auto bytes = imageBufferBytes(size, format);
    const auto o = imageStorageOptions();
    if (bytes <= 0 || resident < 0 || resident > imageMemoryLimit()) throw std::runtime_error("Ungültige Bildgröße oder RAM-Budget überschritten.");
    if (!forceFile && !useDisk(o, bytes, resident)) {
        QImage image(size, format);
        if (!image.isNull()) return image;
        // An actual allocation failure can still use the configured disk budget.
    }
    if (!o.enabled) throw std::runtime_error("Bildpuffer überschreitet das RAM-Budget; Auslagerung ist deaktiviert.");
    auto mapping = std::make_unique<Mapping>();
    mapping->owner = state();
    const qint64 total = bytes + HeaderBytes;
    {
        std::lock_guard guard(mapping->owner->mutex);
        if (total > o.maxBytes - mapping->owner->allocated) throw std::runtime_error("Das Auslagerungsbudget reicht für dieses Bild nicht aus.");
        mapping->owner->allocated += total; mapping->bytes = total;
    }
    const auto root = storageRoot(o);
    const QFileInfo directory(root);
    if (directory.isSymLink() || directory.isJunction() || !QDir().mkpath(root)) throw std::runtime_error("Auslagerungsordner ist nicht verfügbar.");
#ifndef Q_OS_WIN
    if (!QFile::setPermissions(root,QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        throw std::runtime_error("Auslagerungsordner ist nicht verfügbar.");
#endif
    QLockFile gate(QDir(root).filePath(QStringLiteral("create.lock")));
    if (!gate.tryLock(3000)) throw std::runtime_error("Auslagerungsordner ist beschäftigt.");
    const auto path = QDir(root).filePath(QStringLiteral("pixels-%1.bin").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    mapping->lock = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
    if (!mapping->lock->tryLock()) throw std::runtime_error("Auslagerungsdatei konnte nicht gesperrt werden.");
    mapping->file.setFileName(path);
    mapping->created = mapping->file.open(QIODevice::ReadWrite | QIODevice::NewOnly);
    if (!mapping->created)
        throw std::runtime_error("Speicherplatz für die Auslagerungsdatei konnte nicht reserviert werden.");
#ifndef Q_OS_WIN
    // QFile's ordinary creation mode can inherit a permissive umask. Pixels
    // must not become readable by other users of a shared Linux/macOS host.
    if (!mapping->file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        throw std::runtime_error("Auslagerungsdatei konnte nicht geschützt werden.");
#endif
    requireBackingSpace(mapping->file, total + 64 * MiB);
    // Write a recognizable header before reserving potentially large extents:
    // crash cleanup can then remove even an incompletely extended owned file.
    QByteArray header(HeaderBytes, '\0');
    std::memcpy(header.data(), Magic, sizeof(Magic));
    const qint64 stride = bytes / size.height();
    qToLittleEndian<qint64>(size.width(), header.data() + 32);
    qToLittleEndian<qint64>(size.height(), header.data() + 40);
    qToLittleEndian<qint64>(format, header.data() + 48);
    qToLittleEndian<qint64>(stride, header.data() + 56);
    qToLittleEndian<qint64>(bytes, header.data() + 64);
    if (mapping->file.write(header) != header.size() || !mapping->file.flush()) throw std::runtime_error("Auslagerungsheader konnte nicht geschrieben werden.");
    // The per-file process lock now protects this recognizable file. Do not
    // serialize other instances behind a potentially slow multi-GB reservation.
    gate.unlock();
    if (!reserveFile(mapping->file,total)) throw std::runtime_error("Speicherplatz für die Auslagerungsdatei konnte nicht reserviert werden.");
    mapping->base = mapping->file.map(0, total);
    if (!mapping->base) throw std::runtime_error("Auslagerungsdatei konnte nicht in den Adressraum eingebunden werden.");
    auto *pixels = mapping->base + HeaderBytes;
    { std::lock_guard guard(mapping->owner->mutex); mapping->owner->files.emplace(pixels, path); }
    mapping->file.moveToThread(nullptr);
    QImage image(pixels, size.width(), size.height(), stride, format, release, mapping.get());
    if (image.isNull()) throw std::runtime_error("Bildpuffer konnte nicht erstellt werden.");
    mapping.release();
    return image;
}
bool imageIsFileBacked(const QImage &image) { return !imageBackingFile(image).isEmpty(); }
QString imageBackingFile(const QImage &image) {
    auto s = state(); std::lock_guard guard(s->mutex);
    auto found = s->files.find(image.constBits());
    return found == s->files.end() ? QString() : found->second;
}
qint64 mappedImageBytes() { auto s = state(); std::lock_guard guard(s->mutex); return s->allocated; }
void flushImageStorageDeletes() { deletePool().waitForDone(); }
QImage copyImageRegion(const QImage &image, QRect region) {
    region = region.intersected(image.rect());
    if (region.isEmpty()) return {};
    if (image.depth() < 8) {
        if (image.sizeInBytes() > imageMemoryLimit()/2) throw std::runtime_error("Diese Farbtiefe kann nicht ausgelagert zugeschnitten werden.");
        return image.copy(region);
    }
    auto out = allocateImage(region.size(), image.format(), imageIsFileBacked(image) ? 0 : image.sizeInBytes());
    out.setColorTable(image.colorTable());
    const int bytes = image.depth()/8;
    for (int y=0; y<region.height(); ++y)
        std::memcpy(out.scanLine(y), image.constScanLine(y+region.y()) + qint64(region.x())*bytes, size_t(region.width())*bytes);
    inheritImageProperties(image,out);
    out.setDevicePixelRatio(image.devicePixelRatio()); out.setOffset(image.offset());
    return out;
}
QImage transformImageOrientation(const QImage &image, int transformation) {
    if (!transformation) return image;
    if (image.depth() < 8 || transformation < 0 || transformation > 7) throw std::runtime_error("Nicht unterstützte Bildausrichtung.");
    const bool transpose = transformation & 4;
    const auto size = transpose ? image.size().transposed() : image.size();
    auto out = allocateImage(size, image.format(), imageIsFileBacked(image) ? 0 : image.sizeInBytes());
    out.setColorTable(image.colorTable());
    const int bytes=image.depth()/8;
    for (int y=0; y<image.height(); ++y) {
        const auto *row=image.constScanLine(y);
        for (int x=0; x<image.width(); ++x) {
            int tx=(transformation&1) ? image.width()-1-x : x;
            int ty=(transformation&2) ? image.height()-1-y : y;
            if (transpose) { const int previous=tx; tx=image.height()-1-ty; ty=previous; }
            std::memcpy(out.scanLine(ty)+qint64(tx)*bytes,row+qint64(x)*bytes,bytes);
        }
    }
    inheritImageProperties(image,out);
    out.setDevicePixelRatio(image.devicePixelRatio()); out.setOffset(image.offset());
    return out;
}
int cleanupStaleImageStorage(const QString &root) {
    QFileInfo directory(root);
    if (!directory.isDir() || directory.isSymLink() || directory.isJunction()) return 0;
    QLockFile gate(QDir(root).filePath(QStringLiteral("create.lock")));
    if (!gate.tryLock()) return 0;
    int removed = 0;
    for (const auto &info : QDir(root).entryInfoList({QStringLiteral("pixels-*.bin")}, QDir::Files | QDir::NoSymLinks)) {
        if (info.isJunction() || info.fileName().size() != 47 || QUuid(info.fileName().mid(7,36)).isNull()) continue;
        QLockFile lock(info.absoluteFilePath() + QStringLiteral(".lock"));
        if (!lock.tryLock()) continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto header = file.read(72);
        if (header.size() != 72 || std::memcmp(header.constData(), Magic, sizeof(Magic)) != 0) continue;
        const auto width = qFromLittleEndian<qint64>(header.constData()+32), height = qFromLittleEndian<qint64>(header.constData()+40);
        const auto bytes = qFromLittleEndian<qint64>(header.constData()+64);
        if (width <= 0 || height <= 0 || width > ImageAxisLimit || height > ImageAxisLimit || bytes <= 0 || bytes > 1048576 * MiB || file.size() < HeaderBytes || file.size() > bytes + HeaderBytes) continue;
        const auto format=qFromLittleEndian<qint64>(header.constData()+48);
        const auto stride=qFromLittleEndian<qint64>(header.constData()+56);
        if (format<=0 || format>=QImage::NImageFormats ||
            imageBufferBytes({int(width),int(height)},QImage::Format(format))!=bytes || stride!=bytes/height) continue;
        file.close();
        if (file.remove()) ++removed;
    }
    return removed;
}
} // namespace ix
