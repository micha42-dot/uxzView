// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "processing.h"
#include "image_storage.h"
#include "localization.h"
#include "resize_transfer.h"
#include <QCache>
#include <QThread>
#include <QScopeGuard>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define IX_RESIZE_SSE2
#endif
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
using detail::resizeToLinear;
using detail::resizeToSrgb;
const double *linearInputValues() {
    // Every normalized source channel is an integer RGBA64 sample. Evaluate
    // the original transfer function once for each possible sample, exactly;
    // no interpolation or approximation, and no allocation until gamma is used.
    static const auto values = [] {
        auto result = std::make_unique<std::array<double, 65536>>();
        for (size_t i = 0; i < result->size(); ++i) (*result)[i] = resizeToLinear(double(i) / 65535.0);
        return result;
    }();
    return values->data();
}
const detail::ResizeByteTransfer *linearOutputBytes() {
    static const auto table = std::make_unique<const detail::ResizeByteTransfer>();
    return table.get();
}
quint16 u16(double x) {
    return quint16(std::clamp(std::lround(x * 65535), 0L, 65535L));
}
int channelBits(QImage::Format format) {
    const auto pixel = QImage::toPixelFormat(format);
    return std::max({pixel.redSize(), pixel.greenSize(), pixel.blueSize(), pixel.alphaSize()});
}
qint64 resizeRowCacheBudget() { return std::min<qint64>(64 * MiB, imageMemoryLimit() / 16); }
// Horizontal rows retain the original RGBA64 rounding, but not a full-frame
// conversion/intermediate. A bounded cache reuses overlapping vertical support.
QImage resampleRows(const QImage &src, const ResizeOptions &o, const std::atomic_bool *cancel) {
    auto wx = weights(src.width(), o.size.width(), o.filter, o.fastDownscale);
    auto wy = weights(src.height(), o.size.height(), o.filter, o.fastDownscale);
    QCache<int, QImage> rows{int(resizeRowCacheBudget())};
    const bool hasAlpha = src.hasAlphaChannel();
    const auto *linearInput = o.linearLight ? linearInputValues() : nullptr;
    const auto palette = src.colorTable();
    const auto format = channelBits(src.format()) > 8 ? QImage::Format_RGBA64 : hasAlpha ? QImage::Format_ARGB32 : QImage::Format_RGB32;
    const qint64 resident = estimatedResizeBytes(src.size(),src.format(),o,src.sizeInBytes()) - imageBufferBytes(o.size,format) - (imageIsFileBacked(src) ? src.sizeInBytes() : 0);
    QImage out = allocateImage(o.size, format, resident);
    if (out.isNull()) throw std::runtime_error("Nicht genügend Speicher.");
    QVector<std::array<double,4>> sums(o.size.width());
    for (int y = 0; y < out.height(); ++y) {
        if (cancel && cancel->load()) return {};
        std::fill(sums.begin(), sums.end(), std::array<double,4>{});
        for (const auto vertical : wy[y]) {
            if (cancel && cancel->load()) return {};
            QImage horizontal;
            if (const auto *cached = rows.object(vertical.index)) horizontal = *cached;
            else {
                QImage view(src.constScanLine(vertical.index), src.width(), 1, src.bytesPerLine(), src.format());
                if (!palette.isEmpty()) view.setColorTable(palette);
                const auto input = view.convertToFormat(QImage::Format_RGBA64);
                horizontal = QImage(o.size.width(), 1, QImage::Format_RGBA64);
                if (input.isNull() || horizontal.isNull()) throw std::runtime_error("Nicht genügend Speicher.");
                const auto *s = reinterpret_cast<const QRgba64 *>(input.constBits());
                auto *d = reinterpret_cast<QRgba64 *>(horizontal.bits());
                for (int x = 0; x < horizontal.width(); ++x) {
                    double r = 0, g = 0, b = 0, a = 0;
                    for (const auto tap : wx[x]) {
                        const auto p = s[tap.index];
                        const double alpha = p.alpha() / 65535.0;
                        const auto cv = [&](quint16 c) { return linearInput ? linearInput[c] : c / 65535.0; };
                        r += cv(p.red()) * alpha * tap.weight;
                        g += cv(p.green()) * alpha * tap.weight;
                        b += cv(p.blue()) * alpha * tap.weight;
                        a += alpha * tap.weight;
                    }
                    d[x] = QRgba64::fromRgba64(u16(r), u16(g), u16(b), u16(a));
                }
                rows.insert(vertical.index, new QImage(horizontal), int(horizontal.sizeInBytes()));
            }
            const auto *row = reinterpret_cast<const QRgba64 *>(horizontal.constBits());
            for (int x = 0; x < out.width(); ++x) {
                const auto p = row[x];
                sums[x][0] += p.red() / 65535.0 * vertical.weight;
                sums[x][1] += p.green() / 65535.0 * vertical.weight;
                sums[x][2] += p.blue() / 65535.0 * vertical.weight;
                sums[x][3] += p.alpha() / 65535.0 * vertical.weight;
            }
        }
        auto *target = reinterpret_cast<QRgb *>(out.scanLine(y));
        auto *wide = reinterpret_cast<QRgba64 *>(target);
        for (int x = 0; x < out.width(); ++x) {
            auto [r,g,b,a] = sums[x];
            if (a > 1e-10) { r /= a; g /= a; b /= a; } else r = g = b = 0;
            if (o.linearLight) { r = resizeToSrgb(r); g = resizeToSrgb(g); b = resizeToSrgb(b); }
            const auto pixel = QRgba64::fromRgba64(u16(r), u16(g), u16(b), u16(a));
            if (out.depth() == 64) wide[x] = pixel;
            else target[x] = hasAlpha ? pixel.toArgb32() : pixel.toArgb32() | 0xff000000u;
        }
    }
    return out;
}
// The compact RGBA64 fallback above keeps wide rows/large shrinking kernels
// useful even when normalized rows would thrash the bounded cache.
constexpr int MaxResizeWorkers = 4;
constexpr int ResizeTileWidth = 8192;
constexpr int ResizeTileHeight = 256;
qint64 resizeSupportRows(QSize source, const ResizeOptions &o) {
    const double widen = o.fastDownscale ? 1 : std::max(1.0, double(source.height()) / o.size.height());
    return qint64(std::ceil(2 * radius(o.filter) * widen)) + 3;
}
int normalizedResizeTileWidth(QSize source, const ResizeOptions &o, int workers) {
    const qint64 fullRows = qint64(o.size.width()) * sizeof(std::array<double, 4>) * resizeSupportRows(source, o) * 2;
    return resizeRowCacheBudget() / workers >= fullRows ? o.size.width() : std::min(o.size.width(), ResizeTileWidth);
}
int normalizedResizeWorkers(QSize source, const ResizeOptions &o) {
    const qint64 rowBytes = qint64(std::min(o.size.width(), ResizeTileWidth)) * sizeof(std::array<double, 4>);
    if (rowBytes <= 0 || source.height() <= 0 || o.size.height() <= 0) return 0;
    const qint64 supportRows = resizeSupportRows(source, o);
    int count = qint64(o.size.width()) * o.size.height() >= 2 * 1024 * 1024
        ? std::clamp(QThread::idealThreadCount(), 1, MaxResizeWorkers) : 1;
    count = std::min(count, o.size.height());
    // Leave room for overlapping support, without changing the total cache cap.
    while (count && resizeRowCacheBudget() / count < rowBytes * supportRows * 2) --count;
    return count;
}
quint16 resizeU16(double value) {
    if (!(value > 0)) return 0;
    if (value >= 1) {
        // Near-zero alpha can amplify ringing past long's range. Retain the
        // legacy library's overflow behavior as well as its ordinary rounding.
        return value < (double(std::numeric_limits<long>::max()) / 65535.0 - 1) ? 65535 : u16(value);
    }
    const double scaled = value * 65535;
    const int rounded = int(scaled + .5);
    return quint16(rounded - int(scaled < rounded - .5));
}
quint8 resizeU8(double value) {
    if (!(value > 0)) return 0;
    if (value >= 1) return QRgba64::fromRgba64(resizeU16(value), 0, 0, 0).red8();
    const double scaled = value * 255;
    const int rounded = int(scaled + .5);
    // The 16-bit intermediate and Qt's /257 conversion have the same byte
    // thresholds. At floating-point ties, use the exact two-stage conversion.
    if (std::abs(scaled - (rounded - .5)) < 1e-9)
        return QRgba64::fromRgba64(resizeU16(value), 0, 0, 0).red8();
    return quint8(rounded);
}

