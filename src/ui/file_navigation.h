// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDir>
#include <QStringList>
#include "folder_traversal.h"
#include <algorithm>
namespace ix {
// Folder snapshots may lag behind the current image. Never reopen the removed
// path, even when its position is unknown or duplicated in an older snapshot.
inline QString nextPathAfterRemoval(const QStringList &files, qsizetype index, const QString &source, bool wrap = true) {
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
        if (!wrap && index + 1 >= files.size()) return {};
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
// Neighbour anchors locate the removed slot in a fresh listing. Traversal owns
// unreadable-file retries and wrapping exactly as it does for ordinary arrows.
inline std::optional<FolderTraversal> resumeAfterRemoval(const QStringList &files,
        const QString &source, const QString &next, const QString &previous,
        qsizetype oldIndex, int direction, bool wrap) {
    if (files.isEmpty() || direction == 0) return {};
    auto slot = next.isEmpty() ? qsizetype(-1) : files.indexOf(next);
    if (slot < 0 && !previous.isEmpty()) {
        const auto before = files.indexOf(previous);
        if (before >= 0) slot = before + 1;
    }
    if (slot < 0) slot = std::clamp<qsizetype>(oldIndex, 0, files.size());
    return FolderTraversal::start(files, direction > 0 ? slot - 1 : slot,
                                 direction > 0 ? 1 : -1, wrap, source);
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
// Double-click on the image: enter/exit fullscreen (default) or zoom in steps
// around the cursor, toggling back to the pre-zoom state on the next click.
enum class DoubleClickAction { Fullscreen = 0, Zoom = 1 };
inline DoubleClickAction doubleClickMode(int value) {
    return value == int(DoubleClickAction::Zoom) ? DoubleClickAction::Zoom
                                                 : DoubleClickAction::Fullscreen;
}
// Arrow-key navigation: horizontal arrows (default), vertical arrows, or both.
// Up/Down only navigates when no scrollbar can consume the key.
enum class ArrowNavigation { LeftRight = 0, UpDown = 1, Both = 2 };
inline ArrowNavigation arrowNavigationMode(int value) {
    return value < 0 || value > 2 ? ArrowNavigation::LeftRight : ArrowNavigation(value);
}
inline bool arrowNavigatesHorizontally(int value) {
    const auto mode = arrowNavigationMode(value);
    return mode == ArrowNavigation::LeftRight || mode == ArrowNavigation::Both;
}
inline bool arrowNavigatesVertically(int value) {
    const auto mode = arrowNavigationMode(value);
    return mode == ArrowNavigation::UpDown || mode == ArrowNavigation::Both;
}
} // namespace ix
