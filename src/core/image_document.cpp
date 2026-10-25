// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "image_document.h"
#include <QColorSpace>
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
    return size.width() > 0 && size.height() > 0 && size.width() <= 100000 && size.height() <= 100000 &&
           qint64(size.width()) * size.height() <= ImageMemoryLimit / bpp;
}
int imageChannelBits(const QImage &image) {
    const auto format = image.pixelFormat();
    return std::max({format.redSize(), format.greenSize(), format.blueSize(), format.alphaSize()});
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
