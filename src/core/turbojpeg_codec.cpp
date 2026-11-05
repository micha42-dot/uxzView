// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "turbojpeg_codec.h"
#include "image_document.h"
#include "image_storage.h"
#include "localization.h"
#include "memory_policy.h"
#include "metadata.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <cstring>
#include <turbojpeg.h>
namespace ix {
namespace {
// Marker-level checks only need the header region: APP0/APP2 always precede
// the entropy-coded scan data. Bounding the scan keeps the cost constant even
// for multi-megabyte files and avoids accidental matches inside scan data.
constexpr qsizetype kHeaderScanLimit = 64 * 1024;
bool hasEmbeddedIcc(const char *data, qsizetype len) {
    const qsizetype limit = std::min<qsizetype>(len, kHeaderScanLimit);
    for (qsizetype i = 0; i + 20 <= limit; ++i)
        if ((quint8)data[i] == 0xFF && (quint8)data[i + 1] == 0xE2 &&
            std::memcmp(data + i + 4, "ICC_PROFILE\0", 12) == 0)
            return true;
    return false;
}
// JFIF APP0 carries the pixel density that QImageReader would expose as
// dotsPerMeter. Keep document DPI identical between both decode paths.
void applyJfifDensity(QImage &image, const QByteArray &data) {
    const qsizetype limit = std::min<qsizetype>(data.size(), 4096);
    const char *bytes = data.constData();
    for (qsizetype i = 0; i + 16 <= limit; ++i) {
        if ((quint8)bytes[i] != 0xFF || (quint8)bytes[i + 1] != 0xE0 ||
            std::memcmp(bytes + i + 4, "JFIF\0", 5) != 0)
            continue;
        const int units = (quint8)bytes[i + 11];
        const int xd = (quint8(bytes[i + 12]) << 8) | quint8(bytes[i + 13]);
        const int yd = (quint8(bytes[i + 14]) << 8) | quint8(bytes[i + 15]);
        if (xd > 0 && yd > 0) {
            if (units == 1) { // dots per inch
                image.setDotsPerMeterX(qRound(xd / 0.0254));
                image.setDotsPerMeterY(qRound(yd / 0.0254));
            } else if (units == 2) { // dots per centimetre
                image.setDotsPerMeterX(xd * 100);
                image.setDotsPerMeterY(yd * 100);
            }
        }
        return;
    }
}
// Mirror QtCodec's document bookkeeping (path, metadata, EXIF presence) so the
// turbo path is indistinguishable to callers.
void finishDocument(ImageDocument &document, const QString &path,
                    const QByteArray &head) {
    document.path = QFileInfo(path).absoluteFilePath();
    document.metadata = readMetadata(path, document.metadataWarning);
    if (document.metadata) {
        document.sourceHasExif = document.metadata->hasExif ||
                                 document.metadata->hasIptc ||
                                 document.metadata->hasXmp;
    } else {
        document.sourceHasExif = head.contains("Exif\0\0") ||
                                 head.contains("http://ns.adobe.com/xap/") ||
                                 head.contains("Photoshop 3.0");
    }
}
} // namespace

bool TurboJpegCodec::accepts(const QString &path) const {
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QLatin1StringView("jpg") && suffix != QLatin1StringView("jpeg") &&
        suffix != QLatin1StringView("jpe"))
        return false;
    // Large JPEGs and forced spills need Qt's mapped-buffer decode path.
    // Let QtCodec handle them; the turbo path serves the common case.
    QImageReader reader(path);
    if (!reader.canRead())
        return false;
    const auto size = reader.size();
    if (!size.isValid())
        return true; // can read but unknown size → try turbo
    // JPEG always decodes to 8-bit RGB (no alpha); assume the widest output
    // (4 bytes per pixel) so the memory and spill decisions match QtCodec.
    constexpr qint64 bytesPerPixel = 4;
    if (!safeSize(size, int(bytesPerPixel)))
        return false;
    const auto o = imageStorageOptions();
    if (o.spillThresholdBytes > 0 &&
        qint64(size.width()) * size.height() * bytesPerPixel > o.spillThresholdBytes)
        return false;
    return true;
}

