// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "processing.h"
#include "image_storage.h"
#include "localization.h"
#include <QColorSpace>
#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define IX_SHARPEN_SSE2
#endif
namespace ix {
namespace {
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
void checkWorkingSet(const QImage &src, int extraBytesPerPixel) {
    const qint64 pixels = qint64(src.width()) * src.height();
    if (src.sizeInBytes() > imageMemoryLimit() ||
        pixels > (imageMemoryLimit() - src.sizeInBytes()) / extraBytesPerPixel)
        throw std::runtime_error(imageSizeLimitError(src.size(), extraBytesPerPixel + src.depth() / 8).toStdString());
}
bool nativeRgb32(const QImage &image) {
    return image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32;
}
#ifdef IX_SHARPEN_SSE2
__m128i sharpenFour(__m128i center, __m128i above, __m128i left, __m128i right, __m128i below,
                    bool opaque) {
    const auto zero = _mm_setzero_si128();
    const auto channel = [](__m128i c, __m128i up, __m128i l, __m128i r, __m128i down) {
        const auto neighbors = _mm_add_epi16(_mm_add_epi16(up, l), _mm_add_epi16(r, down));
        return _mm_sub_epi16(_mm_add_epi16(_mm_slli_epi16(c, 2), c), neighbors);
    };
    // Each signed word stays within [-1020, 1275]. Saturating back to bytes
    // matches the scalar clamp exactly; alpha comes from the original center.
    const auto low = channel(_mm_unpacklo_epi8(center, zero), _mm_unpacklo_epi8(above, zero),
        _mm_unpacklo_epi8(left, zero), _mm_unpacklo_epi8(right, zero), _mm_unpacklo_epi8(below, zero));
    const auto high = channel(_mm_unpackhi_epi8(center, zero), _mm_unpackhi_epi8(above, zero),
        _mm_unpackhi_epi8(left, zero), _mm_unpackhi_epi8(right, zero), _mm_unpackhi_epi8(below, zero));
    const auto alphaMask = _mm_set1_epi32(-16777216);
    const auto alpha = opaque ? alphaMask : _mm_and_si128(center, alphaMask);
    return _mm_or_si128(_mm_andnot_si128(alphaMask, _mm_packus_epi16(low, high)), alpha);
}
#endif
template <class F> QImage perPixel32(const QImage &src, F fn) {
    QImage out = allocateImage(src.size(), src.format(), imageIsFileBacked(src) ? 0 : src.sizeInBytes());
    if (out.isNull()) throw std::runtime_error("Speicher nicht verfügbar.");
    // convertToFormat() carried these properties in the legacy working buffer.
    out.setDevicePixelRatio(src.devicePixelRatio());
    out.setOffset(src.offset());
    const QRgb opaque = src.format() == QImage::Format_RGB32 ? 0xff000000u : 0;
    for (int y = 0; y < src.height(); ++y) {
        const auto *source = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        auto *target = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < src.width(); ++x) target[x] = fn(source[x] | opaque) | opaque;
    }
    return finish(src, std::move(out));
}
template <class F> QImage perPixel(const QImage &src, F fn) {
    if (src.isNull())
        return {};
    checkWorkingSet(src, 8 + (imageChannelBits(src) <= 8 ? 4 : 0));
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
    if (nativeRgb32(s))
        return perPixel32(s, [](QRgb pixel) {
            const int red = qRed(pixel), green = qGreen(pixel), blue = qBlue(pixel);
            const int sum = 2126 * red + 7152 * green + 722 * blue;
            int value = (sum + 5000) / 10000;
            // Expanding bytes by 257 and rounding back has the same thresholds.
            // Preserve the legacy floating-point tie behavior at exact halves.
            if (sum == value * 10000 - 5000) {
                const auto wide = quint16(std::lround(.2126 * (red * 257) +
                    .7152 * (green * 257) + .0722 * (blue * 257)));
                value = QRgba64::fromRgba64(wide, wide, wide, 65535).red8();
            }
            return qRgba(value, value, value, qAlpha(pixel));
        });
    return perPixel(s, [](QRgba64 p) {
        auto v = quint16(std::lround(.2126 * p.red() + .7152 * p.green() + .0722 * p.blue()));
        return QRgba64::fromRgba64(v, v, v, p.alpha());
    });
}
QImage negative(const QImage &s, int channelIndex) {
    if (nativeRgb32(s)) {
        const QRgb mask = channelIndex < 0 ? 0x00ffffffu : channelIndex == 0 ? 0x00ff0000u
                         : channelIndex == 1 ? 0x0000ff00u : channelIndex == 2 ? 0x000000ffu : 0;
        return perPixel32(s, [mask](QRgb pixel) { return pixel ^ mask; });
    }
    return perPixel(s, [=](QRgba64 p) {
        return QRgba64::fromRgba64(channelIndex < 0 || channelIndex == 0 ? 65535 - p.red() : p.red(),
                                   channelIndex < 0 || channelIndex == 1 ? 65535 - p.green() : p.green(),
                                   channelIndex < 0 || channelIndex == 2 ? 65535 - p.blue() : p.blue(),
                                   p.alpha());
    });
}
QImage channel(const QImage &s, int c) {
    if (nativeRgb32(s))
        return perPixel32(s, [c](QRgb pixel) {
            const int value = c == 0 ? qRed(pixel) : c == 1 ? qGreen(pixel) : c == 2 ? qBlue(pixel) : qAlpha(pixel);
            return qRgba(value, value, value, qAlpha(pixel));
        });
    return perPixel(s, [=](QRgba64 p) {
        quint16 v = c == 0 ? p.red() : c == 1 ? p.green() : c == 2 ? p.blue() : p.alpha();
        return QRgba64::fromRgba64(v, v, v, p.alpha());
    });
}
QImage swapChannels(const QImage &s, int a, int b) {
    if (a < 0 || a > 2 || b < 0 || b > 2)
        throw std::out_of_range("Farbkanal muss zwischen 0 und 2 liegen.");
    if (nativeRgb32(s)) {
        const int first = (2-a)*8, second = (2-b)*8;
        const QRgb mask = (0xffu << first) | (0xffu << second);
        return perPixel32(s, [=](QRgb pixel) {
            return (pixel & ~mask) | (((pixel >> first) & 0xffu) << second) |
                                     (((pixel >> second) & 0xffu) << first);
        });
    }
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
    if (s.isNull() || side <= 0 || qint64(side) * side > std::numeric_limits<int>::max() ||
        k.size() != qint64(side) * side || div == 0)
        throw std::runtime_error("Ungültiger Filter oder nicht genügend Speicher.");
    const int convolutionBytes = (s.format() == QImage::Format_RGBA64 ? 0 : 8) + 8;
    const int finalBytes = 8 + (imageChannelBits(s) <= 8 ? 4 : 0);
    checkWorkingSet(s, std::max(convolutionBytes, finalBytes));
    constexpr std::array<double, 9> sharpen{0, -1, 0, -1, 5, -1, 0, -1, 0};
    const bool standardSharpen = side == 3 && div == 1 && offset == 0 &&
        std::equal(k.cbegin(), k.cend(), sharpen.cbegin());
    if (standardSharpen && nativeRgb32(s)) {
        // Expanding each byte by 257, applying this integer kernel, clamping
        // and converting back is exactly the same byte result. Avoid both
        // RGBA64 working buffers and the input/output format conversions.
        QImage out(s.size(), s.format());
        if (out.isNull()) throw std::runtime_error("Ungültiger Filter oder nicht genügend Speicher.");
        const bool opaque = s.format() == QImage::Format_RGB32;
        const auto channel = [](int center, int above, int left, int right, int below) {
            return std::clamp(5 * center - above - left - right - below, 0, 255);
        };
        for (int y = 0; y < s.height(); ++y) {
            const auto *above = reinterpret_cast<const QRgb *>(s.constScanLine(std::max(0, y - 1)));
            const auto *center = reinterpret_cast<const QRgb *>(s.constScanLine(y));
            const auto *below = reinterpret_cast<const QRgb *>(s.constScanLine(std::min(y + 1, s.height() - 1)));
            auto *target = reinterpret_cast<QRgb *>(out.scanLine(y));
            const auto scalar = [&](int x) {
                const auto p = center[x], up = above[x], down = below[x];
                const auto left = center[std::max(0, x - 1)], right = center[std::min(x + 1, s.width() - 1)];
                return qRgba(channel(qRed(p), qRed(up), qRed(left), qRed(right), qRed(down)),
                    channel(qGreen(p), qGreen(up), qGreen(left), qGreen(right), qGreen(down)),
                    channel(qBlue(p), qBlue(up), qBlue(left), qBlue(right), qBlue(down)), opaque ? 255 : qAlpha(p));
            };
            int x = 0;
#ifdef IX_SHARPEN_SSE2
            target[x] = scalar(x); ++x;
            for (; x <= s.width() - 5; x += 4) {
                const auto pixels = sharpenFour(_mm_loadu_si128(reinterpret_cast<const __m128i *>(center + x)),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(above + x)),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(center + x - 1)),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(center + x + 1)),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(below + x)), opaque);
                _mm_storeu_si128(reinterpret_cast<__m128i *>(target + x), pixels);
            }
