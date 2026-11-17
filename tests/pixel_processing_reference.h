// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/processing.h"
#include <array>
#include <cmath>
#include <utility>
#include <vector>
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
inline QImage referenceAdjustColors(const QImage &source, const ColorAdjust &o) {
    return referencePixelTransform(source, [=](QRgba64 pixel) {
        double r = pixel.red() / 65535.0, g = pixel.green() / 65535.0, b = pixel.blue() / 65535.0,
               gray = .2126 * r + .7152 * g + .0722 * b;
        auto cv = [&](double v, int balance) {
            v = gray + (v - gray) * (1 + o.saturation / 100);
            v = (v - .5) * (1 + o.contrast / 100) + .5 + o.brightness / 255 + balance / 255.0;
            return quint16(std::clamp(std::lround(std::pow(std::clamp(v, 0.0, 1.0),
                1 / std::max(.01, o.gamma)) * 65535), 0L, 65535L));
        };
        return QRgba64::fromRgba64(cv(r, o.red), cv(g, o.green), cv(b, o.blue), pixel.alpha());
    });
}
inline QImage referenceAutoCorrect(const QImage &source) {
    if (source.isNull()) return source;
    std::vector<std::array<quint64, 3>> histogram(65536);
    quint64 count = 0;
    std::array<quint16, 3> lo{65535, 65535, 65535}, hi{0, 0, 0};
    const auto input = source.convertToFormat(QImage::Format_RGBA64);
    for (int y = 0; y < input.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgba64 *>(input.constScanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            if (!row[x].alpha()) continue;
            ++count;
            const std::array<quint16, 3> v{row[x].red(), row[x].green(), row[x].blue()};
            for (int c = 0; c < 3; ++c) ++histogram[v[c]][c];
        }
    }
    if (!count) return source;
    const quint64 clipped = count / 200;
    for (int c = 0; c < 3; ++c) {
        quint64 sum = 0;
        for (int v = 0; v <= 65535; ++v) {
            sum += histogram[size_t(v)][c];
            if (sum > clipped) { lo[c] = quint16(v); break; }
        }
        sum = 0;
        for (int v = 65535; v >= 0; --v) {
            sum += histogram[size_t(v)][c];
            if (sum > clipped) { hi[c] = quint16(v); break; }
        }
    }
    return referencePixelTransform(source, [=](QRgba64 pixel) {
        if (!pixel.alpha()) return pixel;
        auto cv = [&](int c, int v) {
            return hi[c] > lo[c]
                ? quint16(std::clamp(std::lround(double(v - lo[c]) / (hi[c] - lo[c]) * 65535), 0L, 65535L))
                : quint16(v);
        };
        return QRgba64::fromRgba64(cv(0, pixel.red()), cv(1, pixel.green()), cv(2, pixel.blue()), pixel.alpha());
    });
}
} // namespace ix::test