ImageResult TurboJpegCodec::read(const QString &path, int page) const {
    if (page != 0)
        return {{}, IX_TEXT("TurboJPEG: nur Seite 0.")};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, file.errorString()};
    constexpr qint64 maxBytes = 256LL * 1024 * 1024;
    if (file.size() > maxBytes)
        return {{}, IX_TEXT("JPEG-Datei zu groß.")};
    QByteArray buffer = file.readAll();
    file.close();
    if (buffer.isEmpty())
        return {{}, file.errorString()};
    // Use Qt for every JPEG that carries a non-trivial EXIF orientation or an
    // embedded ICC profile so that colour and transforms stay correct.
    QBuffer io(&buffer);
    io.open(QIODevice::ReadOnly);
    QImageReader reader(&io, QByteArrayLiteral("jpeg"));
    if (!reader.canRead())
        return {{}, reader.errorString()};
    if (reader.transformation() != QImageIOHandler::TransformationNone ||
        hasEmbeddedIcc(buffer.constData(), buffer.size()))
        return qtJpegFallback(&io, buffer, path);
    // Direct turbo decode — fast path for unrotated sRGB JPEGs.
    tjhandle handle = tjInitDecompress();
    if (!handle)
        return {{}, IX_TEXT("TurboJPEG nicht verfügbar.")};
    int width = 0, height = 0, jpegSubsamp = 0, colorSpace = 0;
    const auto *src = reinterpret_cast<const unsigned char *>(buffer.constData());
    if (tjDecompressHeader3(handle, src, buffer.size(), &width, &height,
                             &jpegSubsamp, &colorSpace) != 0) {
        auto error = QString::fromUtf8(tjGetErrorStr2(handle));
        tjDestroy(handle);
        return {{}, error};
    }
    // Keep Qt's pixel format for grayscale and CMYK/YCCK sources so the
    // resulting document matches the Qt decode exactly.
    if (colorSpace != TJCS_YCbCr && colorSpace != TJCS_RGB) {
        tjDestroy(handle);
        return qtJpegFallback(&io, buffer, path);
    }
    if (!safeSize({width, height}, 3)) {
        QString limit = imageSizeLimitError({width, height}, 3);
        tjDestroy(handle);
        return limit.isEmpty() ? ImageResult{{}, IX_TEXT("Bild zu groß.")}
                               : ImageResult{{}, limit};
    }
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        tjDestroy(handle);
        return {{}, IX_TEXT("Speicher nicht verfügbar.")};
    }
    if (tjDecompress2(handle, src, buffer.size(), image.bits(), width, 0, height,
                       TJPF_RGB, 0) != 0) {
        auto error = QString::fromUtf8(tjGetErrorStr2(handle));
        tjDestroy(handle);
        return {{}, error};
    }
    tjDestroy(handle);
    image = image.convertToFormat(QImage::Format_RGB32);
    if (image.isNull())
        return {{}, IX_TEXT("Speicher nicht verfügbar.")};
    applyJfifDensity(image, buffer);
    ImageResult result;
    result.document.pixels = std::move(image);
    result.document.format = QByteArrayLiteral("jpeg");
    result.document.page = 0;
    result.document.pageCount = 1;
    result.document.animated = false;
    result.document.delayMs = 100;
    result.document.loopCount = 0;
    finishDocument(result.document, path, buffer.left(256 * 1024));
    return result;
}

// Qt advertises the JPEG capability (and handles ICC/rotated/CMYK variants),
// so this codec deliberately reports no separate capability entry. Otherwise
// the format list would show JPEG twice.
ImageResult TurboJpegCodec::qtJpegFallback(QBuffer *io, const QByteArray &buffer,
                                           const QString &path) {
    // io is already positioned at the start and contains the full JPEG.
    io->seek(0);
    QImageReader reader(io, QByteArrayLiteral("jpeg"));
    reader.setAutoTransform(true);
    auto image = reader.read();
    if (image.isNull())
        return {{}, reader.errorString()};
    if (!safeSize(image.size(), std::max(8, image.depth() / 8)))
        return {{}, IX_TEXT("Bild zu groß.")};
    ImageResult result;
    result.document.pixels = std::move(image);
    result.document.format = reader.format();
    result.document.page = 0;
    result.document.pageCount = std::max(1, reader.imageCount());
    result.document.animated = reader.supportsAnimation() && result.document.pageCount != 1;
    result.document.delayMs = std::max(20, reader.nextImageDelay());
    result.document.loopCount = reader.loopCount();
    finishDocument(result.document, path, buffer.left(256 * 1024));
    return result;
}
} // namespace ix