// SPDX-License-Identifier: GPL-3.0-or-later
#include "file_journal.h"
#include "file_journal_io.h"
#include "file_revision.h"
#include "image_catalog.h"
#include "localization.h"
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <algorithm>
namespace ix {
namespace {
bool digestValid(const QString &text) {
    return text.size() == 64 && std::all_of(text.cbegin(), text.cend(), [](QChar c) {
        return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
               (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
    });
}
bool trashPath(const QString &path, const QString &root) {
    if (path.isEmpty() || !QFileInfo(path).isAbsolute()) return false;
    const auto parent = QDir::cleanPath(QFileInfo(path).absolutePath());
    const auto trash = QDir::cleanPath(QDir(root).absoluteFilePath(QStringLiteral("trash")));
#ifdef Q_OS_WIN
    return parent.compare(trash, Qt::CaseInsensitive) == 0;
#else
    return parent == trash;
#endif
}
struct Files {
    QHash<QString, QString> hashes;
    QString hash(const QString &path) {
        if (!journal_detail::regular(path)) return {};
        if (!hashes.contains(path)) {
            const auto revision = imageFileKey(path);
            const auto value = sha256File(path);
            hashes.insert(path, revision == imageFileKey(path) ? value : QString());
        }
        return hashes.value(path);
    }
    bool matches(const QString &path, const QString &digest) {
        return digestValid(digest) && hash(path) == digest;
    }
    bool restore(const QString &backup, const QString &target, const QString &digest) {
        if (journal_detail::occupied(target) || !matches(backup, digest)) return false;
        if (!journal_detail::moveFile(backup, target)) return false;
        hashes.remove(backup);
        hashes.remove(target);
        return matches(target, digest);
    }
};
// Empty means ambiguous: retain every file and the original intent record.
QString resolve(QJsonObject &entry, const QString &root, Files &files) {
    using journal_detail::occupied;
    const auto get = [&](const char *key) { return entry.value(QString::fromLatin1(key)).toString(); };
    const auto kind = get("kind"), from = get("source"), target = get("target"), digest = get("hash");
    const auto backup = get("backup"), oldDigest = get("backupHash"), discard = get("discard");
    if (!QFileInfo(from).isAbsolute() || !QFileInfo(target).isAbsolute() || !digestValid(digest)) return {};
    const bool undo = get("state") == QStringLiteral("undoPending");
    const bool moving = kind == QStringLiteral("move") || kind == QStringLiteral("delete");
    if (undo) {
        if (moving) {
            if (!occupied(target) && files.matches(from, digest)) return QStringLiteral("undone");
            if (!occupied(from) && files.matches(target, digest)) return QStringLiteral("committed");
        } else if (kind == QStringLiteral("copy") || kind == QStringLiteral("create")) {
            if (!trashPath(discard, root)) return {};
            if (!occupied(target) && files.matches(discard, digest)) return QStringLiteral("undone");
            if (!occupied(discard) && files.matches(target, digest)) return QStringLiteral("committed");
        } else if (kind == QStringLiteral("replace")) {
            if (!trashPath(backup, root) || !trashPath(discard, root) || !digestValid(oldDigest)) return {};
            if (files.matches(discard, digest)) {
                if (!occupied(backup) && files.matches(target, oldDigest)) return QStringLiteral("undone");
                if (!occupied(target) && files.restore(backup, target, oldDigest)) return QStringLiteral("undone");
            }
            if (!occupied(discard) && files.matches(target, digest) && files.matches(backup, oldDigest))
                return QStringLiteral("committed");
        }
        return {};
    }
    if (kind == QStringLiteral("copy")) {
        if (files.matches(target, digest)) return QStringLiteral("committed");
        if (!occupied(target) && files.matches(from, digest)) return QStringLiteral("failed");
    } else if (moving || kind == QStringLiteral("create")) {
        if (!occupied(from) && files.matches(target, digest)) return QStringLiteral("committed");
        if (!occupied(target) && files.matches(from, digest)) return QStringLiteral("failed");
    } else if (kind == QStringLiteral("replace")) {
        if (!trashPath(backup, root) || !digestValid(oldDigest)) return {};
        if (!occupied(from) && files.matches(target, digest) && files.matches(backup, oldDigest))
            return QStringLiteral("committed");
        if (!occupied(target) && files.restore(backup, target, oldDigest)) return QStringLiteral("failed");
        if (!occupied(backup) && files.matches(target, oldDigest)) return QStringLiteral("failed");
    }
    return {};
}
} // namespace
QString FileJournal::recoverEntries(QJsonArray &entries, const QString &chosen) const {
    QString issue;
    for (int i = 0; i < entries.size(); ++i) {
        auto entry = entries.at(i).toObject();
        const auto state = entry.value(QStringLiteral("state")).toString();
        if (!chosen.isEmpty() && entry.value(QStringLiteral("id")).toString() != chosen) continue;
        const bool strandedBackup = state == QStringLiteral("failed") &&
            entry.value(QStringLiteral("kind")).toString() == QStringLiteral("replace") &&
            !journal_detail::occupied(entry.value(QStringLiteral("target")).toString()) &&
            journal_detail::occupied(entry.value(QStringLiteral("backup")).toString());
        if (state != QStringLiteral("pending") && state != QStringLiteral("undoPending") && !strandedBackup) continue;
        Files files;
        const auto recovered = resolve(entry, root_, files);
        if (recovered.isEmpty()) {
            if (issue.isEmpty()) issue = IX_TEXT("Unterbrochener Dateivorgang ist nicht eindeutig. Dateien bleiben zur Prüfung erhalten: %1")
                .arg(entry.value(QStringLiteral("target")).toString());
            continue;
        }
        if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("move")) {
            const auto from = entry.value(QStringLiteral("source")).toString();
            const auto target = entry.value(QStringLiteral("target")).toString();
            QString warning;
            if (state == QStringLiteral("pending") && recovered == QStringLiteral("committed"))
                warning = ImageCatalog().relocate(from, target);
            if (state == QStringLiteral("undoPending") && recovered == QStringLiteral("undone"))
                warning = ImageCatalog().relocate(target, from);
            if (!warning.isEmpty()) entry.insert(QStringLiteral("warning"), warning.left(4096));
        }
        entry.insert(QStringLiteral("state"), recovered);
        entry.insert(QStringLiteral("recoveredAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        const auto error = writeEntry(entry);
        if (!error.isEmpty()) return error;
        entries.replace(i, entry);
    }
    return issue;
}
} // namespace ix
