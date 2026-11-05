// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "turbojpeg_codec.h"
#include "image_document.h"
#include "image_storage.h"
#include "localization.h"
#include "memory_policy.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <cstring>
#include <turbojpeg.h>
namespace ix {
namespace {
// Quick marker-level check for an embedded ICC profile inside a JPEG
// buffer.  Scanning for the APP2 "ICC_PROFILE\0" marker is reliable
// enough to decide the fallback path; false positives are extremely
// unlikely in practice.
bool hasEmbeddedIcc(const char *data, qsizetype len) {
    for (qsizetype i = 0; i < len - 20; ++i)
        if ((quint8)data[i] == 0xFF && (quint8)data[i + 1] == 0xE2 &&
            std::memcmp(data + i + 4, "ICC_PROFILE\0", 12) == 0)
            return true;
    return false;
}
} // namespace

bool TurboJpegCodec::accepts(const QString &path) const {
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QLatin1StringView("jpg") && suffix != QLatin1StringView("jpeg"))
        return false;
    // Large JPEGs and forced spills need Qt's mapped-buffer decode path.
    // Let QtCodec handle them; the turbo path serves the common case.
    QImageReader reader(path);
    if (!reader.canRead())
        return false;
    const auto size = reader.size();
    if (!size.isValid())
        return true; // can read but unknown size → try turbo
    // JPEG is always 8-bit/ch, at most 4 channels (RGB, no alpha), but we
    // match QtCodec's pixel-bytes calculation.
    const auto fmt = reader.imageFormat();
    const int depth = QImage::toPixelFormat(fmt).bitsPerPixel();
    constexpr int safeBpp = 8; // safeSize default bpp — conservative
    const bool fitsMemory = safeSize(size, std::max(1, depth / 8));
    if (!fitsMemory)
        return false;
    const auto o = imageStorageOptions();
    if (o.spillThresholdBytes > 0) {
        const qint64 pixelBytes = qint64(size.width()) * size.height() *
                                  std::max(1, depth / 8);
        if (pixelBytes > o.spillThresholdBytes)
            return false;
    }
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
        return qtJpegFallback(&io, path, page);
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
    ImageResult result;
    result.document.pixels = std::move(image);
    result.document.format = QByteArrayLiteral("jpeg");
    return result;
}

QList<FormatCapability> TurboJpegCodec::capabilities() const {
    return {{QStringLiteral("JPEG"),
             {QStringLiteral("jpg"), QStringLiteral("jpeg")}, true, false,
             QStringLiteral("üxzView / TurboJPEG")}};
}

ImageResult TurboJpegCodec::qtJpegFallback(QBuffer *io, const QString &path,
                                           int page) {
    Q_UNUSED(page);
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
    result.document.format = reader.format();  // already QByteArray
    result.document.pageCount = reader.imageCount() > 0 ? reader.imageCount() : 1;
    Q_UNUSED(path);
    // metadata and source revision are handled by CodecRegistry.
    return result;
}
} // namespace ix