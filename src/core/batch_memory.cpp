// SPDX-License-Identifier: GPL-3.0-or-later
#include "batch_memory.h"
#include "batch_pipeline.h"
#include "processing.h"
#include "jpegli_encoder.h"
#include "save_formats.h"
#include <QScopeGuard>
#include <algorithm>
#include <chrono>
namespace ix {
qint64 estimatedPipelineMemory(QSize size, QImage::Format format,
                               const PipelineProfile &profile, qint64 limit) {
    limit = std::max<qint64>(1, limit);
    if (size.isEmpty() || size.width() > ImageAxisLimit || size.height() > ImageAxisLimit ||
        format <= QImage::Format_Invalid || format >= QImage::NImageFormats) return limit;
    const auto bufferBytes = [](QSize dimensions, QImage::Format pixelFormat) {
        return ((qint64(dimensions.width()) * QImage::toPixelFormat(pixelFormat).bitsPerPixel() + 31) / 32) *
            4 * dimensions.height();
    };
    auto bytes = bufferBytes(size, format);
    qint64 peak = std::min(limit, bytes * 2); // Decoded pixels and a decode/orientation copy.
    for (const auto &step : profile.steps) {
        const auto &p = step.parameters;
        if (step.type == QStringLiteral("resize")) {
            ResizeOptions options;
            options.size = QSize(p.value(QStringLiteral("width")).toInt(), p.value(QStringLiteral("height")).toInt());
            if (options.size.isEmpty() || options.size.width() > ImageAxisLimit || options.size.height() > ImageAxisLimit)
                return limit;
            if (p.value(QStringLiteral("proportional")).toBool(true)) options.size = size.scaled(options.size, Qt::KeepAspectRatio);
            if (options.size.isEmpty()) return limit;
            options.filter = ResampleFilter(std::clamp(p.value(QStringLiteral("filter")).toInt(5), 0, 6));
            options.fastDownscale = p.value(QStringLiteral("fast")).toBool(true);
            peak = std::max(peak, std::min(limit, estimatedResizeBytes(size, format, options, bytes)));
            if (options.filter != ResampleFilter::Nearest) {
                const auto pixels = QImage::toPixelFormat(format);
                const auto channelBits = std::max({pixels.redSize(), pixels.greenSize(), pixels.blueSize(), pixels.alphaSize()});
                format = channelBits > 8 ? QImage::Format_RGBA64 : QImage::Format_ARGB32;
            }
            size = options.size;
            bytes = bufferBytes(size, format);
        } else {
            if (step.type == QStringLiteral("export") &&
                canonicalSaveFormat(p.value(QStringLiteral("format")).toString().toLatin1()) == "jpeg")
                peak = std::max(peak, std::min(limit, bytes + jpegliWorkingBytes(size.width())));
            // Full-frame filtering, painting or export conversion can retain
            // source, normalized 16-bit pixels and destination simultaneously.
            peak = std::max(peak, std::min(limit, bytes + qint64(size.width()) * size.height() * 24));
            if (step.type != QStringLiteral("export")) {
                // Painting can retain 128-bit floating-point pixels. Do not
                // lower their estimate before a subsequent nearest resize.
                if (QImage::toPixelFormat(format).bitsPerPixel() <= 64) format = QImage::Format_RGBA64;
                bytes = bufferBytes(size, format);
            }
        }
    }
    return peak >= limit - std::min(limit, 16 * MiB) ? limit : peak + 16 * MiB;
}
BatchMemoryGate::BatchMemoryGate(qint64 limit, std::function<qint64()> retained)
    : limit_(std::max<qint64>(1, limit)), retained_(std::move(retained)) {}
BatchMemoryGate::Lease::~Lease() { if (owner_) owner_->release(bytes_); }
BatchMemoryGate::Lease BatchMemoryGate::acquire(qint64 bytes, const std::atomic_bool &cancel) {
    bytes = std::clamp<qint64>(bytes, 1, limit_);
    std::unique_lock lock(mutex_);
    if (cancel.load()) return {};
    waiting_.push_back(&bytes);
    const auto removeRequest = qScopeGuard([&] {
        const auto request = std::find(waiting_.begin(), waiting_.end(), &bytes);
        if (request != waiting_.end()) waiting_.erase(request);
        changed_.notify_all();
    });
    while (!cancel.load()) {
        const auto capacity = limit_ - (retained_ ? std::clamp<qint64>(retained_(), 0, limit_ - 1) : 0);
        // Existing work is never preempted. An oversized sole job may proceed
        // using the existing per-operation/disk-backed guards rather than hang.
        if (waiting_.front() == &bytes && (used_ == 0 || bytes <= capacity - used_)) {
            waiting_.pop_front();
            used_ += bytes;
            changed_.notify_all();
            return Lease(this, bytes);
        }
        changed_.wait_for(lock, std::chrono::milliseconds(20));
    }
    return {};
}
void BatchMemoryGate::release(qint64 bytes) {
    std::lock_guard lock(mutex_);
    used_ -= bytes;
    changed_.notify_all();
}
} // namespace ix
