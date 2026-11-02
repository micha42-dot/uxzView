// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>
namespace ix {
struct PipelineProfile;
// Admission estimates, not a process RSS limit. Unknown/oversized jobs run alone;
// existing per-operation and disk-backed allocation guards remain authoritative.
qint64 estimatedPipelineMemory(QSize size, QImage::Format format,
                               const PipelineProfile &profile, qint64 limit);
class BatchMemoryGate {
  public:
    class Lease {
      public:
        Lease() = default;
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        Lease(Lease &&other) noexcept : owner_(std::exchange(other.owner_, nullptr)), bytes_(other.bytes_) {}
        ~Lease();
        explicit operator bool() const { return owner_ != nullptr; }
      private:
        friend class BatchMemoryGate;
        Lease(BatchMemoryGate *owner, qint64 bytes) : owner_(owner), bytes_(bytes) {}
        BatchMemoryGate *owner_ = nullptr;
        qint64 bytes_ = 0;
    };
    explicit BatchMemoryGate(qint64 limit);
    // Only acquire once per job, before decoding; never while holding a lease.
    // Cancellation is polled while waiting and removes the request from the queue.
    Lease acquire(qint64 bytes, const std::atomic_bool &cancel);
    BatchMemoryGate(const BatchMemoryGate &) = delete;
    BatchMemoryGate &operator=(const BatchMemoryGate &) = delete;
  private:
    void release(qint64 bytes);
    const qint64 limit_;
    qint64 used_ = 0;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<const qint64 *> waiting_;
};
} // namespace ix
