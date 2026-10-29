// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "apng_codec.h"
#include "preview_service.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QMutex>
#include <QtEndian>
#include <algorithm>
#include <atomic>
namespace ix {
namespace {
quint32 u32(const QByteArray &data, qsizetype offset = 0) {
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + offset));
}
quint16 u16(const QByteArray &data, qsizetype offset) {
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + offset));
}
void put32(QByteArray &out, quint32 value) {
    char bytes[4];
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes));
    out.append(bytes, 4);
}
quint32 crc32(const QByteArray &data) {
    quint32 crc = 0xffffffff;
    for (auto c : data) {
        crc ^= quint8(c);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320 & quint32(-qint32(crc & 1)));
    }
    return ~crc;
}
QByteArray chunk(QByteArray type, const QByteArray &data) {
    QByteArray out;
    put32(out, quint32(data.size()));
    out += type;
    out += data;
    put32(out, crc32(type + data));
    return out;
}
struct Frame {
    QRect rect;
    int delay = 100, dispose = 0, blend = 0;
    QByteArray compressed;
};
// One process-wide session bounds retained compressed data and composition
// buffers even when registries are copied into workers. Active decode work
// owns its session independently and never holds the global cache lock.
constexpr qint64 SessionLimit = 64LL * 1024 * 1024;
struct Session {
    QMutex mutex;
    bool parsed = false, failed = false;
    std::atomic<qint64> chargedBytes = 0;
    QByteArray ihdr, common;
    QVector<Frame> frames;
    QImage canvas, previous;
    ImageDocument base;
    int current = -1;
    QString error;
    bool parse(const QString &path) {
        auto fail = [&](QString message) {
            error = QStringLiteral("APNG: ") + message;
            return false;
        };
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return fail(file.errorString());
        if (file.size() > 128LL * 1024 * 1024)
            return fail(QStringLiteral("Datei größer als 128 MiB."));
        auto bytes = file.readAll();
        if (bytes.left(8) != QByteArray::fromHex("89504e470d0a1a0a"))
            return fail(QStringLiteral("Ungültige PNG-Signatur."));
        quint32 expectedSequence = 0, declaredFrames = 0, plays = 0;
        bool imageData = false, ended = false;
        qsizetype pos = 8;
        while (pos + 12 <= bytes.size()) {
            quint32 length = u32(bytes, pos);
            if (length > quint32(bytes.size() - pos - 12))
                return fail(QStringLiteral("Abgeschnittener Chunk."));
            auto type = bytes.mid(pos + 4, 4), data = bytes.mid(pos + 8, length);
            if (crc32(type + data) != u32(bytes, pos + 8 + length))
                return fail(QStringLiteral("CRC-Prüfung fehlgeschlagen."));
            pos += length + 12;
            if (type == "IHDR") {
                if (length != 13 || !ihdr.isEmpty())
                    return fail(QStringLiteral("Ungültiger Bildkopf."));
                ihdr = data;
            } else if (type == "acTL") {
                if (length != 8 || declaredFrames)
                    return fail(QStringLiteral("Ungültiger Animationskopf."));
                declaredFrames = u32(data);
                plays = u32(data, 4);
                if (!declaredFrames || declaredFrames > 10000)
                    return fail(QStringLiteral("Ungültige Frameanzahl (Limit 10.000)."));
            } else if (type == "fcTL") {
                if (length != 26 || u32(data) != expectedSequence++)
                    return fail(QStringLiteral("Ungültige Framesequenz."));
                quint32 w = u32(data, 4), h = u32(data, 8), x = u32(data, 12), y = u32(data, 16);
                if (w > 100000 || h > 100000 || x > 100000 || y > 100000)
                    return fail(QStringLiteral("Frame zu groß."));
                Frame frame;
                frame.rect = {int(x), int(y), int(w), int(h)};
                int den = u16(data, 22);
                if (!den)
                    den = 100;
                frame.delay = std::max(20, qRound(u16(data, 20) * 1000.0 / den));
                frame.dispose = quint8(data[24]);
                frame.blend = quint8(data[25]);
                if (frame.dispose > 2 || frame.blend > 1)
                    return fail(QStringLiteral("Ungültige Frameoption."));
                frames.append(std::move(frame));
            } else if (type == "IDAT") {
                imageData = true;
                if (!frames.isEmpty())
                    frames.last().compressed += data;
            } else if (type == "fdAT") {
                if (length < 4 || frames.isEmpty() || u32(data) != expectedSequence++)
                    return fail(QStringLiteral("Ungültige Framedaten."));
                frames.last().compressed += data.mid(4);
            } else if (type == "IEND") {
                ended = true;
                break;
            } else if (!imageData && type != "acTL" && type != "fcTL")
                common += chunk(type, data);
        }
        if (!ended || ihdr.size() != 13 || declaredFrames != quint32(frames.size()))
            return fail(QStringLiteral("Unvollständige Animation."));
        const QSize size(int(u32(ihdr)), int(u32(ihdr, 4)));
        if (!safeSize(size) || qint64(size.width()) * size.height() * 8 * 4 > ImageMemoryLimit)
            return fail(QStringLiteral("Animation überschreitet das Speicherlimit."));
        canvas = QImage(size, quint8(ihdr[8]) == 16 ? QImage::Format_RGBA64 : QImage::Format_ARGB32);
        if (canvas.isNull())
            return fail(QStringLiteral("Speicher nicht verfügbar."));
        canvas.fill(Qt::transparent);
        ihdr = QByteArray(ihdr.constData(), ihdr.size());
        for (auto &frame : frames) {
            if (!canvas.rect().contains(frame.rect) || frame.rect.isEmpty() || frame.compressed.isEmpty())
                return fail(QStringLiteral("Ungültiges Framerechteck."));
            // Do not retain a slice that could own an entire much larger file.
            frame.compressed = QByteArray(frame.compressed.constData(), frame.compressed.size());
        }
        base.path = QFileInfo(path).absoluteFilePath();
        base.format = "apng";
        base.pageCount = frames.size();
        base.animated = frames.size() > 1;
        base.loopCount = plays ? int(std::min(plays - 1, quint32(INT_MAX))) : -1;
        base.fileBytes = bytes.size();
        base.metadata = readMetadata(path, base.metadataWarning);
        if (base.metadata)
            base.sourceHasExif = base.metadata->hasExif || base.metadata->hasIptc || base.metadata->hasXmp;
        return true;
    }
    bool advance(int page, std::atomic<quint64> &compositions) {
        auto fail = [&](QString message) {
            error = QStringLiteral("APNG: ") + message;
            return false;
        };
        if (page < 0 || page >= frames.size())
            return fail(QStringLiteral("Frame nicht vorhanden."));
        if (page < current) {
            // A backward seek replays from a transparent initial canvas; normal
            // forward playback keeps the previous canvas and disposal snapshot.
            canvas = QImage(canvas.size(), canvas.format());
            if (canvas.isNull())
                return fail(QStringLiteral("Speicher nicht verfügbar."));
            canvas.fill(Qt::transparent);
            previous = {};
            current = -1;
        }
        for (int i = current + 1; i <= page; ++i) {
            const auto &f = frames.at(i);
            if (i > 0) {
                const auto &last = frames.at(i - 1);
                if (last.dispose == 1) {
                    QPainter p(&canvas);
                    p.setCompositionMode(QPainter::CompositionMode_Source);
                    p.fillRect(last.rect, Qt::transparent);
                } else if (last.dispose == 2 && !previous.isNull())
                    canvas = previous;
            }
            previous = f.dispose == 2 ? canvas : QImage();
            QByteArray header = ihdr;
            qToBigEndian(quint32(f.rect.width()), reinterpret_cast<uchar *>(header.data()));
            qToBigEndian(quint32(f.rect.height()), reinterpret_cast<uchar *>(header.data() + 4));
            QByteArray png = QByteArray::fromHex("89504e470d0a1a0a") + chunk("IHDR", header) + common +
                             chunk("IDAT", f.compressed) + chunk("IEND", {});
            auto frame = QImage::fromData(png, "PNG");
            if (frame.isNull())
                return fail(QStringLiteral("PNG-Frame konnte nicht dekodiert werden."));
            if (i == 0)
                inheritImageProperties(frame, canvas);
            QPainter painter(&canvas);
            painter.setCompositionMode(f.blend == 0 ? QPainter::CompositionMode_Source
                                                    : QPainter::CompositionMode_SourceOver);
            painter.drawImage(f.rect.topLeft(), frame);
            painter.end();
            current = i;
            ++compositions;
        }
        return true;
    }
    qint64 retainedBytes() const {
        qint64 bytes = ihdr.capacity() + common.capacity() + canvas.sizeInBytes() + previous.sizeInBytes();
        for (const auto &frame : frames)
            bytes += frame.compressed.capacity() + sizeof(Frame);
        qint64 metadataBytes = 0;
        if (base.metadata)
            for (auto tag = base.metadata->tags.cbegin(); tag != base.metadata->tags.cend(); ++tag)
                metadataBytes += (tag.key().size() + tag.value().size()) * 4 + 128;
        // Reserve for native metadata containers as well as their text view.
        return bytes + std::max(base.fileBytes, metadataBytes);
    }
};
QMutex sessionMutex;
std::shared_ptr<Session> session;
QString sessionRevision;
QString sessionPath;
std::atomic<quint64> filesParsed = 0, framesComposited = 0;
} // namespace
bool ApngCodec::accepts(const QString &path) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.read(8) != QByteArray::fromHex("89504e470d0a1a0a"))
        return false;
    for (int count = 0; count < 10000 && !file.atEnd(); ++count) {
        auto header = file.read(8);
        if (header.size() != 8)
            return false;
        auto size = u32(header);
        auto type = header.mid(4);
        if (type == "acTL")
            return true;
        if (type == "IDAT" || type == "IEND")
            return false;
        if (qint64(size) + 4 > file.size() - file.pos())
            return false;
        file.seek(file.pos() + size + 4);
    }
    return false;
}
QList<FormatCapability> ApngCodec::capabilities() const {
    return {{QStringLiteral("APNG"),
             {QStringLiteral("apng")},
             true,
             false,
             QStringLiteral("üxzView APNG / Qt PNG (libpng)")}};
}
ImageResult ApngCodec::read(const QString &path, int page) const {
    const auto revision = imageFileKey(path);
    std::shared_ptr<Session> active;
    {
        QMutexLocker lock(&sessionMutex);
        if (!session || sessionRevision != revision) {
            session = std::make_shared<Session>();
            sessionRevision = revision;
            sessionPath = QFileInfo(path).absoluteFilePath();
        }
        active = session;
    }
    // Different files can decode concurrently. Only readers advancing the same
    // canvas serialize, and clearing the retained session never waits for I/O.
    QMutexLocker decode(&active->mutex);
    auto evict = [&] {
        QMutexLocker lock(&sessionMutex);
        if (session == active)
            session.reset();
    };
    if (!active->parsed && !active->failed) {
        ++filesParsed;
        active->parsed = active->parse(path);
        active->failed = !active->parsed;
    }
    if (active->failed) {
        evict();
        return {{}, active->error};
    }
    if (page < 0 || page >= active->frames.size())
        return {{}, QStringLiteral("APNG: Frame nicht vorhanden.")};
    if (!active->advance(page, framesComposited)) {
        active->failed = true;
        evict();
        return {{}, active->error};
    }
    auto document = active->base;
    document.pixels = active->canvas;
    document.page = page;
    document.delayMs = active->frames.at(page).delay;
    const auto bytes = active->retainedBytes();
    active->chargedBytes.store(bytes);
    if (bytes > SessionLimit)
        evict();
    return {std::move(document), {}};
}
ApngCacheStats ApngCodec::cacheStats() {
    QMutexLocker lock(&sessionMutex);
    return {filesParsed.load(), framesComposited.load(), session ? session->chargedBytes.load() : 0,
            SessionLimit};
}
void ApngCodec::clearSession() {
    QMutexLocker lock(&sessionMutex);
    session.reset();
    sessionRevision.clear();
    sessionPath.clear();
}
void ApngCodec::invalidateSession(const QStringList &absolutePaths) {
    QMutexLocker lock(&sessionMutex);
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    if (absolutePaths.contains(sessionPath, sensitivity)) {
        session.reset();
        sessionRevision.clear();
        sessionPath.clear();
    }
}
} // namespace ix
