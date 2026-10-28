// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
namespace ix {
// Workers publish monotonically; the GUI owns at most one queued notification.
// Clear before reading so a concurrent publication either reaches this delivery
// or schedules the next one. Each run has its own mailbox.
class BatchProgress {
  public:
    explicit BatchProgress(int total) : total_(total) {}
    bool publish(int value) {
        value = std::clamp(value, 0, total_);
        auto previous = completed_.load();
        while (value > previous) {
            if (completed_.compare_exchange_weak(previous, value))
                return !queued_.exchange(true);
        }
        return false;
    }
    int take() {
        queued_.store(false);
        return completed_.load();
    }
    int total() const { return total_; }
  private:
    const int total_;
    std::atomic_int completed_{0};
    std::atomic_bool queued_{false};
};
} // namespace ix
