// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "image_document.h"
#include <QColorSpace>
#include <algorithm>
#include <cmath>
#include <optional>
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
bool imageHasRealTransparency(const QImage &image) {
    if (image.isNull() || !image.hasAlphaChannel())
        return false;
    // Format-only alpha (e.g. a fully opaque ARGB32 PNG) loses nothing on
    // export. Scan for a pixel that is actually non-opaque; sample huge
    // images on a coarse grid so gigapixel originals stay interactive.
    // Cost matters: this runs in the save path before every export.
    const int width = image.width(), height = image.height();
    const qint64 pixels = qint64(width) * height;
    const int step = pixels > 16 * 1024 * 1024 ? 7 : 1;
    if (image.format() == QImage::Format_Indexed8 && !image.isGrayscale()) {
        for (int y = 0; y < height; y += step)
            for (int x = 0; x < width; x += step)
                if (qAlpha(image.color(image.pixelIndex(x, y))) < 255)
                    return true;
        return false;
    }
    // Direct scanline sampling for the common unpremultiplied layouts.
    // Note: RGBA8888 stores bytes as R,G,B,A (not QRgb order), so alpha is
    // the 4th byte of each pixel rather than qAlpha() of a QRgb word.
    const auto sampleDirect = [&]() -> std::optional<bool> {
        if (image.format() == QImage::Format_ARGB32) {
            for (int y = 0; y < height; y += step) {
                const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = 0; x < width; x += step)
                    if (qAlpha(row[x]) < 255)
                        return true;
            }
            return false;
        }
        if (image.format() == QImage::Format_RGBA8888) {
            // bytesPerLine() may include row padding: derive the stride once,
            // not per row (integer division in the hot loop).
            const int bytesPerPixel = image.bytesPerLine() / width;
            for (int y = 0; y < height; y += step) {
                const auto *row = image.constScanLine(y);
                for (int x = 0; x < width; x += step)
                    if (row[x * bytesPerPixel + 3] < 255)
                        return true;
            }
            return false;
        }
        // Premultiplied values cannot prove opacity without a full
        // unpremultiply pass; other layouts need conversion.
        return std::nullopt;
    };
    if (const auto direct = sampleDirect())
        return *direct;
    const QImage view = image.convertToFormat(QImage::Format_ARGB32);
    if (view.isNull())
        return true; // Unconvertible: keep the warning rather than risking silent loss.
    for (int y = 0; y < height; y += step) {
        const auto *row = reinterpret_cast<const QRgb *>(view.constScanLine(y));
        for (int x = 0; x < width; x += step)
            if (qAlpha(row[x]) < 255)
                return true;
    }
    return false;
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
