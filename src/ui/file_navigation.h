// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDir>
#include <QStringList>
namespace ix {
// Folder snapshots may lag behind the current image. Never reopen the removed
// path, even when its position is unknown or duplicated in an older snapshot.
inline QString nextPathAfterRemoval(const QStringList &files, qsizetype index, const QString &source) {
    const auto normalized = QDir::cleanPath(source);
    const auto matches = [&](const QString &path) {
#ifdef Q_OS_WIN
        return QDir::cleanPath(path).compare(normalized, Qt::CaseInsensitive) == 0;
#else
        return QDir::cleanPath(path) == normalized;
#endif
    };
    if (index < 0 || index >= files.size() || !matches(files.at(index))) {
        index = -1;
        for (qsizetype i = 0; i < files.size(); ++i)
            if (matches(files.at(i))) { index = i; break; }
    }
    for (qsizetype visited = 0; visited < files.size(); ++visited) {
        index = index + 1 == files.size() ? 0 : index + 1;
        if (!matches(files.at(index))) return files.at(index);
    }
    return {};
}
// The entry before the removed file. Deliberately without wrap: when the removed
// file was the first entry there is nothing before it, and ordinary navigation
// (which knows the wrap preference) takes over.
inline QString previousPathAfterRemoval(const QStringList &files, qsizetype index, const QString &source) {
    const auto normalized = QDir::cleanPath(source);
    const auto matches = [&](const QString &path) {
#ifdef Q_OS_WIN
        return QDir::cleanPath(path).compare(normalized, Qt::CaseInsensitive) == 0;
#else
        return QDir::cleanPath(path) == normalized;
#endif
    };
    if (index < 0 || index >= files.size() || !matches(files.at(index))) {
        index = -1;
        for (qsizetype i = 0; i < files.size(); ++i)
            if (matches(files.at(i))) { index = i; break; }
    }
    for (qsizetype i = index - 1; i >= 0; --i)
        if (!matches(files.at(i))) return files.at(i);
    return {};
}
// StayEmpty keeps the deliberate black state of a deletion (the default) while
// OpenNext restores the previous behaviour of opening the following image.
enum class AfterDelete { OpenNext = 0, StayEmpty = 1 };
// Clamp hand-edited values so an out-of-range setting cannot silently flip the
// default behaviour; unknown values fall back to staying empty.
inline AfterDelete afterDeleteMode(int value) {
    return value == int(AfterDelete::OpenNext) ? AfterDelete::OpenNext : AfterDelete::StayEmpty;
}
inline bool afterDeleteStaysEmpty(int value) { return afterDeleteMode(value) == AfterDelete::StayEmpty; }
} // namespace ix