using ResizePixel = std::array<double, 4>;
QRgb resizePixel32(double r, double g, double b, double a) {
#ifdef IX_RESIZE_SSE2
    const auto rg = _mm_set_pd(g, r), ba = _mm_set_pd(a, b);
    const auto overflow = _mm_set1_pd(double(std::numeric_limits<long>::max()) / 65535.0 - 1);
    // Preserve legacy overflow and exact two-stage quantization at byte ties.
    // SSE2 is part of the x64 baseline; other architectures use the scalar path.
    if (!_mm_movemask_pd(_mm_or_pd(_mm_cmpge_pd(rg, overflow), _mm_cmpge_pd(ba, overflow)))) {
        const auto zero = _mm_setzero_pd(), one = _mm_set1_pd(1), half = _mm_set1_pd(.5);
        const auto scale = _mm_set1_pd(255), sign = _mm_set1_pd(-0.0), epsilon = _mm_set1_pd(1e-9);
        const auto x = _mm_mul_pd(_mm_min_pd(_mm_max_pd(rg, zero), one), scale);
        const auto y = _mm_mul_pd(_mm_min_pd(_mm_max_pd(ba, zero), one), scale);
        const auto ix = _mm_cvttpd_epi32(_mm_add_pd(x, half)), iy = _mm_cvttpd_epi32(_mm_add_pd(y, half));
        const auto dx = _mm_andnot_pd(sign, _mm_sub_pd(x, _mm_sub_pd(_mm_cvtepi32_pd(ix), half)));
        const auto dy = _mm_andnot_pd(sign, _mm_sub_pd(y, _mm_sub_pd(_mm_cvtepi32_pd(iy), half)));
        if (!_mm_movemask_pd(_mm_or_pd(_mm_cmplt_pd(dx, epsilon), _mm_cmplt_pd(dy, epsilon)))) {
            const auto rgba = _mm_unpacklo_epi64(ix, iy);
            const auto bgra = _mm_shuffle_epi32(rgba, _MM_SHUFFLE(3, 0, 1, 2));
            const auto words = _mm_packs_epi32(bgra, _mm_setzero_si128());
            return QRgb(_mm_cvtsi128_si32(_mm_packus_epi16(words, _mm_setzero_si128())));
        }
    }
#endif
    return qRgba(resizeU8(r), resizeU8(g), resizeU8(b), resizeU8(a));
}
void writeNormalizedRow(uchar *destination, int width, const ResizePixel *sums,
                        bool wideOutput, bool hasAlpha, bool linearLight) {
    const auto *transfer = linearLight && !wideOutput ? linearOutputBytes() : nullptr;
    auto *target = reinterpret_cast<QRgb *>(destination);
    auto *wide = reinterpret_cast<QRgba64 *>(destination);
    for (int x = 0; x < width; ++x) {
        auto [r, g, b, a] = sums[x];
        if (a > 1e-10) { r /= a; g /= a; b /= a; } else r = g = b = 0;
        if (transfer) {
            target[x] = qRgba(transfer->convert(r), transfer->convert(g), transfer->convert(b), hasAlpha ? resizeU8(a) : 255);
            continue;
        }
        if (linearLight) { r = resizeToSrgb(r); g = resizeToSrgb(g); b = resizeToSrgb(b); }
        if (wideOutput)
            wide[x] = QRgba64::fromRgba64(resizeU16(r), resizeU16(g), resizeU16(b), resizeU16(a));
        else target[x] = resizePixel32(r, g, b, hasAlpha ? a : 1);
    }
}

