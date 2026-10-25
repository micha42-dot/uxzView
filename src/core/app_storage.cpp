// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_storage.h"
#include "directory_commit.h"
#include "preview_service.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QUrl>
#include <memory>
#include <vector>
namespace ix {
namespace {
bool within(const QString &path, const QString &parent) {
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    const auto prefix = parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/');
    return path.compare(parent, sensitivity) == 0 || path.startsWith(prefix, sensitivity);
}
QString homePath(const QString &home) {
    return QDir::cleanPath(home.isEmpty() ? AppStorage::applicationHome() : QFileInfo(home).absoluteFilePath());
}
QString settingsPath(const QString &data) {
    return data + QStringLiteral("/settings/IxView/IxView.ini");
}
QString write(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return path + QStringLiteral(": ") + file.errorString();
    return {};
}
QString writable(const QString &path) {
    if (!QDir().mkpath(path))
        return QStringLiteral("Ordner konnte nicht angelegt werden: ") + path;
    QTemporaryFile probe(path + QStringLiteral("/.ixview-write-XXXXXX"));
    if (!probe.open())
        return QStringLiteral("Portabler Speicherort ist nicht beschreibbar: ") + path;
    return {};
}
QString rebasePath(const QString &value, const QString &before, const QString &after) {
    if (value.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(value);
        if (url.isLocalFile()) {
            const auto local = url.toLocalFile();
            const auto changed = rebasePath(local, before, after);
            return changed == local ? value : QUrl::fromLocalFile(changed).toString();
        }
    }
    const auto path = QDir::fromNativeSeparators(value);
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    if (path.compare(before, sensitivity) == 0)
        return after;
    const auto prefix = before.endsWith(QLatin1Char('/')) ? before : before + QLatin1Char('/');
    if (path.startsWith(prefix, sensitivity))
        return QDir(after).filePath(path.mid(prefix.size()));
    return value;
}
QJsonValue rebaseJson(const QJsonValue &value, const QString &before, const QString &after, bool &collision,
                      const QString &field = {}) {
    if (value.isString()) {
        if (field == QStringLiteral("source") || field == QStringLiteral("target") ||
            field == QStringLiteral("backup") || field == QStringLiteral("image"))
            return rebasePath(value.toString(), before, after);
        return value; // Preserve metadata, profile names and literal watermark text.
    }
    if (value.isArray()) {
        QJsonArray result;
        for (auto entry : value.toArray())
            result.append(rebaseJson(entry, before, after, collision, field));
        return result;
    }
    if (value.isObject()) {
        QJsonObject result;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const auto key = field.isEmpty() ? rebasePath(it.key(), before, after) : it.key();
            if (result.contains(key))
                collision = true;
            result.insert(key, rebaseJson(it.value(), before, after, collision, it.key()));
        }
        return result;
    }
    return value;
}
QVariant rebaseSetting(const QVariant &value, const QString &before, const QString &after) {
    if (value.metaType().id() == QMetaType::QString)
        return rebasePath(value.toString(), before, after);
    if (value.metaType().id() == QMetaType::QStringList) {
        auto list = value.toStringList();
        for (auto &path : list)
            path = rebasePath(path, before, after);
        return list;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        auto list = value.toList();
        for (auto &entry : list)
            entry = rebaseSetting(entry, before, after);
        return list;
    }
    if (value.metaType().id() == QMetaType::QVariantMap) {
        auto map = value.toMap();
        for (auto &entry : map)
            entry = rebaseSetting(entry, before, after);
        return map;
    }
    return value;
}
QString relocate(const QString &data, const QString &before, const QString &after) {
    if (before == after || before.isEmpty())
        return {};
    // Only application-owned records contain paths that we rewrite. External
    // profiles/images are never edited, and plugin package contents stay intact.
    QStringList files;
    if (QFileInfo::exists(data + QStringLiteral("/catalog.json")))
        files.append(data + QStringLiteral("/catalog.json"));
    if (QFileInfo::exists(data + QStringLiteral("/file-journal/operations.json")))
        files.append(data + QStringLiteral("/file-journal/operations.json"));
    // Trash contains original user files, including arbitrary JSON. Rewriting
    // any of those bytes would invalidate undo hashes and corrupt the backup.
    for (const auto &folder : {QStringLiteral("file-journal/entries"), QStringLiteral("profiles")}) {
        QDirIterator entries(data + QLatin1Char('/') + folder, {QStringLiteral("*.json")},
                             QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (entries.hasNext())
            files.append(entries.next());
    }
    for (const auto &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 256LL * 1024 * 1024)
            return QStringLiteral("Portable Pfade konnten nicht gelesen werden: ") + path;
        const auto bytes = file.readAll();
        file.close();
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(bytes, &parse);
        if (parse.error != QJsonParseError::NoError || (!document.isObject() && !document.isArray()))
            return QStringLiteral("Ungültige portable Verwaltungsdaten: ") + path;
        bool collision = false;
        const auto changed = rebaseJson(document.isObject() ? QJsonValue(document.object())
                                                           : QJsonValue(document.array()), before, after, collision);
        if (collision)
            return QStringLiteral("Pfadkollision in portablen Verwaltungsdaten: ") + path;
        const auto replacement = changed.isObject() ? QJsonDocument(changed.toObject())
                                                    : QJsonDocument(changed.toArray());
        if (replacement != document) {
            const auto error = write(path, replacement.toJson());
            if (!error.isEmpty())
                return error;
        }
    }
    const auto ini = settingsPath(data);
    if (QFileInfo::exists(ini)) {
        QSettings settings(ini, QSettings::IniFormat);
        settings.setFallbacksEnabled(false);
        if (settings.status() != QSettings::NoError)
            return QStringLiteral("Portable Einstellungen konnten nicht gelesen werden.");
        for (const auto &key : settings.allKeys()) {
            const auto value = settings.value(key);
            const auto changed = rebaseSetting(value, before, after);
            if (value != changed)
                settings.setValue(key, changed);
        }
        settings.sync();
        if (settings.status() != QSettings::NoError)
            return QStringLiteral("Portable Einstellungen konnten nicht gespeichert werden.");
    }
    return {};
}
QString copyTree(const QString &source, const QString &target, const std::atomic_bool &cancel,
                 const QStringList &excluded = {}) {
    if (!QFileInfo::exists(source))
        return {};
    QDirIterator entries(source, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        if (cancel.load())
            return QStringLiteral("Kopieren abgebrochen.");
        const auto path = entries.next();
        const auto info = entries.fileInfo();
        if (info.isSymLink() || info.isJunction())
            return QStringLiteral("Verknüpfung im Benutzerprofil wird nicht kopiert: ") + path;
        const auto relative = QDir(source).relativeFilePath(path);
        if (relative.split(QLatin1Char('/')).contains(QStringLiteral(".tool-use"))) continue;
        bool skip = false;
        for (const auto &name : excluded)
            skip |= relative == name || relative.startsWith(name + QLatin1Char('/'));
        if (skip) continue;
        if (relative == QStringLiteral("catalog.json.lock") ||
            relative == QStringLiteral("file-journal/journal.lock") ||
            relative == QStringLiteral("extensions/install.lock") ||
            relative == QStringLiteral("install.lock") ||
            (relative.startsWith(QStringLiteral("settings/")) && info.fileName().endsWith(QStringLiteral(".lock"))))
            continue;
        const auto destination = QDir(target).filePath(relative);
        if (info.isDir()) {
            if (!QDir().mkpath(destination))
                return QStringLiteral("Ordner konnte nicht kopiert werden: ") + destination;
            continue;
        }
        if (!info.isFile() || !QDir().mkpath(QFileInfo(destination).absolutePath()))
            return QStringLiteral("Datei konnte nicht kopiert werden: ") + path;
        const auto revision = imageFileKey(path);
        QFile input(path);
        QSaveFile output(destination);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
            return QStringLiteral("Datei konnte nicht kopiert werden: ") + path;
        while (!input.atEnd()) {
            if (cancel.load())
                return QStringLiteral("Kopieren abgebrochen.");
            const auto bytes = input.read(1024 * 1024);
            if (bytes.isEmpty() || output.write(bytes) != bytes.size())
                return QStringLiteral("Datei konnte nicht kopiert werden: ") + path;
        }
        if (input.error() != QFileDevice::NoError || imageFileKey(path) != revision ||
            !output.setPermissions(info.permissions()) || !output.commit())
            return QStringLiteral("Datei wurde beim Kopieren geändert oder konnte nicht gespeichert werden: ") + path;
    }
    return {};
}
QString locationFile(const QString &home) {
    return QDir(home).filePath(QStringLiteral("IxView-data/location.json"));
}
QString marker(const QString &home) {
    return QDir(home).filePath(QStringLiteral("portable.mode"));
}
} // namespace
QString AppStorage::applicationHome() {
    const auto home = QCoreApplication::instance()->property("ixStorageHome").toString();
    if (!home.isEmpty()) return home;
    QDir folder(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    if (folder.dirName() == QStringLiteral("MacOS")) {
        QDir bundle(folder);
        if (bundle.cdUp() && bundle.dirName() == QStringLiteral("Contents") && bundle.cdUp() &&
            bundle.dirName().endsWith(QStringLiteral(".app")) && bundle.cdUp())
            return bundle.absolutePath();
    }
#endif
    return folder.absolutePath();
}
bool AppStorage::portableRequested(const QString &home) {
    return QFileInfo::exists(marker(homePath(home)));
}
bool AppStorage::isPortable() {
    return QCoreApplication::instance()->property("ixStoragePortable").toBool();
}
QString AppStorage::dataRoot() {
    const auto path = QCoreApplication::instance()->property("ixStorageData").toString();
    return path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : path;
}
QString AppStorage::cacheRoot() {
    const auto path = QCoreApplication::instance()->property("ixStorageCache").toString();
    return path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) : path;
}
QString AppStorage::initialize(const QString &requestedHome, bool portable) {
    const auto home = homePath(requestedHome);
    portable |= portableRequested(home);
    QString data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (portable) {
        if (QFileInfo(marker(home)).isSymLink() || QFileInfo(home + QStringLiteral("/IxView-data")).isSymLink())
            return QStringLiteral("Portabler Speicherort darf keine Verknüpfung sein.");
        data = QDir(home).filePath(QStringLiteral("IxView-data"));
        cache = data + QStringLiteral("/cache");
        for (const auto &path : {data, cache, data + QStringLiteral("/settings/IxView")}) {
            const auto error = writable(path);
            if (!error.isEmpty())
                return error;
        }
        QLockFile lock(data + QStringLiteral("/storage.lock"));
        if (!lock.tryLock(0))
            return QStringLiteral("Portables Profil wird gerade vorbereitet.");
        QFile location(locationFile(home));
        if (location.exists()) {
            if (!location.open(QIODevice::ReadOnly) || location.size() > 16384)
                return QStringLiteral("Portable Standortdaten konnten nicht gelesen werden.");
            QJsonParseError parse;
            const auto object = QJsonDocument::fromJson(location.readAll(), &parse).object();
            location.close();
            const auto previous = object.value(QStringLiteral("home")).toString();
            if (parse.error != QJsonParseError::NoError || object.value(QStringLiteral("schema")).toInt() != 1 ||
                previous.isEmpty() || !QDir::isAbsolutePath(previous))
                return QStringLiteral("Ungültige portable Standortdaten.");
            const auto error = relocate(data, previous, home);
            if (!error.isEmpty())
                return error;
        }
        auto error = write(locationFile(home), QJsonDocument(QJsonObject{
            {QStringLiteral("schema"), 1}, {QStringLiteral("home"), home}}).toJson());
        if (!error.isEmpty())
            return error;
        if (!QFileInfo::exists(marker(home))) {
            error = write(marker(home), QByteArray("IxView portable mode\n"));
            if (!error.isEmpty())
                return error;
        }
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data + QStringLiteral("/settings"));
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, data + QStringLiteral("/settings/system"));
        QSettings settings;
        settings.allKeys(); // Force parsing before accepting the portable profile.
        if (settings.status() != QSettings::NoError || !settings.isWritable())
            return QStringLiteral("Portable Einstellungen sind beschädigt oder nicht beschreibbar.");
    }
    // Application properties also reach lazily loaded modules that statically
    // link ixcore. These immutable paths are installed before any worker starts.
    auto *app = QCoreApplication::instance();
    app->setProperty("ixStorageHome", home);
    app->setProperty("ixStoragePortable", portable);
    app->setProperty("ixStorageData", data);
    app->setProperty("ixStorageCache", cache);
    return {};
}
QString AppStorage::preparePortable(const QString &requestedHome, const QVariantMap &values,
                                   const QString &sourceData, const std::atomic_bool &cancel) {
    if (cancel.load())
        return QStringLiteral("Kopieren abgebrochen.");
    const auto home = homePath(requestedHome);
    const auto source = QDir::cleanPath(QFileInfo(sourceData).absoluteFilePath());
    if (!QDir::isAbsolutePath(sourceData) || within(home, source))
        return QStringLiteral("Der portable Programmordner darf nicht im zu kopierenden Benutzerprofil liegen.");
    auto error = writable(home);
    if (!error.isEmpty())
        return error;
    const auto physicalHome = QFileInfo(home).canonicalFilePath();
    const auto physicalSource = QFileInfo(sourceData).canonicalFilePath();
    if (!physicalSource.isEmpty() && within(physicalHome, physicalSource))
        return QStringLiteral("Der portable Programmordner darf nicht im zu kopierenden Benutzerprofil liegen.");
    QLockFile lock(home + QStringLiteral("/.ixview-portable.lock"));
    if (!lock.tryLock(0))
        return QStringLiteral("Portables Profil wird gerade vorbereitet.");
    const auto target = QDir(home).filePath(QStringLiteral("IxView-data"));
    if (QFileInfo::exists(target)) {
        if (QFileInfo(target).isSymLink() || !QFileInfo::exists(locationFile(home)))
            return QStringLiteral("IxView-data existiert bereits ohne gültiges portables Profil.");
        QFile location(locationFile(home));
        if (!location.open(QIODevice::ReadOnly) || location.size() > 16384)
            return QStringLiteral("Portable Standortdaten konnten nicht gelesen werden.");
        QJsonParseError parse;
        const auto object = QJsonDocument::fromJson(location.readAll(), &parse).object();
        if (parse.error != QJsonParseError::NoError || object.value(QStringLiteral("schema")).toInt() != 1 ||
            !QDir::isAbsolutePath(object.value(QStringLiteral("home")).toString()))
            return QStringLiteral("Ungültige portable Standortdaten.");
        const auto error = writable(target + QStringLiteral("/settings/IxView"));
        if (!error.isEmpty())
            return error;
        // Re-enabling resumes the portable profile; it never overwrites it with
        // settings from a different normal-mode session.
        return write(marker(home), QByteArray("IxView portable mode\n"));
    }
    // Use the same locks as the catalog, journal and extension manager so another
    // IxView process cannot change records/backups halfway through the snapshot.
    std::vector<std::unique_ptr<QLockFile>> sourceLocks;
    for (const auto &name : {QStringLiteral("catalog.json.lock"), QStringLiteral("file-journal/journal.lock"),
                             QStringLiteral("extensions/install.lock")}) {
        const auto path = QDir(sourceData).filePath(name);
        if (!QFileInfo(path).dir().exists())
            continue;
        auto sourceLock = std::make_unique<QLockFile>(path);
        if (!sourceLock->tryLock(0))
            return QStringLiteral("Das Benutzerprofil ist belegt oder nicht beschreibbar: ") + sourceData;
        sourceLocks.push_back(std::move(sourceLock));
    }
    QTemporaryDir staging(home + QStringLiteral("/.ixview-profile-XXXXXX"));
    if (!staging.isValid())
        return QStringLiteral("Portables Profil konnte nicht vorbereitet werden.");
    const auto customExtensions = values.value(QStringLiteral("extensions/root")).toString();
    const auto configured = QDir::cleanPath(customExtensions);
    const auto defaultExtensions = QDir(sourceData).filePath(QStringLiteral("extensions"));
    const bool customRoot = !customExtensions.isEmpty() &&
        !(within(configured, defaultExtensions) && within(defaultExtensions, configured));
    QStringList excluded;
    if (customRoot) {
        const auto extensionInfo = QFileInfo(customExtensions);
        const auto extensionPath = QDir::cleanPath(extensionInfo.absoluteFilePath());
        const auto physicalExtensions = extensionInfo.canonicalFilePath();
        if (!QDir::isAbsolutePath(customExtensions) || extensionInfo.isSymLink() || extensionInfo.isJunction() ||
            within(home, extensionPath) || (!physicalExtensions.isEmpty() && within(physicalHome, physicalExtensions)))
            return QStringLiteral("Ungültiger Erweiterungsordner für den portablen Import.");
        excluded.append(QStringLiteral("extensions"));
        const auto relative = QDir(sourceData).relativeFilePath(customExtensions);
        if (!relative.startsWith(QStringLiteral("../"))) excluded.append(relative);
        if (QFileInfo::exists(customExtensions)) {
            auto extensionLock = std::make_unique<QLockFile>(customExtensions + QStringLiteral("/install.lock"));
            if (!extensionLock->tryLock(0)) return QStringLiteral("Die Erweiterungsverwaltung ist belegt.");
            sourceLocks.push_back(std::move(extensionLock));
        }
    }
    const auto customTools = values.value(QStringLiteral("tools/root")).toString();
    if (!customTools.isEmpty()) {
        const auto relative = QDir(sourceData).relativeFilePath(customTools);
        if (relative != QStringLiteral(".") && !relative.startsWith(QStringLiteral("../"))) excluded.append(relative);
    }
    error = copyTree(sourceData, staging.path(), cancel, excluded);
    if (!error.isEmpty())
        return error;
    if (customRoot) {
        error = copyTree(customExtensions, staging.filePath(QStringLiteral("extensions")), cancel);
        if (!error.isEmpty()) return error;
    }
    QString importedTools;
    if (!customTools.isEmpty()) {
        const QFileInfo info(customTools);
        const auto toolsPath = QDir::cleanPath(info.absoluteFilePath());
        const auto physicalTools = info.canonicalFilePath();
        if (!QDir::isAbsolutePath(customTools) || info.isSymLink() || info.isJunction() ||
            within(home, toolsPath) || (!physicalTools.isEmpty() && within(physicalHome, physicalTools)))
            return QStringLiteral("Ungültiger Werkzeugordner für den portablen Import.");
        // Tools already inside the application directory travel with the app.
        // External custom tools are snapshotted, like custom image extensions.
        if (!within(toolsPath, home)) {
            if (customRoot && within(toolsPath, configured) && within(configured, toolsPath)) {
                importedTools = target + QStringLiteral("/extensions");
            } else {
                std::unique_ptr<QLockFile> toolsLock;
                if (info.exists() && !(within(toolsPath, defaultExtensions) && within(defaultExtensions, toolsPath))) {
                    toolsLock = std::make_unique<QLockFile>(customTools + QStringLiteral("/install.lock"));
                    if (!toolsLock->tryLock(0)) return QStringLiteral("Die Erweiterungsverwaltung ist belegt.");
                }
                error = copyTree(customTools, staging.filePath(QStringLiteral("tools")), cancel);
                if (!error.isEmpty()) return error;
                importedTools = target + QStringLiteral("/tools");
            }
        }
    }
    if (!QDir().mkpath(staging.filePath(QStringLiteral("settings/IxView"))))
        return QStringLiteral("Portable Einstellungen konnten nicht angelegt werden.");
    {
        QSettings settings(settingsPath(staging.path()), QSettings::IniFormat);
        settings.setFallbacksEnabled(false);
        for (auto it = values.begin(); it != values.end(); ++it)
            settings.setValue(it.key(), it.value());
        if (customRoot) settings.setValue(QStringLiteral("extensions/root"), target + QStringLiteral("/extensions"));
        if (!importedTools.isEmpty()) settings.setValue(QStringLiteral("tools/root"), importedTools);
        settings.sync();
        if (settings.status() != QSettings::NoError)
            return QStringLiteral("Portable Einstellungen konnten nicht gespeichert werden.");
    }
    error = relocate(staging.path(), QDir::cleanPath(sourceData), target);
    if (!error.isEmpty())
        return error;
    error = write(staging.filePath(QStringLiteral("location.json")), QJsonDocument(QJsonObject{
        {QStringLiteral("schema"), 1}, {QStringLiteral("home"), home}}).toJson());
    if (!error.isEmpty())
        return error;
    if (cancel.load())
        return QStringLiteral("Kopieren abgebrochen.");
    if (!detail::commitDirectory(staging.path(), target, cancel))
        return cancel.load() ? QStringLiteral("Kopieren abgebrochen.") : QStringLiteral("Portables Profil konnte nicht übernommen werden.");
    staging.setAutoRemove(false);
    // The marker is the final commit. Failure leaves the copied profile intact
    // for review, while the existing user profile remains the active source.
    return write(marker(home), QByteArray("IxView portable mode\n"));
}
QString AppStorage::disablePortable(const QString &requestedHome) {
    const auto path = marker(homePath(requestedHome));
    if (QFileInfo::exists(path) && !QFile::remove(path))
        return QStringLiteral("Portabelmodus konnte nicht deaktiviert werden: ") + path;
    return {};
}
} // namespace ix
