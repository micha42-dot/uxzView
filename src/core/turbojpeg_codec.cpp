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
// JPEG header walking: marker segments before the entropy-coded scan carry the
// JFIF density, ICC profile and component count. Walking segments (instead of
// scanning a fixed byte window) keeps detection correct even when a large EXIF
// APP1 pushes the ICC APP2 past any fixed offset.
struct JpegHeader {
    bool parsed = false; // reached the scan with a valid SOF
    bool hasIcc = false;
    int components = 0;  // 3 = color; 1 = grayscale; 4 = CMYK/YCCK
    int densityUnits = 0, densityX = 0, densityY = 0; // JFIF APP0
};
JpegHeader inspectJpegHeader(const char *data, qsizetype len) {
    JpegHeader out;
    if (len < 4 || (quint8)data[0] != 0xFF || (quint8)data[1] != 0xD8)
        return out;
    qsizetype i = 2;
    while (i + 4 <= len) {
        if ((quint8)data[i] != 0xFF)
            return out;
        const quint8 marker = quint8(data[i + 1]);
        if (marker == 0xFF) { ++i; continue; } // fill byte
        if (marker == 0x01 || marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;
            continue;
        }
        if (marker == 0xDA) { // start of scan
            if (out.components > 0) out.parsed = true;
            return out;
        }
        if (marker == 0xD9) return out; // end of image before any scan
        const int length = (quint8(data[i + 2]) << 8) | quint8(data[i + 3]);
        if (length < 2) return out;
        const qsizetype payload = i + 4;
        const qsizetype end = payload + length - 2;
        if (end > len) return out;
        if (marker == 0xE0 && length >= 16 && std::memcmp(data + payload, "JFIF\0", 5) == 0) {
            out.densityUnits = quint8(data[payload + 7]);
            out.densityX = (quint8(data[payload + 8]) << 8) | quint8(data[payload + 9]);
            out.densityY = (quint8(data[payload + 10]) << 8) | quint8(data[payload + 11]);
        } else if (marker == 0xE2 && length >= 16 &&
                   std::memcmp(data + payload, "ICC_PROFILE\0", 12) == 0) {
            out.hasIcc = true;
        } else if ((marker == 0xC0 || marker == 0xC1 || marker == 0xC2) && length >= 8) {
            out.components = quint8(data[payload + 5]);
            out.parsed = true;
        }
        i = end;
    }
    return out;
}
// JFIF APP0 carries the pixel density that QImageReader would expose as
// dotsPerMeter. Keep document DPI identical between both decode paths.
void applyJfifDensity(QImage &image, const JpegHeader &header) {
    if (header.densityX <= 0 || header.densityY <= 0)
        return;
    if (header.densityUnits == 1) { // dots per inch
        image.setDotsPerMeterX(qRound(header.densityX / 0.0254));
        image.setDotsPerMeterY(qRound(header.densityY / 0.0254));
    } else if (header.densityUnits == 2) { // dots per centimetre
        image.setDotsPerMeterX(header.densityX * 100);
        image.setDotsPerMeterY(header.densityY * 100);
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
    // Rotated images must go through Qt's auto-transform path.
    if (reader.transformation() != QImageIOHandler::TransformationNone)
        return false;
    const auto size = reader.size();
    if (!size.isValid())
        return false; // unusual headers stay with Qt
    // JPEG always decodes to 8-bit RGB (no alpha); assume the widest output
    // (4 bytes per pixel) so the memory and spill decisions match QtCodec.
    constexpr qint64 bytesPerPixel = 4;
    if (!safeSize(size, int(bytesPerPixel)))
        return false;
    const auto o = imageStorageOptions();
    if (o.spillThresholdBytes > 0 &&
        qint64(size.width()) * size.height() * bytesPerPixel > o.spillThresholdBytes)
        return false;
    // Walk the segment header: ICC profiles and non-RGB component layouts
    // (grayscale, CMYK/YCCK) must keep Qt's pixel format and colour handling.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    constexpr qint64 kHeaderBytes = 256 * 1024;
    const auto head = file.read(std::min<qint64>(kHeaderBytes, file.size()));
    const auto header = inspectJpegHeader(head.constData(), head.size());
    return header.parsed && !header.hasIcc && header.components == 3;
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
    // Use Qt for every JPEG that carries a non-trivial EXIF orientation, an
    // embedded ICC profile or a non-RGB component layout (also re-checked here
    // in case the file changed after accepts()).
    QBuffer io(&buffer);
    io.open(QIODevice::ReadOnly);
    QImageReader reader(&io, QByteArrayLiteral("jpeg"));
    if (!reader.canRead())
        return {{}, reader.errorString()};
    const auto header = inspectJpegHeader(buffer.constData(), buffer.size());
    if (reader.transformation() != QImageIOHandler::TransformationNone ||
        header.hasIcc || (header.parsed && header.components != 3))
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
    applyJfifDensity(image, header);
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