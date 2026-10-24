// SPDX-License-Identifier: GPL-3.0-or-later
#include "extension_storage.h"
#include "directory_commit.h"
#include "app_storage.h"
#include "preview_service.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
namespace ix {
namespace {
QString absolute(const QString &path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
bool same(const QString &a, const QString &b) {
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}
bool inside(const QString &path, const QString &parent) {
    const auto prefix = parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/');
#ifdef Q_OS_WIN
    return same(path, parent) || path.startsWith(prefix, Qt::CaseInsensitive);
#else
    return same(path, parent) || path.startsWith(prefix);
#endif
}
QString persist(const QString &path, ExtensionStorage::Domain domain) {
    QSettings settings;
    const auto key = domain == ExtensionStorage::Domain::Tools ? QStringLiteral("tools/root") : QStringLiteral("extensions/root");
    const auto old = settings.value(key);
    if (!settings.isWritable())
        return QStringLiteral("Einstellungen sind nicht beschreibbar.");
    // An explicit default also overrides a legacy shared extension directory.
    settings.setValue(key, path);
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return {};
    if (old.isValid())
        settings.setValue(key, old);
    else
        settings.remove(key);
    settings.sync();
    return QStringLiteral("Erweiterungsordner konnte nicht gespeichert werden.");
}
}
QString ExtensionStorage::defaultRoot(Domain domain) {
    return absolute(domain == Domain::Tools ? AppStorage::applicationHome() + QStringLiteral("/tools")
                                           : AppStorage::dataRoot() + QStringLiteral("/extensions"));
}
QString ExtensionStorage::requestedRoot(Domain domain) {
    const auto path = QSettings().value(domain == Domain::Tools ? QStringLiteral("tools/root") : QStringLiteral("extensions/root")).toString();
    if (!path.isEmpty()) return absolute(path);
    if (domain == Domain::Formats) return defaultRoot(domain);
    const auto previous = QSettings().value(QStringLiteral("extensions/root")).toString();
    if (!previous.isEmpty() && QFileInfo::exists(previous + QStringLiteral("/tools.json"))) return absolute(previous);
    const auto root = defaultRoot(domain);
    const auto legacy = absolute(AppStorage::dataRoot() + QStringLiteral("/extensions"));
    // Keep an existing installation usable when running an unbundled source
    // build. A distributed installation supplies its ready-to-use tools here.
    if (!QFileInfo::exists(root + QStringLiteral("/tools.json")) && QFileInfo::exists(legacy + QStringLiteral("/tools.json")))
        return legacy;
    return root;
}
QString ExtensionStorage::activeRoot(Domain domain) {
    const auto path = qApp->property(domain == Domain::Tools ? "ixToolRoot" : "ixExtensionRoot").toString();
    return path.isEmpty() ? requestedRoot(domain) : path;
}
void ExtensionStorage::initialize() {
    qApp->setProperty("ixExtensionRoot", requestedRoot());
    qApp->setProperty("ixToolRoot", requestedRoot(Domain::Tools));
}
bool ExtensionStorage::restartPending(Domain domain) { return !same(activeRoot(domain), requestedRoot(domain)); }
QString ExtensionStorage::prepareRoot(const QString &destination, const std::atomic_bool &cancel, Domain domain) {
    const auto source = activeRoot(domain), target = absolute(destination);
    if (cancel.load())
        return QStringLiteral("Kopieren abgebrochen.");
    if (same(source, target))
        return persist(source, domain);
    if (destination.trimmed().isEmpty() || QFileInfo(target).isSymLink() || QFileInfo(target).isJunction() || inside(target, source) || inside(source, target))
        return QStringLiteral("Der neue Erweiterungsordner darf nicht mit dem bisherigen Ordner verschachtelt sein.");
    if (QFileInfo::exists(target) && (!QFileInfo(target).isDir() ||
        !QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty()))
        return QStringLiteral("Bitte einen leeren Erweiterungsordner auswählen.");
    const auto parent = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(parent) || !QDir().mkpath(source) || QFileInfo(source).isSymLink() || QFileInfo(source).isJunction())
        return QStringLiteral("Erweiterungsordner konnte nicht angelegt werden.");
    const auto physicalParent = QFileInfo(parent).canonicalFilePath();
    const auto physicalSource = QFileInfo(source).canonicalFilePath();
    if (physicalParent.isEmpty() || physicalSource.isEmpty() || inside(physicalParent, physicalSource))
        return QStringLiteral("Der Zielordner liegt im bisherigen Erweiterungsordner.");
    QLockFile sourceLock(source + QStringLiteral("/install.lock"));
    QLockFile targetLock(parent + QStringLiteral("/.uxzview-extension-copy.lock"));
    if (!sourceLock.tryLock(0) || !targetLock.tryLock(0))
        return QStringLiteral("Die Erweiterungsverwaltung ist belegt.");
    QTemporaryDir stage(parent + QStringLiteral("/.uxzview-extensions-XXXXXX"));
    if (!stage.isValid())
        return QStringLiteral("Der Zielordner ist nicht beschreibbar.");
    QDirIterator entries(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        if (cancel.load())
            return QStringLiteral("Kopieren abgebrochen.");
        entries.next();
        const auto info = entries.fileInfo();
        const auto relative = QDir(source).relativeFilePath(info.absoluteFilePath());
        if (relative == QStringLiteral("install.lock") || relative.split(QLatin1Char('/')).contains(QStringLiteral(".tool-use")))
            continue;
        if (info.isSymLink() || info.isJunction())
            return QStringLiteral("Verknüpfungen im Erweiterungsordner werden nicht kopiert: ") + relative;
        const auto output = stage.filePath(relative);
        if (info.isDir()) {
            if (!QDir().mkpath(output))
                return QStringLiteral("Unterordner konnte nicht angelegt werden: ") + relative;
            continue;
        }
        if (!info.isFile())
            return QStringLiteral("Unbekannter Dateityp im Erweiterungsordner: ") + relative;
        const auto revision = imageFileKey(info.absoluteFilePath());
        QFile input(info.absoluteFilePath());
        QSaveFile file(output);
        if (!QDir().mkpath(QFileInfo(output).absolutePath()) || !input.open(QIODevice::ReadOnly) || !file.open(QIODevice::WriteOnly))
            return QStringLiteral("Datei konnte nicht kopiert werden: ") + relative;
        while (!input.atEnd()) {
            if (cancel.load())
                return QStringLiteral("Kopieren abgebrochen.");
            const auto bytes = input.read(1024 * 1024);
            if (bytes.isEmpty() || file.write(bytes) != bytes.size())
                return QStringLiteral("Datei konnte nicht vollständig kopiert werden: ") + relative;
        }
        if (input.error() != QFileDevice::NoError || revision != imageFileKey(info.absoluteFilePath()) ||
            !file.setPermissions(info.permissions()) || !file.commit())
            return QStringLiteral("Datei wurde verändert oder konnte nicht gespeichert werden: ") + relative;
    }
    if (cancel.load())
        return QStringLiteral("Kopieren abgebrochen.");
    if (QFileInfo::exists(target) && !QDir().rmdir(target))
        return QStringLiteral("Zielordner wurde während des Kopierens verändert.");
    if (!detail::commitDirectory(stage.path(), target, cancel))
        return cancel.load() ? QStringLiteral("Kopieren abgebrochen.") : QStringLiteral("Erweiterungsordner konnte nicht übernommen werden.");
    return persist(target, domain);
}
} // namespace ix
