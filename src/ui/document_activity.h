// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace ix {
// Owned by the GUI thread. File dialogs and delayed captures hold an exclusive
// phase across nested event loops; workers cannot replace their document.
class DocumentActivity {
  public:
    enum class Phase { Idle, Loading, Editing, Saving, Files, Capture };
    bool busy() const {
        return phase_ != Phase::Idle;
    }
    bool workerActive() const {
        return phase_ == Phase::Loading || phase_ == Phase::Editing;
    }
    bool begin(Phase phase) {
        if (busy())
            return false;
        phase_ = phase;
        return true;
    }
    void finish() {
        phase_ = Phase::Idle;
    }

  private:
    Phase phase_ = Phase::Idle;
};
} // namespace ix
