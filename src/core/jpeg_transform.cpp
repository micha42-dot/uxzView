// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "jpeg_transform.h"
#include "image_document.h"
#include "file_revision.h"
#include "localization.h"
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <memory>
#include <turbojpeg.h>
namespace ix {
QString transformJpeg(const QString &source, const QString &target, JpegTransform op, QRect crop,
                      const std::optional<QString> &expectedTarget, const std::optional<QString> &expectedSource) {
    const auto targetRevision = [&] { return QFileInfo::exists(target) ? imageFileKey(target) : QString(); };
    const auto authorizedTarget = expectedTarget.value_or(targetRevision());
    const auto sourceRevision = expectedSource.value_or(imageFileKey(source));
    const auto conflict = [&]() -> QString {
        if (imageFileKey(source) != sourceRevision)
            return IX_TEXT("Die Datei wurde während des Lesens verändert. Bitte erneut öffnen.");
        if (targetRevision() != authorizedTarget)
            return IX_TEXT("Die Zieldatei wurde inzwischen verändert. Bitte vor dem Speichern erneut prüfen.");
        return {};
    };
    if (const auto error = conflict(); !error.isEmpty()) return error;
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly))
        return input.errorString();
    if (input.size() > 128LL * 1024 * 1024)
        return QStringLiteral("JPEG-Datei zu groß (Limit 128 MiB).");
    auto data = input.readAll();
    std::unique_ptr<void, decltype(&tjDestroy)> handle(tjInitTransform(), tjDestroy);
    if (!handle)
        return QStringLiteral("JPEG-Transformer konnte nicht gestartet werden.");
    int width = 0, height = 0, sampling = 0, color = 0;
    if (tjDecompressHeader3(handle.get(), reinterpret_cast<const unsigned char *>(data.constData()),
                            static_cast<unsigned long>(data.size()), &width, &height, &sampling, &color))
        return QString::fromUtf8(tjGetErrorStr2(handle.get()));
    if (!safeSize({width, height}))
        return QStringLiteral("JPEG-Bild überschreitet das Speicherlimit.");
    tjtransform transform{};
    transform.options = TJXOPT_PERFECT;
    switch (op) {
    case JpegTransform::Left:
        transform.op = TJXOP_ROT270;
        break;
    case JpegTransform::Right:
        transform.op = TJXOP_ROT90;
        break;
    case JpegTransform::Rotate180:
        transform.op = TJXOP_ROT180;
        break;
    case JpegTransform::Horizontal:
        transform.op = TJXOP_HFLIP;
        break;
    case JpegTransform::Vertical:
        transform.op = TJXOP_VFLIP;
        break;
    case JpegTransform::Crop:
        if (crop.isEmpty() || !QRect(0, 0, width, height).contains(crop))
            return QStringLiteral("Ungültige JPEG-Markierung.");
        if (crop.x() % tjMCUWidth[sampling] || crop.y() % tjMCUHeight[sampling])
            return QStringLiteral(
                       "Verlustfreies Freistellen benötigt X/Y auf MCU-Grenzen von %1 x %2 Pixeln.")
                .arg(tjMCUWidth[sampling])
                .arg(tjMCUHeight[sampling]);
        transform.op = TJXOP_NONE;
        transform.options |= TJXOPT_CROP;
        transform.r = {crop.x(), crop.y(), crop.width(), crop.height()};
        break;
    }
    unsigned char *output = nullptr;
    unsigned long size = 0;
    int result = tjTransform(handle.get(), reinterpret_cast<const unsigned char *>(data.constData()),
                             static_cast<unsigned long>(data.size()), 1, &output, &size, &transform, 0);
    std::unique_ptr<unsigned char, decltype(&tjFree)> buffer(output, tjFree);
    if (result)
        return QStringLiteral("Verlustfreie Transformation nicht möglich (ohne Randverlust): ") +
               QString::fromUtf8(tjGetErrorStr2(handle.get()));
    // Keep pixel dimensions and thumbnails consistent without recompressing JPEG coefficients.
    QString warning;
    auto metadata = readMetadata(source, warning);
    QByteArray encoded(reinterpret_cast<const char *>(output), qsizetype(size));
    if (metadata && (metadata->hasExif || metadata->hasIptc || metadata->hasXmp)) {
        if (metadata->tags.value(QStringLiteral("Exif.Image.Orientation"), QStringLiteral("1")) !=
            QStringLiteral("1"))
            return QStringLiteral(
                "EXIF-Ausrichtung ungleich 1 wird bei verlustfreien Operationen noch nicht unterstützt.");
        QSize resultSize = op == JpegTransform::Crop                                 ? crop.size()
                           : op == JpegTransform::Left || op == JpegTransform::Right ? QSize(height, width)
                                                                                     : QSize(width, height);
        QString error;
        encoded = attachMetadata(std::move(encoded), *metadata, resultSize, -1, -1, true, error);
        if (!error.isEmpty())
            return error;
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    if (file.write(encoded) != encoded.size()) {
        file.cancelWriting();
        return file.errorString();
    }
    if (const auto error = conflict(); !error.isEmpty()) {
        file.cancelWriting();
        return error;
    }
    if (!file.commit())
        return file.errorString();
    return {};
}
} // namespace ix
