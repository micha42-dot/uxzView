// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/processing.h"
#include "core/localization.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>
#include <vector>
namespace ix::test {
namespace legacy_resize_detail {
double kernel(ResampleFilter f, double x) {
    x = std::abs(x);
    switch (f) {
    case ResampleFilter::Hermite:
        return x < 1 ? (2 * x - 3) * x * x + 1 : 0;
    case ResampleFilter::Triangle:
        return x < 1 ? 1 - x : 0;
    case ResampleFilter::Bell:
        return x < .5 ? .75 - x * x : x < 1.5 ? .5 * (x - 1.5) * (x - 1.5) : 0;
    case ResampleFilter::BSpline:
        return x < 1 ? (4 + x * x * (3 * x - 6)) / 6 : x < 2 ? std::pow(2 - x, 3) / 6 : 0;
    case ResampleFilter::Mitchell: {
        constexpr double b = 1.0 / 3, c = 1.0 / 3;
        return x < 1   ? ((12 - 9 * b - 6 * c) * x * x * x + (-18 + 12 * b + 6 * c) * x * x + 6 - 2 * b) / 6
               : x < 2 ? ((-b - 6 * c) * x * x * x + (6 * b + 30 * c) * x * x + (-12 * b - 48 * c) * x +
                          8 * b + 24 * c) /
                             6
                       : 0;
    }
    case ResampleFilter::Lanczos:
        if (x < 1e-10)
            return 1;
        return x < 3 ? 3 * std::sin(std::numbers::pi * x) * std::sin(std::numbers::pi * x / 3) /
                           (std::numbers::pi * std::numbers::pi * x * x)
                     : 0;
    default:
        return x < .5 ? 1 : 0;
    }
}
double radius(ResampleFilter f) {
    switch (f) {
    case ResampleFilter::Lanczos:
        return 3;
    case ResampleFilter::BSpline:
    case ResampleFilter::Mitchell:
        return 2;
    case ResampleFilter::Bell:
        return 1.5;
    default:
        return 1;
    }
}
struct Tap {
    int index;
    double weight;
};
using Weights = QVector<QVector<Tap>>;
Weights weights(int from, int to, ResampleFilter f, bool fast) {
    Weights result(to);
    double scale = double(from) / to;
    double widen = fast ? 1 : std::max(1.0, scale), support = radius(f) * widen;
    for (int i = 0; i < to; ++i) {
        double center = (i + .5) * scale - .5, total = 0;
        int begin = int(std::ceil(center - support)), end = int(std::floor(center + support));
        for (int j = begin; j <= end; ++j) {
            double w = kernel(f, (center - j) / widen);
            if (std::abs(w) > 1e-12) {
                result[i].append({std::clamp(j, 0, from - 1), w});
                total += w;
            }
        }
        if (std::abs(total) < 1e-10)
            result[i] = {{std::clamp(int(std::round(center)), 0, from - 1), 1}};
        else
            for (auto &t : result[i])
                t.weight /= total;
    }
    return result;
}
double toLinear(double x) {
    return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4);
}
double toSrgb(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x <= .0031308 ? x * 12.92 : 1.055 * std::pow(x, 1 / 2.4) - .055;
}
quint16 u16(double x) {
    return quint16(std::clamp(std::lround(x * 65535), 0L, 65535L));
}
QImage finish(const QImage &src, QImage dst) {
    inheritImageProperties(src, dst);
    if (imageChannelBits(src) <= 8)
        dst = dst.convertToFormat(src.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    if (dst.isNull())
        throw std::runtime_error("Speicher nicht verfügbar.");
    return dst;
}
QImage resample(const QImage &src, const ResizeOptions &o, const std::atomic_bool *cancel) {
    if (src.isNull())
        return {};
    const double density = o.dpi / .0254;
    if (!std::isfinite(density) || density < .5 ||
        density > double(std::numeric_limits<int>::max()) - .5)
        throw std::runtime_error(IX_TEXT("Die Auflösung kann nicht als gültige Bildmetadaten gespeichert werden.").toStdString());
    constexpr auto sizeError = "Größenänderung überschreitet das Speicherlimit von 512 MiB.";
    if (!safeSize(o.size))
        throw std::runtime_error(sizeError);
    const qint64 outputPixels = qint64(o.size.width()) * o.size.height();
    qint64 imageBuffers = src.sizeInBytes();
    if (o.filter == ResampleFilter::Nearest) {
        // Nearest copies native pixels directly; it has no 16-bit conversion
        // or separable-filter intermediate image.
        imageBuffers += ((qint64(o.size.width()) * src.depth() + 31) / 32) * 4 * o.size.height();
    } else {
        if (src.format() != QImage::Format_RGBA64)
            imageBuffers += qint64(src.width()) * src.height() * 8;
        imageBuffers += qint64(o.size.width()) * src.height() * 8 + outputPixels * 8;
        if (imageChannelBits(src) <= 8)
            imageBuffers += outputPixels * 4; // final 8-bit conversion overlaps
        if (o.sharpen)
            imageBuffers = std::max(imageBuffers, src.sizeInBytes() +
                outputPixels * (imageChannelBits(src) <= 8 ? 24 : 16));
    }
    if (imageBuffers > imageMemoryLimit())
        throw std::runtime_error(sizeError);
    QImage out;
    if (o.filter == ResampleFilter::Nearest) {
        out = QImage(o.size, src.format());
        if (out.isNull())
            throw std::runtime_error("Nicht genügend Speicher.");
        out.setColorTable(src.colorTable());
        const int bytes = src.depth() / 8;
        std::vector<int> columns(out.width());
        for (int x = 0; x < out.width(); ++x)
            columns[x] = int((qint64(2 * x + 1) * src.width()) / (2 * out.width()));
        for (int y = 0; y < out.height(); ++y) {
            if (cancel && cancel->load())
                throw std::runtime_error("Abgebrochen.");
            const int sourceY = int((qint64(2 * y + 1) * src.height()) / (2 * out.height()));
            const auto *source = src.constScanLine(sourceY);
            auto *target = out.scanLine(y);
            for (int x = 0; x < out.width(); ++x) {
                if (bytes)
                    std::memcpy(target + x * bytes, source + columns[x] * bytes, bytes);
                else
                    out.setPixel(x, y, src.pixelIndex(columns[x], sourceY));
            }
        }
    } else {
        auto input = src.convertToFormat(QImage::Format_RGBA64);
        QImage temp(o.size.width(), src.height(), QImage::Format_RGBA64);
        out = QImage(o.size, QImage::Format_RGBA64);
        if (input.isNull() || temp.isNull() || out.isNull())
            throw std::runtime_error("Nicht genügend Speicher.");
        // Fast shrinking limits the footprint; quality shrinking widens the kernel
        // to integrate source pixels and avoid aliasing.
        auto wx = weights(input.width(), out.width(), o.filter, o.fastDownscale);
        auto wy = weights(input.height(), out.height(), o.filter, o.fastDownscale);
        for (int y = 0; y < input.height(); ++y) {
            if (cancel && cancel->load())
                return {};
            auto *s = reinterpret_cast<const QRgba64 *>(input.constScanLine(y));
            auto *d = reinterpret_cast<QRgba64 *>(temp.scanLine(y));
            for (int x = 0; x < temp.width(); ++x) {
                double r = 0, g = 0, b = 0, a = 0;
                for (auto t : wx[x]) {
                    auto p = s[t.index];
                    double alpha = p.alpha() / 65535.0;
                    auto cv = [&](quint16 c) {
                        double v = c / 65535.0;
                        return o.linearLight ? toLinear(v) : v;
                    };
                    r += cv(p.red()) * alpha * t.weight;
                    g += cv(p.green()) * alpha * t.weight;
                    b += cv(p.blue()) * alpha * t.weight;
                    a += alpha * t.weight;
                }
                // Keep premultiplied components in intermediate storage.
                d[x] = QRgba64::fromRgba64(u16(r), u16(g), u16(b), u16(a));
            }
        }
        for (int y = 0; y < out.height(); ++y) {
            if (cancel && cancel->load())
                return {};
            auto *d = reinterpret_cast<QRgba64 *>(out.scanLine(y));
            for (int x = 0; x < out.width(); ++x) {
                double r = 0, g = 0, b = 0, a = 0;
                for (auto t : wy[y]) {
                    auto p = reinterpret_cast<const QRgba64 *>(temp.constScanLine(t.index))[x];
                    r += p.red() / 65535.0 * t.weight;
                    g += p.green() / 65535.0 * t.weight;
                    b += p.blue() / 65535.0 * t.weight;
                    a += p.alpha() / 65535.0 * t.weight;
                }
                if (a > 1e-10) {
                    r /= a;
                    g /= a;
                    b /= a;
                } else
                    r = g = b = 0;
                if (o.linearLight) {
                    r = toSrgb(r);
                    g = toSrgb(g);
                    b = toSrgb(b);
                }
                d[x] = QRgba64::fromRgba64(u16(r), u16(g), u16(b), u16(a));
            }
        }
        out = finish(src, std::move(out));
    }
    inheritImageProperties(src, out);
    out.setDotsPerMeterX(int(std::round(density)));
    out.setDotsPerMeterY(int(std::round(density)));
    if (o.sharpen && o.filter != ResampleFilter::Nearest)
        out = convolve(out, {0, -1, 0, -1, 5, -1, 0, -1, 0}, 3);
    return out;
}
} // namespace legacy_resize_detail
inline QImage referenceResample(const QImage &source, const ResizeOptions &options) {
    return legacy_resize_detail::resample(source, options, nullptr);
}
} // namespace ix::test
