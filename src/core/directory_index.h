// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QHash>
#include <QStringList>
#include <atomic>
namespace ix {
struct DirectorySnapshot {
    QString directory, latest;
    QStringList files;
    QHash<QString, int> positions;
    qint64 elapsedMs = 0;
};
DirectorySnapshot scanDirectory(const QString &directory, const QStringList &extensions, int sorting,
                                bool reverse, const std::atomic_bool *cancel = nullptr,
                                bool findLatest = true);
} // namespace ix
