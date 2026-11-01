// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/processing.h"
#include <array>
#include <cmath>
#include <utility>
namespace ix::test {
// Legacy full-RGBA64 path retained independently of native 32-bit processing.
template <class Fn> QImage referencePixelTransform(const QImage &source, Fn fn) {
    if (source.isNull()) return {};
    auto output = source.convertToFormat(QImage::Format_RGBA64);
    for (int y = 0; y < output.height(); ++y) {
        auto *row = reinterpret_cast<QRgba64 *>(output.scanLine(y));
        for (int x = 0; x < output.width(); ++x) row[x] = fn(row[x]);
    }
    inheritImageProperties(source, output);
    if (imageChannelBits(source) <= 8)
        output = output.convertToFormat(source.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    return output;
}
inline QImage referenceNegative(const QImage &source, int channel = -1) {
    return referencePixelTransform(source, [=](QRgba64 pixel) {
        return QRgba64::fromRgba64(channel < 0 || channel == 0 ? 65535-pixel.red() : pixel.red(),
            channel < 0 || channel == 1 ? 65535-pixel.green() : pixel.green(),
            channel < 0 || channel == 2 ? 65535-pixel.blue() : pixel.blue(),pixel.alpha());
    });
}
inline QImage referenceGrayscale(const QImage &source) {
    return referencePixelTransform(source, [](QRgba64 pixel) {
        const auto value = quint16(std::lround(.2126 * pixel.red() + .7152 * pixel.green() + .0722 * pixel.blue()));
        return QRgba64::fromRgba64(value, value, value, pixel.alpha());
    });
}
inline QImage referenceChannel(const QImage &source, int channel) {
    return referencePixelTransform(source, [=](QRgba64 pixel) {
        const auto value = channel == 0 ? pixel.red() : channel == 1 ? pixel.green()
                                                : channel == 2 ? pixel.blue() : pixel.alpha();
        return QRgba64::fromRgba64(value,value,value,pixel.alpha());
    });
}
inline QImage referenceSwap(const QImage &source, int first, int second) {
    return referencePixelTransform(source, [=](QRgba64 pixel) {
        std::array<quint16,3> values{pixel.red(),pixel.green(),pixel.blue()};
        std::swap(values[first],values[second]);
        return QRgba64::fromRgba64(values[0],values[1],values[2],pixel.alpha());
    });
}
} // namespace ix::test
