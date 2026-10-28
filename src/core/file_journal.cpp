// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_storage.h"
#include "file_journal.h"
#include "file_journal_io.h"
#include "localization.h"
#include "image_catalog.h"
#include "json_storage.h"
#include "file_revision.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <limits>
namespace ix {
namespace {
QMutex journalMutex;
using journal_detail::moveFile;
using journal_detail::regular;
using journal_detail::occupied;
QString abs(const QString &path) {
    return QFileInfo(path).absoluteFilePath();
}
} // namespace
QString sha256File(const QString &path, const std::atomic_bool *cancel) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancel && cancel->load())
            return {};
        auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFile::NoError)
            return {};
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
FileJournal::FileJournal(QString root, WriteGuard writeGuard)
    : root_(root.isEmpty() ? AppStorage::dataRoot() +
                                 QStringLiteral("/file-journal")
                           : std::move(root)), writeGuard_(std::move(writeGuard)) {}
namespace {
constexpr qint64 LegacyLimit = 256LL * 1024 * 1024;
constexpr qint64 EntryLimit = 64LL * 1024 * 1024;
QString entryName(const QJsonObject &entry) {
    return QStringLiteral("%1-%2.json")
        .arg(entry.value(QStringLiteral("sequence")).toInteger(), 20, 10, QLatin1Char('0'))
        .arg(entry.value(QStringLiteral("id")).toString());
}
QString writeSequence(const QString &path, qint64 sequence) {
    QSaveFile file(path);
    auto bytes = QByteArray::number(sequence);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return file.errorString();
    return {};
}
} // namespace
QJsonArray FileJournal::readEntries(QString &error) const {
    QJsonArray result;
    QHash<QString, int> positions;
    auto append = [&](QJsonObject entry) {
        const auto id = entry.value(QStringLiteral("id")).toString();
        if (positions.contains(id))
            result.replace(positions.value(id), entry);
        else {
            positions.insert(id, result.size());
            result.append(entry);
        }
    };
    const auto legacy = root_ + QStringLiteral("/operations.json");
    error = jsonStorageError(legacy, true, LegacyLimit);
    if (!error.isEmpty())
        return {};
    QFile old(legacy);
    if (old.exists() && old.open(QIODevice::ReadOnly)) {
        auto all = QJsonDocument::fromJson(old.readAll()).array();
        for (int i = 0; i < all.size(); ++i) {
            auto entry = all[i].toObject();
            if (entry.value(QStringLiteral("id")).toString().isEmpty())
                entry.insert(QStringLiteral("id"),
                             QUuid::fromRfc4122(QCryptographicHash::hash(QJsonDocument(entry).toJson(),
                                                                         QCryptographicHash::Sha256)
                                                    .first(16))
                                 .toString(QUuid::WithoutBraces));
            entry.insert(QStringLiteral("sequence"), i + 1);
            append(entry);
        }
    }
    const QDir records(root_ + QStringLiteral("/entries"));
    for (const auto &name : records.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const auto path = records.filePath(name);
        error = jsonStorageError(path, false, EntryLimit);
        if (!error.isEmpty())
            return {};
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            error = file.errorString();
            return {};
        }
        auto entry = QJsonDocument::fromJson(file.readAll()).object();
        if (entryName(entry) != name || QUuid(entry.value(QStringLiteral("id")).toString()).isNull()) {
            error = QStringLiteral("Ungültiger Journaleintrag: ") + path;
            return {};
        }
        append(entry);
    }
    QList<QJsonObject> sorted;
    for (const auto &entry : result)
        sorted.append(entry.toObject());
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.value(QStringLiteral("sequence")).toInteger() <
               b.value(QStringLiteral("sequence")).toInteger();
    });
    result = {};
    for (const auto &entry : sorted)
        result.append(entry);
    return result;
}
QJsonArray FileJournal::entries(QString *error) const {
    QMutexLocker guard(&journalMutex);
    QString issue;
    auto result = readEntries(issue);
    if (error)
        *error = issue;
    return result;
}
QString FileJournal::migrateLegacy() const {
    const auto legacy = root_ + QStringLiteral("/operations.json");
    if (!QFileInfo::exists(legacy))
        return {};
    QString error;
    const auto all = readEntries(error);
    if (!error.isEmpty())
        return error;
    // A crash before the rename is recoverable: replay preserves the newer
    // per-entry states and existing sequence numbers on the next attempt.
    for (const auto &value : all) {
        auto entry = value.toObject();
        error = writeEntry(entry);
        if (!error.isEmpty())
            return error;
    }
    const auto archived = root_ + QStringLiteral("/operations.legacy-") +
                          QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".json");
    qint64 last = 0;
    for (const auto &value : all)
        last = std::max(last, value.toObject().value(QStringLiteral("sequence")).toInteger());
    error = writeSequence(root_ + QStringLiteral("/sequence"), last);
    if (!error.isEmpty())
        return error;
    if (!QFile::rename(legacy, archived))
        return QStringLiteral("Altes Journal konnte nicht archiviert werden.");
    return {};
}
QString FileJournal::writeEntry(QJsonObject &entry, bool reserve) const {
    if (QUuid(entry.value(QStringLiteral("id")).toString()).isNull())
        return QStringLiteral("Ungültige Journalkennung.");
    if (!QDir().mkpath(root_ + QStringLiteral("/entries")))
        return QStringLiteral("Journalordner konnte nicht erstellt werden.");
    if (!entry.contains(QStringLiteral("sequence"))) {
        const auto path = root_ + QStringLiteral("/sequence");
        QFile counter(path);
        qint64 last = 0;
        if (counter.exists()) {
            if (!counter.open(QIODevice::ReadOnly))
                return counter.errorString();
            bool ok = false;
            if (counter.size() > 32)
                return QStringLiteral("Beschädigte Journalreihenfolge.");
            last = counter.readAll().toLongLong(&ok);
            counter.close();
            if (!ok || last < 0 || last >= std::numeric_limits<qint64>::max() - 1)
                return QStringLiteral("Beschädigte Journalreihenfolge.");
        } else {
            const auto names = QDir(root_ + QStringLiteral("/entries"))
                                   .entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
            if (!names.isEmpty())
                last = names.last().first(20).toLongLong();
        }
        auto error = writeSequence(path, last + 1);
        if (!error.isEmpty())
            return error;
        entry.insert(QStringLiteral("sequence"), last + 1);
    }
    const auto bytes = QJsonDocument(entry).toJson(QJsonDocument::Compact);
    // Reserve room for later state changes and native paths before touching a
    // user file. The limit applies to one operation, never the whole history.
    if (bytes.size() > EntryLimit - (reserve ? 256 * 1024 : 0))
        return QStringLiteral("Journaleintrag ist zu groß; keine Datei geändert.");
    if (writeGuard_) {
        const auto error = writeGuard_(entry);
        if (!error.isEmpty()) return error;
    }
    QSaveFile file(root_ + QStringLiteral("/entries/") + entryName(entry));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return file.errorString();
    return {};
}
QString FileJournal::perform(const QString &kind, const QString &source, const QString &destination) {
    if (kind != QStringLiteral("copy") && kind != QStringLiteral("move") && kind != QStringLiteral("delete"))
        return QStringLiteral("Unbekannte Dateioperation.");
    QMutexLocker guard(&journalMutex);
    if (!QDir().mkpath(root_ + QStringLiteral("/trash")))
        return QStringLiteral("Journalordner konnte nicht erstellt werden.");
    QLockFile lock(root_ + QStringLiteral("/journal.lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Dateijournal wird von einer anderen Instanz benutzt.");
    if (!regular(source))
        return QStringLiteral("Quelle ist keine normale Datei oder ist eine Verknüpfung.");
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces), from = abs(source);
    const QString to = kind == QStringLiteral("delete") ? root_ + QStringLiteral("/trash/") + id +
                                                              QLatin1Char('-') + QFileInfo(source).fileName()
                                                        : abs(destination);
    if (destination.isEmpty() && kind != QStringLiteral("delete"))
        return QStringLiteral("Ziel fehlt.");
    if (occupied(to) || from == to)
        return QStringLiteral("Zieldatei existiert bereits.");
    const auto digest = sha256File(from);
    if (digest.isEmpty())
        return QStringLiteral("Quelle konnte nicht geprüft werden.");
    auto storageError = migrateLegacy();
    if (!storageError.isEmpty())
        return storageError;
    QJsonObject entry{{QStringLiteral("id"), id},
                      {QStringLiteral("kind"), kind},
                      {QStringLiteral("source"), from},
                      {QStringLiteral("target"), to},
                      {QStringLiteral("hash"), digest},
                      {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                      {QStringLiteral("state"), QStringLiteral("pending")}};
    auto error = writeEntry(entry, true);
    if (!error.isEmpty())
        return error;
    const bool ok = kind == QStringLiteral("copy") ? QFile::copy(from, to) : moveFile(from, to);
    if (ok && kind == QStringLiteral("move")) {
        auto warning = ImageCatalog().relocate(from, to);
        if (!warning.isEmpty())
            entry.insert(QStringLiteral("warning"), warning.left(4096));
    }
    entry.insert(QStringLiteral("state"), ok ? QStringLiteral("committed") : QStringLiteral("failed"));
    error = writeEntry(entry);
    if (!error.isEmpty())
        return ok ? IX_TEXT("Dateiänderung ausgeführt, Journalabschluss fehlgeschlagen. Bitte im Dateijournal prüfen: %1").arg(error)
                  : error;
    return ok ? QString() : QStringLiteral("Dateioperation fehlgeschlagen; Eintrag steht im Journal.");
}
QString FileJournal::installOutput(const QString &stage, const QString &destination, bool overwrite,
                                   const QString &expectedStamp, const QString &expectedHash) {
    QMutexLocker guard(&journalMutex);
    if (!QDir().mkpath(root_ + QStringLiteral("/trash")))
        return QStringLiteral("Journalordner konnte nicht erstellt werden.");
    QLockFile lock(root_ + QStringLiteral("/journal.lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Dateijournal ist belegt.");
    if (!regular(stage))
        return QStringLiteral("Ausgabedatei fehlt.");
    const auto target = abs(destination);
    const bool exists = occupied(target);
    if (exists && (!overwrite || !regular(target)))
        return QStringLiteral("Ziel existiert oder ist keine normale Datei.");
    if ((exists ? imageFileKey(target) : QString()) != expectedStamp)
        return QStringLiteral("Ziel wurde seit der Vorschau verändert. Bitte den Plan neu prüfen.");
    const auto hash = sha256File(stage);
    if (hash.isEmpty())
        return QStringLiteral("Ausgabe konnte nicht geprüft werden.");
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString backup = exists ? root_ + QStringLiteral("/trash/") + id + QStringLiteral("-original-") +
                                        QFileInfo(target).fileName()
                                  : QString();
    const auto backupHash = exists ? sha256File(target) : QString();
    if (exists && !expectedHash.isEmpty() && backupHash != expectedHash)
        return QStringLiteral("Zielinhalt wurde seit der Vorschau verändert. Bitte den Plan neu prüfen.");
    if (exists && backupHash.isEmpty())
        return QStringLiteral("Originaldatei konnte nicht geprüft werden.");
    QJsonObject entry{{QStringLiteral("id"), id},
                      {QStringLiteral("kind"), exists ? QStringLiteral("replace") : QStringLiteral("create")},
                      {QStringLiteral("source"), abs(stage)},
                      {QStringLiteral("target"), target},
                      {QStringLiteral("backup"), backup},
                      {QStringLiteral("backupHash"), backupHash},
                      {QStringLiteral("hash"), hash},
                      {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                      {QStringLiteral("state"), QStringLiteral("pending")}};
    auto storageError = migrateLegacy();
    if (!storageError.isEmpty())
        return storageError;
    auto error = writeEntry(entry, true);
    if (!error.isEmpty())
        return error;
    bool ok = !exists || moveFile(target, backup);
    if (ok)
        ok = moveFile(stage, target);
    if (!ok && exists && QFileInfo::exists(backup) && !QFileInfo::exists(target))
        moveFile(backup, target);
    entry.insert(QStringLiteral("state"), ok ? QStringLiteral("committed") : QStringLiteral("failed"));
    error = writeEntry(entry);
    if (!error.isEmpty())
        return ok ? IX_TEXT("Dateiänderung ausgeführt, Journalabschluss fehlgeschlagen. Bitte im Dateijournal prüfen: %1").arg(error)
                  : error;
    return ok ? QString() : QStringLiteral("Ausgabe konnte nicht installiert werden.");
}
QString FileJournal::recover() {
    QMutexLocker guard(&journalMutex);
    if (!QFileInfo::exists(root_)) return {};
    QLockFile lock(root_ + QStringLiteral("/journal.lock"));
    if (!lock.tryLock(0)) return IX_TEXT("Dateijournal ist belegt.");
    auto error = migrateLegacy();
    if (!error.isEmpty()) return error;
    auto all = readEntries(error);
    if (!error.isEmpty()) return error;
    return recoverEntries(all);
}
QString FileJournal::undo(const QString &chosen) {
    QMutexLocker guard(&journalMutex);
    QDir().mkpath(root_ + QStringLiteral("/trash"));
    QLockFile lock(root_ + QStringLiteral("/journal.lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Dateijournal ist belegt.");
    auto storageError = migrateLegacy();
    if (!storageError.isEmpty())
        return storageError;
    QString readError;
    auto all = readEntries(readError);
    if (!readError.isEmpty())
        return readError;
    int index = -1;
    for (int i = all.size() - 1; i >= 0; --i) {
        const auto entry = all.at(i).toObject();
        const auto state = entry.value(QStringLiteral("state")).toString();
        if (!chosen.isEmpty()) {
            if (entry.value(QStringLiteral("id")).toString() == chosen) { index = i; break; }
        } else if (state == QStringLiteral("committed") || state == QStringLiteral("pending") ||
                   state == QStringLiteral("undoPending")) { index = i; break; }
    }
    if (index < 0) return QStringLiteral("Keine rückgängig machbare Dateioperation vorhanden.");
    const auto originalState = all.at(index).toObject().value(QStringLiteral("state")).toString();
    const auto selectedId = all.at(index).toObject().value(QStringLiteral("id")).toString();
    readError = recoverEntries(all, selectedId);
    if (!readError.isEmpty()) return readError;
    const auto recoveredState = all.at(index).toObject().value(QStringLiteral("state")).toString();
    // A retry of an interrupted Undo must not consume the previous operation.
    if ((originalState == QStringLiteral("pending") || originalState == QStringLiteral("undoPending")) &&
        (recoveredState == QStringLiteral("undone") || recoveredState == QStringLiteral("failed"))) return {};
    if (recoveredState != QStringLiteral("committed"))
        return QStringLiteral("Keine rückgängig machbare Dateioperation vorhanden.");
    auto entry = all[index].toObject();
    const auto kind = entry.value(QStringLiteral("kind")).toString(),
               from = entry.value(QStringLiteral("source")).toString(),
               target = entry.value(QStringLiteral("target")).toString();
    if (kind != QStringLiteral("copy") && kind != QStringLiteral("move") &&
        kind != QStringLiteral("delete") && kind != QStringLiteral("create") &&
        kind != QStringLiteral("replace"))
        return QStringLiteral("Unbekannte Dateioperation.");
    if (!regular(target) || sha256File(target) != entry.value(QStringLiteral("hash")).toString())
        return QStringLiteral("Zieldatei fehlt oder wurde geändert; sie bleibt unverändert.");
    const bool restoreSource = kind == QStringLiteral("move") || kind == QStringLiteral("delete");
    if (restoreSource && occupied(from))
        return QStringLiteral("Ursprünglicher Pfad ist belegt; keine Datei überschrieben.");
    auto backup = entry.value(QStringLiteral("backup")).toString();
    if (kind == QStringLiteral("replace") &&
        (!regular(backup) || sha256File(backup) != entry.value(QStringLiteral("backupHash")).toString()))
        return QStringLiteral("Sicherung der ersetzten Datei fehlt.");
    const auto discard = restoreSource ? QString() : root_ + QStringLiteral("/trash/") +
        QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral("-undo-") + QFileInfo(target).fileName();
    if (!restoreSource) entry.insert(QStringLiteral("discard"), discard);
    entry.insert(QStringLiteral("state"), QStringLiteral("undoPending"));
    auto error = writeEntry(entry, true);
    if (!error.isEmpty())
        return error;
    bool ok;
    if (restoreSource) {
        QDir().mkpath(QFileInfo(from).absolutePath());
        ok = moveFile(target, from);
    } else {
        ok = moveFile(target, discard);
        if (ok && kind == QStringLiteral("replace")) {
            ok = moveFile(backup, target);
            if (!ok)
                moveFile(discard, target);
        }
        entry.insert(QStringLiteral("discard"), discard);
    }
    if (ok && kind == QStringLiteral("move")) {
        auto warning = ImageCatalog().relocate(target, from);
        if (!warning.isEmpty())
            entry.insert(QStringLiteral("warning"), warning.left(4096));
    }
    // If restoring the original AND rolling back both fail, retain the intent
    // so explicit recovery can finish restoring the verified backup later.
    const bool interruptedRestore = !restoreSource && kind == QStringLiteral("replace") &&
        occupied(discard) && !occupied(target);
    entry.insert(QStringLiteral("state"), ok ? QStringLiteral("undone") :
        interruptedRestore ? QStringLiteral("undoPending") : QStringLiteral("committed"));
    entry.insert(QStringLiteral("undoTime"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    error = writeEntry(entry);
    if (!error.isEmpty())
        return ok ? IX_TEXT("Dateiänderung ausgeführt, Journalabschluss fehlgeschlagen. Bitte im Dateijournal prüfen: %1").arg(error)
                  : error;
    return ok ? QString() : QStringLiteral("Rückgängig fehlgeschlagen; Pfade stehen im Journal.");
}
} // namespace ix
