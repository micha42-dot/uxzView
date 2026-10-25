// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
#include <QTimer>
namespace ix {
class SlideshowController {
  public:
    struct Next {
        QString path;
        bool finished = false;
    };
    QTimer &timer() {
        return timer_;
    }
    bool active() const {
        return timer_.isActive();
    }
    void setInterval(int milliseconds) {
        timer_.setInterval(qBound(100, milliseconds, 3600000));
    }
    void start() {
        roundStarted_ = false;
        remaining_.clear();
        roundFiles_.clear();
        timer_.start();
    }
    void stop() {
        timer_.stop();
    }
    Next next(const QStringList &files, int current, bool random, bool repeat);

  private:
    QTimer timer_;
    bool roundStarted_ = false;
    QStringList remaining_, roundFiles_;
};
} // namespace ix
