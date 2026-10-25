// SPDX-License-Identifier: GPL-3.0-or-later
#include "directory_index.h"
#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QDirListing>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocale>
#include <algorithm>
#include <limits>
#include <vector>
namespace ix {
DirectorySnapshot scanDirectory(const QString &path, const QStringList &extensions, int sorting, bool reverse,
                                const std::atomic_bool *cancel, bool findLatest) {
    QElapsedTimer timer;
    timer.start();
    DirectorySnapshot result;
    result.directory = QDir(path).absolutePath();
    QStringList filters;
    for (const auto &ext : extensions)
        filters.append(QStringLiteral("*.") + ext);
    if (cancel && cancel->load())
        return result;
    QLocale locale;
    if (locale.language() == QLocale::C)
        locale = QLocale(QLocale::German, QLocale::Germany);
    QCollator collator(locale);
    collator.setNumericMode(sorting == 0);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    struct Entry {
        QString name, path;
        QCollatorSortKey key;
        qint64 stamp, bytes;
    };
    std::vector<Entry> prepared;
    qint64 newest = std::numeric_limits<qint64>::min();
    // Enumerate names/types directly. Probing permissions for every entry can
    // take seconds on Windows; the decoder checks access when a file is opened.
    const auto flags = QDirListing::IteratorFlag::FilesOnly | QDirListing::IteratorFlag::ResolveSymlinks;
    for (const auto &entry : QDirListing(result.directory, filters, flags)) {
        if (cancel && cancel->load())
            return {};
        const auto name = entry.fileName(), absolute = entry.filePath();
        const auto stamp = (findLatest || sorting == 2) ? entry.fileInfo().lastModified().toMSecsSinceEpoch() : 0;
        if (findLatest && stamp > newest) {
            newest = stamp;
            result.latest = absolute;
        }
        prepared.push_back({name, absolute, collator.sortKey(name), stamp, sorting == 3 ? entry.size() : 0});
    }
    if (cancel && cancel->load())
        return {};
    std::sort(prepared.begin(), prepared.end(), [&](const Entry &a, const Entry &b) {
        int order = 0;
        if (sorting == 2)
            order = a.stamp < b.stamp ? -1 : a.stamp > b.stamp ? 1 : 0;
        else if (sorting == 3)
            order = a.bytes < b.bytes ? -1 : a.bytes > b.bytes ? 1 : 0;
        if (!order)
            order = a.key.compare(b.key);
        if (!order)
            order = QString::compare(a.name, b.name, Qt::CaseSensitive);
        return reverse ? order > 0 : order < 0;
    });
    result.files.reserve(qsizetype(prepared.size()));
    result.positions.reserve(qsizetype(prepared.size()));
    for (const auto &file : prepared) {
        if (cancel && cancel->load())
            return {};
        result.positions.insert(file.path, result.files.size());
        result.files.append(file.path);
    }
    result.elapsedMs = timer.elapsed();
    return result;
}
} // namespace ix
