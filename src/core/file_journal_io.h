// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFile>
#include <QFileInfo>
namespace ix::journal_detail {
inline bool occupied(const QString &path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}
inline bool regular(const QString &path) {
    const QFileInfo info(path);
    return info.isFile() && !info.isSymLink();
}
inline bool moveFile(const QString &from, const QString &to) {
    if (occupied(to)) return false;
    if (QFile::rename(from, to)) return true;
    if (!QFile::copy(from, to)) return false;
    if (QFile::remove(from)) return true;
    QFile::remove(to);
    return false;
}
} // namespace ix::journal_detail
