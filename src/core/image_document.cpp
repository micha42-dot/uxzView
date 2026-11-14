// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "image_document.h"
#include <QColorSpace>
#include <QtCore/qfloat16.h>
#include <algorithm>
#include <cmath>
namespace ix {
double ImageDocument::dpiX() const {
    return pixels.dotsPerMeterX() > 0 ? pixels.dotsPerMeterX() * 0.0254 : 96.0;
}
double ImageDocument::dpiY() const {
    return pixels.dotsPerMeterY() > 0 ? pixels.dotsPerMeterY() * 0.0254 : 96.0;
}
bool safeSize(QSize size, int bpp) {
    return bpp > 0 && size.width() > 0 && size.height() > 0 && size.width() <= ImageAxisLimit && size.height() <= ImageAxisLimit &&
           qint64(size.width()) * size.height() <= imageMemoryLimit() / bpp;
}
int imageChannelBits(const QImage &image) {
    const auto format = image.pixelFormat();
    return std::max({format.redSize(), format.greenSize(), format.blueSize(), format.alphaSize()});
}
namespace {
template <typename Pixel, typename Predicate>
bool anyTransparentPixel(const QImage &image, Predicate transparent) {
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const Pixel *>(image.constScanLine(y));
        if (std::any_of(row, row + image.width(), transparent))
            return true;
    }
    return false;
}
}
bool imageHasRealTransparency(const QImage &image) {
    if (!image.hasAlphaChannel())
        return false;
    // Read the native alpha samples. Padding belongs to the row, and
    // premultiplication changes only the color channels.
    switch (image.format()) {
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
        return anyTransparentPixel<QRgb>(image, [](QRgb pixel) { return qAlpha(pixel) < 255; });
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied:
        return anyTransparentPixel<uchar[4]>(image, [](const auto &pixel) { return pixel[3] < 255; });
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied:
        return anyTransparentPixel<QRgba64>(image, [](QRgba64 pixel) { return !pixel.isOpaque(); });
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
        return anyTransparentPixel<qfloat16[4]>(image, [](const auto &pixel) { return float(pixel[3]) < 1; });
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
        return anyTransparentPixel<float[4]>(image, [](const auto &pixel) { return pixel[3] < 1; });
    default:
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y).alphaF() < 1)
                    return true;
        return false;
    }
}
QImage::Format imageWorkingFormat(const QImage &image, bool alpha) {
    if (image.pixelFormat().typeInterpretation() == QPixelFormat::FloatingPoint) {
        if (imageChannelBits(image) > 16)
            return alpha ? QImage::Format_RGBA32FPx4 : QImage::Format_RGBX32FPx4;
        return alpha ? QImage::Format_RGBA16FPx4 : QImage::Format_RGBX16FPx4;
    }
    if (imageChannelBits(image) > 8)
        return alpha ? QImage::Format_RGBA64 : QImage::Format_RGBX64;
    return alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32;
}
void inheritImageProperties(const QImage &source, QImage &target) {
    target.setColorSpace(source.colorSpace());
    target.setDotsPerMeterX(source.dotsPerMeterX());
    target.setDotsPerMeterY(source.dotsPerMeterY());
    for (const auto &key : source.textKeys())
        target.setText(key, source.text(key));
}
} // namespace ix
