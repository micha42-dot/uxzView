// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QScopeGuard>
#include <QProgressDialog>
#include <QTimer>
#include <QThread>
#include <QtConcurrent>
#include <type_traits>
#include <utility>
namespace ix {
// GUI-thread join: keep paints, timers and user input alive while joining.
// Reentrancy is blocked by DocumentActivity (commands return early), by the
// caller's modal progress window, and by closing checks (closeEvent is ignored
// while a transaction owns the document). Stack-owned captures therefore stay
// valid without suppressing input, and a cancellable caller can act on clicks.
// Blocking OS/codec work still must finish before stack-owned state is released.
template <class Result> void waitForGuiWorker(QFutureWatcher<Result> &watcher) {
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    if (!watcher.isFinished()) {
        QElapsedTimer watch;
        watch.start();
        loop.exec();
        // Intermittent stalls are otherwise invisible in user reports; log the
        // join duration without changing the documented join semantics.
        if (watch.elapsed() > 2000)
            qWarning("ix-worker join took %lld ms", static_cast<long long>(watch.elapsed()));
    }
    // Application shutdown may end nested loops before a worker finishes.
    if (!watcher.isFinished()) watcher.waitForFinished();
}

// Fast transactions need no progress window, so it appears only after 200 ms.
// Join the future's loop rather than a dialog loop: completion during show()
// must not be lost before QDialog::exec() has started its nested event loop.
// `cancellable` marks callers that wire the dialog's cancel signal to their own
// flag. Such a window stays hidden once dismissed; a pure progress window
// reappears, because a blocked main window must never look idle.
template <class Result>
void waitForGuiWorker(QFutureWatcher<Result> &watcher, QProgressDialog &progress,
                      bool cancellable = false) {
    QTimer delayedShow;
    delayedShow.setSingleShot(true);
    QObject::connect(&delayedShow, &QTimer::timeout, &progress, [&] {
        if (!watcher.isFinished()) progress.show();
    });
    QTimer keepVisible;
    if (!cancellable) {
        QObject::connect(&keepVisible, &QTimer::timeout, &progress, [&] {
            if (!watcher.isFinished() && !progress.isVisible()) progress.show();
        });
        keepVisible.start(250);
    }
    delayedShow.start(200);
    waitForGuiWorker(watcher);
    keepVisible.stop();
    delayedShow.stop();
    progress.hide();
}
template <class Work> auto runGuiWorker(Work work) {
    QApplication::setOverrideCursor(Qt::BusyCursor);
    const auto cursor = qScopeGuard([] { QApplication::restoreOverrideCursor(); });
    QFutureWatcher<std::invoke_result_t<Work>> watcher;
    watcher.setFuture(QtConcurrent::run(std::move(work)));
    waitForGuiWorker(watcher);
    return watcher.future().takeResult();
}
} // namespace ix
