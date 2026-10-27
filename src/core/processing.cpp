// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "processing.h"
#include "localization.h"
#include <QColorSpace>
#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <limits>
#include <stdexcept>
#include <vector>
namespace ix {
namespace {
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
    return dst;
}
template <class F> QImage perPixel(const QImage &src, F fn) {
    if (src.isNull())
        return {};
    auto out = src.convertToFormat(QImage::Format_RGBA64);
    if (out.isNull())
        throw std::runtime_error("Speicher nicht verfügbar.");
    for (int y = 0; y < out.height(); ++y) {
        auto *row = reinterpret_cast<QRgba64 *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
            row[x] = fn(row[x]);
    }
    return finish(src, std::move(out));
}
} // namespace
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
    if (imageBuffers > ImageMemoryLimit)
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
QImage rotate(const QImage &s, double deg) {
    QTransform t;
    t.rotate(deg);
    if (!safeSize(t.mapRect(QRectF(s.rect())).size().toSize()))
        throw std::runtime_error("Gedrehtes Bild überschreitet das Speicherlimit.");
    auto out = s.convertToFormat(imageWorkingFormat(s, s.hasAlphaChannel()))
                   .transformed(t, Qt::SmoothTransformation);
    if (!safeSize(out.size()))
        throw std::runtime_error("Ergebnis zu groß.");
    inheritImageProperties(s, out);
    return out;
}
QImage grayscale(const QImage &s) {
    return perPixel(s, [](QRgba64 p) {
        auto v = quint16(std::lround(.2126 * p.red() + .7152 * p.green() + .0722 * p.blue()));
        return QRgba64::fromRgba64(v, v, v, p.alpha());
    });
}
QImage negative(const QImage &s, int channelIndex) {
    return perPixel(s, [=](QRgba64 p) {
        return QRgba64::fromRgba64(channelIndex < 0 || channelIndex == 0 ? 65535 - p.red() : p.red(),
                                   channelIndex < 0 || channelIndex == 1 ? 65535 - p.green() : p.green(),
                                   channelIndex < 0 || channelIndex == 2 ? 65535 - p.blue() : p.blue(),
                                   p.alpha());
    });
}
QImage channel(const QImage &s, int c) {
    return perPixel(s, [=](QRgba64 p) {
        quint16 v = c == 0 ? p.red() : c == 1 ? p.green() : c == 2 ? p.blue() : p.alpha();
        return QRgba64::fromRgba64(v, v, v, p.alpha());
    });
}
QImage swapChannels(const QImage &s, int a, int b) {
    return perPixel(s, [=](QRgba64 p) {
        std::array<quint16, 3> v{p.red(), p.green(), p.blue()};
        std::swap(v[a], v[b]);
        return QRgba64::fromRgba64(v[0], v[1], v[2], p.alpha());
    });
}
QImage adjustColors(const QImage &s, const ColorAdjust &o) {
    return perPixel(s, [=](QRgba64 p) {
        double r = p.red() / 65535.0, g = p.green() / 65535.0, b = p.blue() / 65535.0,
               gray = .2126 * r + .7152 * g + .0722 * b;
        auto cv = [&](double v, int balance) {
            v = gray + (v - gray) * (1 + o.saturation / 100);
            v = (v - .5) * (1 + o.contrast / 100) + .5 + o.brightness / 255 + balance / 255.0;
            return u16(std::pow(std::clamp(v, 0.0, 1.0), 1 / std::max(.01, o.gamma)));
        };
        return QRgba64::fromRgba64(cv(r, o.red), cv(g, o.green), cv(b, o.blue), p.alpha());
    });
}
QImage convolve(const QImage &s, const QVector<double> &k, int side, double div, double offset) {
    auto input = s.convertToFormat(QImage::Format_RGBA64);
    QImage out(input.size(), input.format());
    int half = side / 2;
    if (out.isNull() || k.size() != side * side || div == 0)
        throw std::runtime_error("Ungültiger Filter oder nicht genügend Speicher.");
    constexpr std::array<double, 9> sharpen{0, -1, 0, -1, 5, -1, 0, -1, 0};
    if (side == 3 && div == 1 && offset == 0 && std::equal(k.cbegin(), k.cend(), sharpen.cbegin())) {
        // This integer kernel is exact in the RGBA64 working format. Its
        // [-262140, 327675] channel range fits int; rounding yields the same
        // clamped 16-bit samples as the scalar double convolution below.
        const auto channel = [](int center, int above, int left, int right, int below) {
            return quint16(std::clamp(5 * center - above - left - right - below, 0, 65535));
        };
        for (int y = 0; y < input.height(); ++y) {
            const auto *above = reinterpret_cast<const QRgba64 *>(input.constScanLine(std::max(0, y - 1)));
            const auto *center = reinterpret_cast<const QRgba64 *>(input.constScanLine(y));
            const auto *below = reinterpret_cast<const QRgba64 *>(input.constScanLine(std::min(y + 1, input.height() - 1)));
            auto *target = reinterpret_cast<QRgba64 *>(out.scanLine(y));
            for (int x = 0; x < input.width(); ++x) {
                const auto p = center[x], up = above[x], down = below[x];
                const auto left = center[std::max(0, x - 1)], right = center[std::min(x + 1, input.width() - 1)];
                target[x] = QRgba64::fromRgba64(
                    channel(p.red(), up.red(), left.red(), right.red(), down.red()),
                    channel(p.green(), up.green(), left.green(), right.green(), down.green()),
                    channel(p.blue(), up.blue(), left.blue(), right.blue(), down.blue()), p.alpha());
            }
        }
        return finish(s, std::move(out));
    }
    for (int y = 0; y < input.height(); ++y) {
        auto *d = reinterpret_cast<QRgba64 *>(out.scanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            double r = 0, g = 0, b = 0;
            for (int j = 0; j < side; ++j) {
                auto *row = reinterpret_cast<const QRgba64 *>(
                    input.constScanLine(std::clamp(y + j - half, 0, input.height() - 1)));
                for (int i = 0; i < side; ++i) {
                    auto p = row[std::clamp(x + i - half, 0, input.width() - 1)];
                    double w = k[j * side + i];
                    r += p.red() * w;
                    g += p.green() * w;
                    b += p.blue() * w;
                }
            }
            auto p = reinterpret_cast<const QRgba64 *>(input.constScanLine(y))[x];
            d[x] = QRgba64::fromRgba64(u16(r / div / 65535 + offset), u16(g / div / 65535 + offset),
                                       u16(b / div / 65535 + offset), p.alpha());
        }
    }
    return finish(s, std::move(out));
}
QImage autoCorrect(const QImage &s) {
    if (s.isNull())
        return s;
    // Percentile auto levels: isolated black/white pixels must not make a
    // low-contrast photograph look as though the command did nothing.
    std::vector<std::array<quint64, 3>> histogram(65536);
    quint64 count = 0;
    std::array<quint16, 3> lo{65535, 65535, 65535}, hi{0, 0, 0};
    auto input = s.convertToFormat(QImage::Format_RGBA64);
    for (int y = 0; y < input.height(); ++y) {
        auto *r = reinterpret_cast<const QRgba64 *>(input.constScanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            if (!r[x].alpha())
                continue;
            ++count;
            std::array<quint16, 3> v{r[x].red(), r[x].green(), r[x].blue()};
            for (int c = 0; c < 3; ++c) {
                ++histogram[v[c]][c];
            }
        }
    }
    if (!count)
        return s;
    const quint64 clipped = count / 200; // Discard 0.5% at either end.
    for (int c = 0; c < 3; ++c) {
        quint64 sum = 0;
        for (int v = 0; v <= 65535; ++v) {
            sum += histogram[size_t(v)][c];
            if (sum > clipped) {
                lo[c] = quint16(v);
                break;
            }
        }
        sum = 0;
        for (int v = 65535; v >= 0; --v) {
            sum += histogram[size_t(v)][c];
            if (sum > clipped) {
                hi[c] = quint16(v);
                break;
            }
        }
    }
    return perPixel(s, [=](QRgba64 p) {
        if (!p.alpha())
            return p;
        auto cv = [&](int c, int v) {
            return hi[c] > lo[c] ? u16(double(v - lo[c]) / (hi[c] - lo[c])) : quint16(v);
        };
        return QRgba64::fromRgba64(cv(0, p.red()), cv(1, p.green()), cv(2, p.blue()), p.alpha());
    });
}
QRect autoCropRect(const QImage &s, int tolerance) {
    if (s.isNull())
        return {};
    QColor bg = s.pixelColor(0, 0);
    int left = s.width(), right = -1, top = s.height(), bottom = -1;
    for (int y = 0; y < s.height(); ++y)
        for (int x = 0; x < s.width(); ++x) {
            auto c = s.pixelColor(x, y);
            if (std::abs(c.red() - bg.red()) > tolerance || std::abs(c.green() - bg.green()) > tolerance ||
                std::abs(c.blue() - bg.blue()) > tolerance || std::abs(c.alpha() - bg.alpha()) > tolerance) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    return right < left ? s.rect() : QRect(QPoint(left, top), QPoint(right, bottom));
}
QImage canvas(const QImage &s, int l, int r, int t, int b, QColor bg) {
    QSize size(s.width() + l + r, s.height() + t + b);
    if (!safeSize(size))
        throw std::runtime_error("Ungültige Leinwandgröße.");
    QImage out(size, imageWorkingFormat(s));
    out.fill(bg);
    QPainter p(&out);
    p.drawImage(l, t, s);
    p.end();
    inheritImageProperties(s, out);
    return out;
}
} // namespace ix
