// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/processing.h"
#include <algorithm>
#include <cmath>

namespace ix::test {
// Retained scalar convolution for independent pixel comparisons and timing.
// Keep the original arithmetic order, including zero taps and final rounding.
inline QImage referenceConvolve(const QImage &source, const QVector<double> &kernel, int side,
                                double divisor = 1, double offset = 0) {
    const auto input = source.convertToFormat(QImage::Format_RGBA64);
    QImage output(input.size(), input.format());
    const auto quantize = [](double value) {
        return quint16(std::clamp(std::lround(value * 65535), 0L, 65535L));
    };
    for (int y = 0; y < input.height(); ++y) {
        auto *target = reinterpret_cast<QRgba64 *>(output.scanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            double red = 0, green = 0, blue = 0;
            for (int j = 0; j < side; ++j) {
                const auto *row = reinterpret_cast<const QRgba64 *>(
                    input.constScanLine(std::clamp(y + j - side / 2, 0, input.height() - 1)));
                for (int i = 0; i < side; ++i) {
                    const auto pixel = row[std::clamp(x + i - side / 2, 0, input.width() - 1)];
                    const double weight = kernel[j * side + i];
                    red += pixel.red() * weight;
                    green += pixel.green() * weight;
                    blue += pixel.blue() * weight;
                }
            }
            const auto pixel = reinterpret_cast<const QRgba64 *>(input.constScanLine(y))[x];
            target[x] = QRgba64::fromRgba64(quantize(red / divisor / 65535 + offset),
                quantize(green / divisor / 65535 + offset), quantize(blue / divisor / 65535 + offset), pixel.alpha());
        }
    }
    inheritImageProperties(source, output);
    if (imageChannelBits(source) <= 8)
        output = output.convertToFormat(source.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    return output;
}
inline QImage filterPattern(QSize size, QImage::Format format) {
    QImage source(size, QImage::Format_RGBA64);
    quint32 state = 0x91d2e3f4;
    const auto next = [&] { state = state * 1664525u + 1013904223u; return quint16(state >> 16); };
    for (int y = 0; y < source.height(); ++y) {
        auto *row = reinterpret_cast<QRgba64 *>(source.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const auto r = next(), g = next(), b = next(), a = next();
            row[x] = QRgba64::fromRgba64(r, g, b, (x + y) % 5 ? a : 0);
        }
    }
    return source.convertToFormat(format);
}
} // namespace ix::test
