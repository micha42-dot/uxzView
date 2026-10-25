// SPDX-License-Identifier: GPL-3.0-or-later
#include "slideshow_controller.h"
#include <QRandomGenerator>
#include <QSet>
namespace ix {
SlideshowController::Next SlideshowController::next(const QStringList &files, int current, bool random,
                                                    bool repeat) {
    if (files.isEmpty())
        return {{}, true};
    if (!random) {
        if (current + 1 >= files.size())
            return repeat ? Next{files.first()} : Next{{}, true};
        return {files[std::max(0, current + 1)]};
    }
    const auto beginRound = [&] {
        remaining_ = files;
        if (current >= 0 && current < files.size())
            remaining_.removeAll(files[current]);
        // Shuffle once, then pop in constant time. Removing a random element
        // and reconstructing the complete membership set on every tick made
        // an unchanged directory need quadratic work over a full round.
        for (qsizetype i = remaining_.size() - 1; i > 0; --i)
            remaining_.swapItemsAt(i, QRandomGenerator::global()->bounded(int(i + 1)));
        roundFiles_ = files;
        roundStarted_ = true;
    };
    if (!roundStarted_ || (remaining_.isEmpty() && repeat)) {
        beginRound();
    } else if (!roundFiles_.isSharedWith(files)) {
        // A fresh directory snapshot can remove files during a round. New
        // arrivals join the following round, as before.
        const QSet<QString> available(files.begin(), files.end());
        remaining_.removeIf([&](const QString &path) { return !available.contains(path); });
        roundFiles_ = files;
    }
    // Manual browsing can display an image still queued in this round.
    // Skip it without rebuilding/filtering the whole remaining list per tick.
    for (int attempt = 0; attempt < 2; ++attempt) {
        while (!remaining_.isEmpty()) {
            const auto path = remaining_.takeLast();
            if (current < 0 || current >= files.size() || path != files.at(current))
                return {path};
        }
        if (!repeat)
            return {{}, true};
        if (attempt == 0)
            beginRound();
    }
    return {};
}
} // namespace ix
