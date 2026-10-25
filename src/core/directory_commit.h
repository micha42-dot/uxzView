// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <atomic>

namespace ix::detail {
// Publish a completed staging directory without replacing an existing target.
// Windows scanners/file handles can briefly deny the rename after files close.
// Normal commits have no delay; retries are bounded and remain cancellable.
inline bool commitDirectory(const QString &source, const QString &target, const std::atomic_bool &cancel) {
    for (int attempt = 0; ; ++attempt) {
        if (cancel.load())
            return false;
        if (QDir().rename(source, target))
            return true;
#ifdef Q_OS_WIN
        if (attempt >= 8 || QFileInfo::exists(target) || !QFileInfo::exists(source))
            return false;
        QThread::msleep(25);
#else
        return false;
#endif
    }
}
} // namespace ix::detail
