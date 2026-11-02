// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QApplication>
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
// GUI-thread join: keep paints/timers alive while suppressing reentrant input.
// Blocking OS/codec work still must finish before stack-owned state is released.
template <class Result> void waitForGuiWorker(QFutureWatcher<Result> &watcher) {
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    if (!watcher.isFinished()) loop.exec(QEventLoop::ExcludeUserInputEvents);
    // Application shutdown may end nested loops before a worker finishes.
    if (!watcher.isFinished()) watcher.waitForFinished();
}

// Non-cancellable transactions only. Fast saves/moves need no progress window.
// Join the future's loop rather than a dialog loop: completion during show()
// must not be lost before QDialog::exec() has started its nested event loop.
template <class Result> void waitForGuiWorker(QFutureWatcher<Result> &watcher, QProgressDialog &progress) {
    QTimer delayedShow;
    delayedShow.setSingleShot(true);
    QObject::connect(&delayedShow, &QTimer::timeout, &progress, [&] {
        if (!watcher.isFinished()) progress.show();
    });
    delayedShow.start(200);
    waitForGuiWorker(watcher);
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