#endif
            for (; x < s.width(); ++x) target[x] = scalar(x);
        }
        return finish(s, std::move(out));
    }
    auto input = s.convertToFormat(QImage::Format_RGBA64);
    QImage out(input.size(), input.format());
    int half = side / 2;
    if (input.isNull() || out.isNull())
        throw std::runtime_error("Ungültiger Filter oder nicht genügend Speicher.");
    const auto finishFilter = [&] {
        // The final 8-bit conversion does not need the convolution input.
        input = {};
        return finish(s, std::move(out));
    };
    if (standardSharpen) {
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
        return finishFilter();
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
    return finishFilter();
}
QImage autoCorrect(const QImage &s) {
    if (s.isNull())
        return s;
    checkWorkingSet(s, 8 + (imageChannelBits(s) <= 8 ? 4 : 0));
    // Percentile auto levels: isolated black/white pixels must not make a
    // low-contrast photograph look as though the command did nothing.
    std::vector<std::array<quint64, 3>> histogram(65536);
    quint64 count = 0;
    std::array<quint16, 3> lo{65535, 65535, 65535}, hi{0, 0, 0};
    auto input = s.convertToFormat(QImage::Format_RGBA64);
    if (input.isNull())
        throw std::runtime_error("Speicher nicht verfügbar.");
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
    input = {}; // Histogram analysis is complete before allocating output pixels.
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
    const int bgRed = bg.red(), bgGreen = bg.green(), bgBlue = bg.blue(), bgAlpha = bg.alpha();
    int left = s.width(), right = -1, top = s.height(), bottom = -1;
    const auto different = [&](int red, int green, int blue, int alpha) {
        return std::abs(red - bgRed) > tolerance || std::abs(green - bgGreen) > tolerance ||
               std::abs(blue - bgBlue) > tolerance || std::abs(alpha - bgAlpha) > tolerance;
    };
    const auto scan = [&](auto rowAt, auto differs) {
        for (int y = 0; y < s.height(); ++y) {
            const auto *row = rowAt(y);
            for (int x = 0; x < s.width(); ++x) if (differs(row[x])) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    };
    if (s.format() == QImage::Format_RGB32 || s.format() == QImage::Format_ARGB32) {
        const bool alpha = s.format() == QImage::Format_ARGB32;
        scan([&](int y) { return reinterpret_cast<const QRgb *>(s.constScanLine(y)); },
             [&](QRgb p) { return different(qRed(p), qGreen(p), qBlue(p), alpha ? qAlpha(p) : 255); });
    } else if (s.format() == QImage::Format_RGBA64 || s.format() == QImage::Format_RGBX64) {
        const bool alpha = s.format() == QImage::Format_RGBA64;
        scan([&](int y) { return reinterpret_cast<const QRgba64 *>(s.constScanLine(y)); },
             [&](QRgba64 p) { return different(p.red8(), p.green8(), p.blue8(), alpha ? p.alpha8() : 255); });
    } else {
        // Preserve Qt's exact unpremultiplication, palette and floating-point
        // behavior rather than allocating/converting another full-size image.
        for (int y = 0; y < s.height(); ++y)
            for (int x = 0; x < s.width(); ++x) {
                const auto c = s.pixelColor(x, y);
                if (different(c.red(), c.green(), c.blue(), c.alpha())) {
                    left = std::min(left, x); right = std::max(right, x);
                    top = std::min(top, y); bottom = std::max(bottom, y);
                }
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