template <int Bytes>
void copyNearestRow(uchar *destination, const uchar *source, const std::vector<int> &columns) {
    for (size_t x = 0; x < columns.size(); ++x)
        std::memcpy(destination + x * Bytes, source + size_t(columns[x]) * Bytes, Bytes);
}

class ResizeWorkerSlots {
  public:
    explicit ResizeWorkerSlots(int wanted) {
        int available = availableSlots().load();
        do { extra_ = std::min(std::max(0, wanted - 1), available); }
        while (!availableSlots().compare_exchange_weak(available, available - extra_));
    }
    ResizeWorkerSlots(const ResizeWorkerSlots &) = delete;
    ResizeWorkerSlots &operator=(const ResizeWorkerSlots &) = delete;
    ~ResizeWorkerSlots() { availableSlots().fetch_add(extra_); }
    int count() const { return extra_ + 1; }
  private:
    static std::atomic_int &availableSlots() {
        // Shared across simultaneous viewer and batch resize operations. The
        // calling worker always participates; no nested global-pool joins.
        static std::atomic_int available{std::clamp(QThread::idealThreadCount() - 1, 0, MaxResizeWorkers - 1)};
        return available;
    }
    int extra_ = 0;
};
template <typename Work>
void runResizeWorkers(int workers, std::atomic_bool &stopped, Work &&work) {
    std::exception_ptr failure;
    std::mutex failureMutex;
    const auto guarded = [&](int index) {
        try { work(index); }
        catch (...) {
            { std::lock_guard lock(failureMutex); if (!failure) failure = std::current_exception(); }
            stopped.store(true);
        }
    };
    {
        // Also supported by the libc++ in older Xcode versions. Join on every
        // exit, including thread-creation failure, before buffers are released.
        std::vector<std::thread> threads;
        const auto joined = qScopeGuard([&] {
            for (auto &thread : threads) if (thread.joinable()) thread.join();
        });
        threads.reserve(workers - 1);
        try {
            for (int i = 1; i < workers; ++i) threads.emplace_back(guarded, i);
        } catch (...) {
            stopped.store(true);
            throw;
        }
        guarded(0);
    }
    if (failure) std::rethrow_exception(failure);
}
QImage resampleNearest(const QImage &src, const ResizeOptions &o, const std::atomic_bool *cancel) {
    if (cancel && cancel->load()) throw std::runtime_error("Abgebrochen.");
    QImage out = allocateImage(o.size, src.format(), (imageIsFileBacked(src) ? 0 : src.sizeInBytes()) + qint64(o.size.width()) * sizeof(int));
    if (out.isNull()) throw std::runtime_error("Nicht genügend Speicher.");
    out.setColorTable(src.colorTable());
    const int bytes = src.depth() / 8;
    std::vector<int> columns(out.width());
    for (int x = 0; x < out.width(); ++x)
        columns[x] = int((qint64(2 * x + 1) * src.width()) / (2 * out.width()));
    auto *destination = out.bits();
    const qsizetype stride = out.bytesPerLine();
    const qsizetype rowBytes = (qsizetype(out.width()) * out.depth() + 7) / 8;
    // Packed monochrome uses QImage's mutable pixel API and remains serial.
    // All byte-addressable formats write disjoint rows of a unique raw buffer.
    // Mapped bulk copies are dominated by page faults/storage. Multiple writers
    // increased working set and CPU time in proportional 100k probes; keep one.
    const int wanted = bytes && out.sizeInBytes() >= 8 * MiB && !imageIsFileBacked(out)
        ? std::min(MaxResizeWorkers, out.height()) : 1;
    ResizeWorkerSlots reservation(wanted);
    const int workers = reservation.count();
    const int blockHeight = workers == 1 ? out.height() : ResizeTileHeight;
    std::atomic_int nextBlock{0};
    std::atomic_bool stopped{false};
    runResizeWorkers(workers, stopped, [&](int) {
        for (;;) {
            const int first = nextBlock.fetch_add(blockHeight);
            const int last = std::min(out.height(), first + blockHeight);
            if (first >= last || stopped.load()) return;
            int previousY = -1;
            for (int y = first; y < last; ++y) {
                if (cancel && cancel->load()) stopped.store(true);
                if (stopped.load()) return;
                const int sourceY = int((qint64(2 * y + 1) * src.height()) / (2 * out.height()));
                auto *target = destination + qsizetype(y) * stride;
                if (sourceY == previousY) {
                    std::memcpy(target, target - stride, size_t(rowBytes));
                    continue;
                }
                previousY = sourceY;
                const auto *source = src.constScanLine(sourceY);
                switch (bytes) {
                case 1: copyNearestRow<1>(target, source, columns); break;
                case 2: copyNearestRow<2>(target, source, columns); break;
                case 3: copyNearestRow<3>(target, source, columns); break;
                case 4: copyNearestRow<4>(target, source, columns); break;
                case 8: copyNearestRow<8>(target, source, columns); break;
                case 16: copyNearestRow<16>(target, source, columns); break;
                default:
                    for (int x = 0; x < out.width(); ++x) if (bytes)
                        std::memcpy(target + x * bytes, source + columns[x] * bytes, bytes);
                    else
                        out.setPixel(x, y, src.pixelIndex(columns[x], sourceY));
                    break;
                }
            }
        }
    });
    if (stopped.load()) throw std::runtime_error("Abgebrochen.");
    return out;
}
QImage resampleNormalizedRows(const QImage &src, const ResizeOptions &o, const std::atomic_bool *cancel,
                              int plannedWorkers) {
    if (cancel && cancel->load()) return {};
    const auto wx = weights(src.width(), o.size.width(), o.filter, o.fastDownscale);
    const auto wy = weights(src.height(), o.size.height(), o.filter, o.fastDownscale);
    const bool hasAlpha = src.hasAlphaChannel();
    const bool wideOutput = channelBits(src.format()) > 8;
    const auto *linearInput = o.linearLight ? linearInputValues() : nullptr;
    const auto format = wideOutput ? QImage::Format_RGBA64 : hasAlpha ? QImage::Format_ARGB32 : QImage::Format_RGB32;
    const qint64 resident = estimatedResizeBytes(src.size(), src.format(), o, src.sizeInBytes()) -
        imageBufferBytes(o.size, format) - (imageIsFileBacked(src) ? src.sizeInBytes() : 0);
    QImage out = allocateImage(o.size, format, resident);
    // QImage's writable accessors can detach/change its cache key. Obtain the
    // unique mutable buffer once, before any worker writes disjoint row ranges.
    auto *destination = out.bits();
    const qsizetype stride = out.bytesPerLine();
    const auto palette = src.colorTable();
    ResizeWorkerSlots reservation(plannedWorkers);
    const int workers = reservation.count();
    const qint64 cacheBudget = resizeRowCacheBudget() / workers;
    // Keep the planned tile width when another operation holds thread slots;
    // using fewer workers must not increase the estimated scratch allocation.
    const int tileWidth = normalizedResizeTileWidth(src.size(), o, plannedWorkers);
    std::atomic_bool stopped{false};
    std::atomic_int nextBlock{0};
    const int blockHeight = std::min(ResizeTileHeight, (o.size.height() + workers - 1) / workers);
    using Pixel = ResizePixel;
    using Row = QVector<Pixel>;
    runResizeWorkers(workers, stopped, [&](int index) {
        QCache<int, Row> rows{int(cacheBudget)};
        Row input(src.width()), sums(tileWidth);
        const int first = int(qint64(o.size.height()) * index / workers);
        const int end = int(qint64(o.size.height()) * (index + 1) / workers);
        auto cancelled = [&] {
            if (cancel && cancel->load()) stopped.store(true);
            return stopped.load();
        };
        // Wide mapped outputs advance through neighboring stripes, so
        // workers do not scatter dirty pages over multi-GB row bands.
        // Keep full-width row bands for the ordinary in-cache path.
        for (;;) {
            const int block = tileWidth == o.size.width() ? first : nextBlock.fetch_add(blockHeight);
            const int last = tileWidth == o.size.width() ? end : std::min(o.size.height(), block + blockHeight);
            if (block >= last || cancelled()) break;
            for (int left = 0; left < o.size.width(); left += tileWidth) {
                const int width = std::min(tileWidth, o.size.width() - left);
                rows.clear();
                for (int y = block; y < last; ++y) {
                    if (cancelled()) return;
                    std::fill_n(sums.data(), width, Pixel{});
                    for (const auto vertical : wy[y]) {
                        if (cancelled()) return;
                        Row horizontal;
                        if (const auto *cached = rows.object(vertical.index)) horizontal = *cached;
                        else {
                            QImage view(src.constScanLine(vertical.index), src.width(), 1, src.bytesPerLine(), src.format());
                            if (!palette.isEmpty()) view.setColorTable(palette);
                            const auto converted = view.convertToFormat(QImage::Format_RGBA64);
                            if (converted.isNull()) throw std::runtime_error("Nicht genügend Speicher.");
                            const auto *pixels = reinterpret_cast<const QRgba64 *>(converted.constBits());
                            auto *normalized = input.data();
                            for (int x = 0; x < src.width(); ++x) {
                                const auto pixel = pixels[x];
                                const double alpha = pixel.alpha() / 65535.0;
                                auto cv = [&](quint16 value) { return linearInput ? linearInput[value] : value / 65535.0; };
                                normalized[x] = {cv(pixel.red()) * alpha, cv(pixel.green()) * alpha,
                                                 cv(pixel.blue()) * alpha, alpha};
                            }
                            horizontal.resize(width);
                            auto *filtered = horizontal.data();
                            for (int x = 0; x < width; ++x) {
                                Pixel sum{};
                                for (const auto tap : wx[left + x])
                                    for (int c = 0; c < 4; ++c) sum[c] += normalized[tap.index][c] * tap.weight;
                                // Quantize at the same horizontal boundary as the
                                // legacy RGBA64 pass, then normalize once for reuse.
                                for (int c = 0; c < 4; ++c) filtered[x][c] = resizeU16(sum[c]) / 65535.0;
                            }
                            rows.insert(vertical.index, new Row(horizontal), int(qint64(horizontal.size()) * sizeof(Pixel)));
                        }
                        const auto *row = horizontal.constData();
                        auto *accumulated = sums.data();
                        for (int x = 0; x < width; ++x)
                            for (int c = 0; c < 4; ++c) accumulated[x][c] += row[x][c] * vertical.weight;
                    }
                    writeNormalizedRow(destination + qsizetype(y) * stride + qsizetype(left) * (wideOutput ? 8 : 4),
                        width, sums.constData(), wideOutput, hasAlpha, o.linearLight);
                }
            }
            if (tileWidth == o.size.width()) break;
        }
    });
    return stopped.load() ? QImage() : out;
}
} // namespace
qint64 estimatedResizeBytes(QSize source, QImage::Format format, const ResizeOptions &o, qint64 sourceBytes) {
    if (source.isEmpty() || o.size.isEmpty() || source.width() > ImageAxisLimit || source.height() > ImageAxisLimit ||
        o.size.width() > ImageAxisLimit || o.size.height() > ImageAxisLimit || format <= QImage::Format_Invalid || format >= QImage::NImageFormats)
        return std::numeric_limits<qint64>::max();
    const int depth = QImage::toPixelFormat(format).bitsPerPixel();
    if (sourceBytes <= 0) sourceBytes = ((qint64(source.width()) * depth + 31) / 32) * 4 * source.height();
    const qint64 pixels = qint64(o.size.width()) * o.size.height();
    qint64 result = sourceBytes;
    if (o.filter == ResampleFilter::Nearest)
        return result + ((qint64(o.size.width()) * depth + 31) / 32) * 4 * o.size.height() + qint64(o.size.width()) * sizeof(int);
    // Six-tap Lanczos is the widest kernel. Includes outer row containers,
    // edge taps, row conversion, accumulation and the bounded row cache.
    const qint64 taps = 128 * (qint64(std::max(source.width(),o.size.width())) + std::max(source.height(),o.size.height())) +
        64 * (qint64(o.size.width()) + o.size.height());
    const int workers = normalizedResizeWorkers(source, o);
    const qint64 rowWork = workers
        ? qint64(workers) * (qint64(source.width()) * 48 + qint64(normalizedResizeTileWidth(source, o, workers)) * 64)
        : qint64(source.width()) * 12 + qint64(o.size.width()) * 48;
    result += pixels * (channelBits(format) <= 8 ? 4 : 8) + rowWork + resizeRowCacheBudget() + taps;
    if (o.linearLight) result += 65536 * qint64(sizeof(double)) + sizeof(detail::ResizeByteTransfer);
    if (o.sharpen) result = std::max(result, sourceBytes + pixels * (channelBits(format) <= 8 ? 24 : 16));
    return result;
}
bool resizeFitsBudget(QSize source, QImage::Format format, const ResizeOptions &o, qint64 sourceBytes, bool mappedSource) {
    if (format <= QImage::Format_Invalid || format >= QImage::NImageFormats) return false;
    const auto targetFormat = o.filter == ResampleFilter::Nearest ? format : channelBits(format) > 8
        ? QImage::Format_RGBA64 : QImage::Format_ARGB32;
    const auto bytes = imageBufferBytes(o.size,targetFormat);
    const auto estimate = estimatedResizeBytes(source,format,o,sourceBytes);
    if (bytes <= 0 || estimate == std::numeric_limits<qint64>::max()) return false;
    const auto resident = estimate - bytes - (mappedSource ? sourceBytes : 0);
    // Sharpen still requires full-frame working images. It remains RAM guarded.
    if (o.sharpen && o.filter != ResampleFilter::Nearest) return estimate <= imageMemoryLimit();
    return canAllocateImage(o.size,targetFormat,resident);
}
QSize maximumResizeSize(QSize source, QImage::Format format, const ResizeOptions &o, qint64 sourceBytes, bool mappedSource) {
    if (o.size.isEmpty()) return {};
    const auto aspect = o.size;
    int low = 1, high = ImageAxisLimit; QSize best;
    while (low <= high) {
        const int longest = low + (high-low)/2;
        ResizeOptions candidate = o;
        candidate.size = aspect.width() >= aspect.height()
            ? QSize(longest, std::max(1, int(qint64(longest) * aspect.height() / aspect.width())))
            : QSize(std::max(1, int(qint64(longest) * aspect.width() / aspect.height())), longest);
        if (resizeFitsBudget(source,format,candidate,sourceBytes,mappedSource)) {
            best = candidate.size; low = longest + 1;
        } else high = longest - 1;
    }
    return best;
}
QString resizeSizeLimitError(QSize source, QImage::Format format, const ResizeOptions &o, qint64 sourceBytes, bool mappedSource) {
    const auto maximum = maximumResizeSize(source, format, o, sourceBytes, mappedSource);
    return IX_TEXT("Für dieses Seitenverhältnis sind höchstens %1 × %2 Pixel möglich (%3 MiB RAM; Auslagerungsbudget berücksichtigt).")
        .arg(std::max(0, maximum.width())).arg(std::max(0, maximum.height())).arg(imageMemoryLimit() / MiB);
}
QImage resample(const QImage &src, const ResizeOptions &o, const std::atomic_bool *cancel) {
    if (src.isNull())
        return {};
    const double density = o.dpi / .0254;
    if (!std::isfinite(density) || density < .5 ||
        density > double(std::numeric_limits<int>::max()) - .5)
        throw std::runtime_error(IX_TEXT("Die Auflösung kann nicht als gültige Bildmetadaten gespeichert werden.").toStdString());
    const auto sizeError = [&] { return resizeSizeLimitError(src.size(),src.format(),o,src.sizeInBytes(),imageIsFileBacked(src)).toStdString(); };
    if (!resizeFitsBudget(src.size(),src.format(),o,src.sizeInBytes(),imageIsFileBacked(src)))
        throw std::runtime_error(sizeError());
    QImage out;
    if (o.filter == ResampleFilter::Nearest) {
        out = resampleNearest(src, o, cancel);
    } else {
        const int workers = normalizedResizeWorkers(src.size(), o);
        out = workers ? resampleNormalizedRows(src, o, cancel, workers) : resampleRows(src, o, cancel);
        if (out.isNull()) return {};
    }
    inheritImageProperties(src, out);
    out.setDotsPerMeterX(int(std::round(density)));
    out.setDotsPerMeterY(int(std::round(density)));
    if (o.sharpen && o.filter != ResampleFilter::Nearest)
        out = convolve(out, {0, -1, 0, -1, 5, -1, 0, -1, 0}, 3);
    return out;
}
} // namespace ix
