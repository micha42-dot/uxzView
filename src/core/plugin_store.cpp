// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_storage.h"
#include "extension_storage.h"
#include "plugin_store.h"
#include "json_storage.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <QUuid>
#include <QVersionNumber>
#include <QtEndian>
namespace ix {
namespace {
QString architecture(const QByteArray &bytes) {
    if (bytes.size() < 64)
        return {};
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData());
    quint32 machine = 0;
    if (bytes.startsWith("MZ")) {
        if (PluginStore::platform() != QStringLiteral("windows"))
            return {};
        auto pos = qFromLittleEndian<quint32>(p + 0x3c);
        if (pos > quint32(bytes.size() - 6) || bytes.mid(pos, 4) != QByteArray("PE\0\0", 4))
            return {};
        machine = qFromLittleEndian<quint16>(p + pos + 4);
        return machine == 0x8664   ? QStringLiteral("x86_64")
               : machine == 0xaa64 ? QStringLiteral("arm64")
               : machine == 0x14c  ? QStringLiteral("x86")
                                   : QString();
    }
    if (bytes.startsWith(QByteArray::fromHex("7f454c46"))) {
        if (PluginStore::platform() != QStringLiteral("linux") || (p[5] != 1 && p[5] != 2))
            return {};
        machine = p[5] == 1 ? qFromLittleEndian<quint16>(p + 18) : qFromBigEndian<quint16>(p + 18);
        return machine == 62    ? QStringLiteral("x86_64")
               : machine == 183 ? QStringLiteral("arm64")
               : machine == 3   ? QStringLiteral("x86")
                                : QString();
    }
    if (bytes.startsWith(QByteArray::fromHex("cffaedfe"))) {
        if (PluginStore::platform() != QStringLiteral("macos"))
            return {};
        machine = qFromLittleEndian<quint32>(p + 4);
        return machine == 0x01000007   ? QStringLiteral("x86_64")
               : machine == 0x0100000c ? QStringLiteral("arm64")
                                       : QString();
    }
    if (bytes.startsWith(QByteArray::fromHex("cafebabe")) ||
        bytes.startsWith(QByteArray::fromHex("cafebabf"))) {
        if (PluginStore::platform() != QStringLiteral("macos"))
            return {};
        const bool wide = bytes.startsWith(QByteArray::fromHex("cafebabf"));
        const quint32 count = qFromBigEndian<quint32>(p + 4), stride = wide ? 32 : 20;
        if (!count || count > 32 || 8 + quint64(count) * stride > quint64(bytes.size()))
            return {};
        bool matching = false;
        for (quint32 n = 0; n < count; ++n) {
            const auto *entry = p + 8 + n * stride;
            auto cpu = qFromBigEndian<quint32>(entry);
            quint64 offset = wide ? qFromBigEndian<quint64>(entry + 8) : qFromBigEndian<quint32>(entry + 8);
            quint64 size = wide ? qFromBigEndian<quint64>(entry + 16) : qFromBigEndian<quint32>(entry + 12);
            if (offset > quint64(bytes.size()) || !size || size > quint64(bytes.size()) - offset)
                return {};
            matching |=
                (cpu == 0x01000007 && QSysInfo::currentCpuArchitecture() == QStringLiteral("x86_64")) ||
                (cpu == 0x0100000c && QSysInfo::currentCpuArchitecture() == QStringLiteral("arm64"));
        }
        return matching ? QStringLiteral("universal") : QString();
    }
    return {};
}
bool identifier(const QString &id) {
    return QRegularExpression(QStringLiteral("^[a-z0-9][a-z0-9.-]{1,63}$")).match(id).hasMatch() &&
           !id.contains(QStringLiteral(".."));
}
QStringList formatsOf(const QJsonObject &metadata) {
    QStringList formats;
    for (auto key :
         metadata.value(QStringLiteral("MetaData")).toObject().value(QStringLiteral("Keys")).toArray())
        formats.append(key.toString().toLower());
    return formats;
}
} // namespace
QString optionalModulePath(const QString &name) {
    auto app = QCoreApplication::applicationDirPath();
    for (const auto &folder :
         {app, app + QStringLiteral("/../lib/IxView"), app + QStringLiteral("/../PlugIns/IxView")}) {
        auto path = QDir(folder).absoluteFilePath(name);
        if (QFileInfo::exists(path))
            return path;
    }
    return app + QLatin1Char('/') + name;
}
QString PluginStore::platform() {
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}
PluginStore::PluginStore(QString root)
    : root_(root.isEmpty() ? ExtensionStorage::activeRoot()
                           : std::move(root)) {}
QJsonObject PluginStore::state() const {
    QFile file(root_ + QStringLiteral("/state.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QString PluginStore::writeState(const QJsonObject &s) const {
    QSaveFile file(root_ + QStringLiteral("/state.json"));
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    auto data = QJsonDocument(s).toJson();
    if (file.write(data) != data.size() || !file.commit())
        return file.errorString();
    return {};
}
QStringList PluginStore::disabledFormats() const {
    QStringList result;
    for (auto x : state().value(QStringLiteral("disabledFormats")).toArray())
        result.append(x.toString());
    return result;
}
QJsonArray PluginStore::installed() const {
    QJsonArray result;
    auto packages = state().value(QStringLiteral("packages")).toObject();
    for (auto it = packages.begin(); it != packages.end(); ++it) {
        auto object = it.value().toObject();
        object.insert(QStringLiteral("id"), it.key());
        result.append(object);
    }
    return result;
}
QString PluginStore::compatibility(const QJsonObject &m) {
    if (m.value(QStringLiteral("schema")).toInt() != 1 ||
        m.value(QStringLiteral("kind")).toString() != QStringLiteral("qt-imageformats"))
        return QStringLiteral("Unbekanntes Paketformat; erwartet wird ein üxzView-Qt-Bildformatpaket.");
    if (!identifier(m.value(QStringLiteral("id")).toString()) ||
        QVersionNumber::fromString(m.value(QStringLiteral("version")).toString()).isNull())
        return QStringLiteral("Ungültige Paketkennung oder Version.");
    if (m.value(QStringLiteral("os")).toString() != platform())
        return QStringLiteral("Paket ist für ein anderes Betriebssystem.");
    const auto cpu = m.value(QStringLiteral("arch")).toString();
    if (cpu != QSysInfo::currentCpuArchitecture() &&
        !(platform() == QStringLiteral("macos") && cpu == QStringLiteral("universal")))
        return QStringLiteral("Architektur passt nicht: benötigt %1, Paket ist %2.")
            .arg(QSysInfo::currentCpuArchitecture(), cpu);
    if (m.value(QStringLiteral("qtMajor")).toInt() != QT_VERSION_MAJOR ||
        m.value(QStringLiteral("qtMinor")).toInt(999) > QT_VERSION_MINOR)
        return QStringLiteral("Qt-Version des Pakets ist nicht kompatibel.");
    return {};
}
QString PluginStore::install(const QByteArray &package, const QString &expected) {
    if (root_ == ExtensionStorage::activeRoot() && ExtensionStorage::restartPending())
        return QStringLiteral("Bitte zuerst neu starten, um den neuen Erweiterungsordner zu verwenden.");
    if (package.size() > 64 * 1024 * 1024)
        return QStringLiteral("Paket überschreitet 64 MiB.");
    if (!expected.isEmpty() &&
        QString::fromLatin1(QCryptographicHash::hash(package, QCryptographicHash::Sha256).toHex())
                .compare(expected, Qt::CaseInsensitive) != 0)
        return QStringLiteral("SHA-256 des Pakets stimmt nicht.");
    QJsonParseError parse;
    auto document = QJsonDocument::fromJson(package, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("Paket ist kein gültiges JSON.");
    auto manifest = document.object();
    auto error = compatibility(manifest);
    if (!error.isEmpty())
        return error;
    auto files = manifest.value(QStringLiteral("files")).toArray();
    if (files.isEmpty() || files.size() > 32)
        return QStringLiteral("Paket benötigt 1 bis 32 Dateien.");
    if (!QDir().mkpath(root_))
        return QStringLiteral("Pluginordner konnte nicht angelegt werden.");
    QLockFile lock(root_ + QStringLiteral("/install.lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Plugin-Verwaltung ist belegt.");
    auto storageError = jsonStorageError(root_ + QStringLiteral("/state.json"), false, 4 * 1024 * 1024);
    if (!storageError.isEmpty())
        return storageError;
    const auto id = manifest.value(QStringLiteral("id")).toString(),
               version = manifest.value(QStringLiteral("version")).toString();
    if (!QRegularExpression(QStringLiteral("^[0-9]+(?:\\.[0-9]+){1,3}$")).match(version).hasMatch())
        return QStringLiteral("Version muss aus Ziffern und Punkten bestehen.");
    const QString stage = root_ + QStringLiteral("/.install-") +
                          QUuid::createUuid().toString(QUuid::WithoutBraces),
                  destination = root_ + QLatin1Char('/') + id + QLatin1Char('/') + version;
    if (QFileInfo::exists(destination))
        return QStringLiteral("Diese Version ist bereits installiert.");
    QDir().mkpath(stage + QStringLiteral("/imageformats"));
    auto clean = [&] {
        const auto resolved = QFileInfo(stage).absoluteFilePath();
        const auto prefix = QFileInfo(root_).absoluteFilePath() + QStringLiteral("/.install-");
        if (resolved.startsWith(prefix) && !QFileInfo(stage).isSymLink())
            QDir(resolved).removeRecursively();
    };
    QString plugin;
    QSet<QString> names;
    int plugins = 0;
    qsizetype total = 0;
    for (auto value : files) {
        auto file = value.toObject();
        auto name = file.value(QStringLiteral("name")).toString();
        if (name.isEmpty() || name != QFileInfo(name).fileName() || name.contains(QLatin1Char('\\')) ||
            name.contains(QLatin1Char(':')) || name.startsWith(QLatin1Char('.')) ||
            names.contains(name.toCaseFolded())) {
            clean();
            return QStringLiteral("Ungültiger oder doppelter Paketpfad.");
        }
        names.insert(name.toCaseFolded());
        auto decoded =
            QByteArray::fromBase64Encoding(file.value(QStringLiteral("data")).toString().toLatin1(),
                                           QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            clean();
            return QStringLiteral("Ungültige Base64-Paketdaten.");
        }
        auto bytes = decoded.decoded;
        total += bytes.size();
        if (total > 48 * 1024 * 1024 || bytes.isEmpty() ||
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
                    .compare(file.value(QStringLiteral("sha256")).toString(), Qt::CaseInsensitive) != 0) {
            clean();
            return QStringLiteral("Dateigröße oder SHA-256-Prüfung fehlgeschlagen.");
        }
        auto actual = architecture(bytes);
        if (actual != QSysInfo::currentCpuArchitecture() &&
            !(platform() == QStringLiteral("macos") && actual == QStringLiteral("universal"))) {
            clean();
            return QStringLiteral("Binärdatei hat eine falsche oder unbekannte Architektur.");
        }
        auto path = stage + QStringLiteral("/imageformats/") + name;
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            clean();
            return QStringLiteral("Plugin-Datei konnte nicht geschrieben werden.");
        }
        if (file.value(QStringLiteral("role")).toString() == QStringLiteral("plugin")) {
            plugin = path;
            ++plugins;
        }
    }
    if (plugins != 1) {
        clean();
        return QStringLiteral("Genau eine Plugin-Hauptdatei ist erforderlich.");
    }
    auto metadata = QPluginLoader(plugin).metaData();
    int qt = metadata.value(QStringLiteral("version")).toInt();
    if (!metadata.value(QStringLiteral("IID"))
             .toString()
             .contains(QStringLiteral("QImageIOHandlerFactoryInterface")) ||
        formatsOf(metadata).isEmpty() || (qt >> 16) != QT_VERSION_MAJOR ||
        ((qt >> 8) & 255) > QT_VERSION_MINOR) {
        clean();
        return QStringLiteral("Datei ist kein kompatibles Qt-Bildformatplugin.");
    }
#ifdef Q_OS_WIN
    if (metadata.value(QStringLiteral("debug")).toBool()) {
#ifndef QT_DEBUG
        clean();
        return QStringLiteral("Debug-Plugin passt nicht zur Release-Anwendung.");
#endif
    }
#endif
    QSaveFile description(stage + QStringLiteral("/manifest.json"));
    auto reduced = manifest;
    reduced.remove(QStringLiteral("files"));
    reduced.insert(QStringLiteral("formats"), QJsonArray::fromStringList(formatsOf(metadata)));
    if (!description.open(QIODevice::WriteOnly) || description.write(QJsonDocument(reduced).toJson()) < 0 ||
        !description.commit()) {
        clean();
        return QStringLiteral("Paketbeschreibung konnte nicht gespeichert werden.");
    }
    QDir().mkpath(QFileInfo(destination).absolutePath());
    if (!QDir().rename(stage, destination)) {
        clean();
        return QStringLiteral("Plugin-Paket konnte nicht aktiviert werden.");
    }
    auto s = state();
    auto packages = s.value(QStringLiteral("packages")).toObject();
    reduced.insert(QStringLiteral("enabled"), true);
    packages.insert(id, reduced);
    s.insert(QStringLiteral("packages"), packages);
    auto activationError = writeState(s);
    if (!activationError.isEmpty() && QDir().rename(destination, stage))
        clean();
    return activationError;
}
QString PluginStore::setEnabled(const QString &id, bool enabled) {
    if (root_ == ExtensionStorage::activeRoot() && ExtensionStorage::restartPending())
        return QStringLiteral("Bitte zuerst neu starten, um den neuen Erweiterungsordner zu verwenden.");
    QDir().mkpath(root_);
    QLockFile lock(root_ + QStringLiteral("/install.lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Plugin-Verwaltung ist belegt.");
    auto storageError = jsonStorageError(root_ + QStringLiteral("/state.json"), false, 4 * 1024 * 1024);
    if (!storageError.isEmpty())
        return storageError;
    auto s = state();
    auto p = s.value(QStringLiteral("packages")).toObject();
    if (!p.contains(id))
        return QStringLiteral("Paket nicht installiert.");
    auto item = p.value(id).toObject();
    item.insert(QStringLiteral("enabled"), enabled);
    p.insert(id, item);
    s.insert(QStringLiteral("packages"), p);
    return writeState(s);
}
QString PluginStore::setBuiltinEnabled(const QStringList &formats, bool enabled) {
    if (root_ == ExtensionStorage::activeRoot() && ExtensionStorage::restartPending())
        return QStringLiteral("Bitte zuerst neu starten, um den neuen Erweiterungsordner zu verwenden.");
    QDir().mkpath(root_);
    QLockFile lock(root_ + QStringLiteral("/install.lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Plugin-Verwaltung ist belegt.");
    auto storageError = jsonStorageError(root_ + QStringLiteral("/state.json"), false, 4 * 1024 * 1024);
    if (!storageError.isEmpty())
        return storageError;
    auto s = state();
    QSet<QString> disabled;
    for (auto x : s.value(QStringLiteral("disabledFormats")).toArray())
        disabled.insert(x.toString());
    for (auto f : formats) {
        if (enabled)
            disabled.remove(f.toLower());
        else
            disabled.insert(f.toLower());
    }
    s.insert(QStringLiteral("disabledFormats"), QJsonArray::fromStringList(disabled.values()));
    return writeState(s);
}
QJsonArray PluginStore::builtins() {
    QJsonArray result;
    QSet<QString> seen;
    for (const auto &root : QCoreApplication::libraryPaths())
        for (const auto &file : QDir(root + QStringLiteral("/imageformats")).entryInfoList(QDir::Files)) {
            if (seen.contains(file.canonicalFilePath()))
                continue;
            auto metadata = QPluginLoader(file.absoluteFilePath()).metaData();
            auto formats = formatsOf(metadata);
            if (formats.isEmpty())
                continue;
            seen.insert(file.canonicalFilePath());
            result.append(QJsonObject{{QStringLiteral("name"), file.fileName()},
                                      {QStringLiteral("path"), file.absoluteFilePath()},
                                      {QStringLiteral("formats"), QJsonArray::fromStringList(formats)}});
        }
    return result;
}
void PluginStore::prepareStartup() {
    PluginStore store;
    for (auto value : store.installed()) {
        auto p = value.toObject();
        if (!p.value(QStringLiteral("enabled")).toBool(true))
            continue;
        if (!compatibility(p).isEmpty() || !QRegularExpression(QStringLiteral("^[0-9]+(?:\\.[0-9]+){1,3}$"))
                                                .match(p.value(QStringLiteral("version")).toString())
                                                .hasMatch())
            continue;
        auto path = store.root_ + QLatin1Char('/') + p.value(QStringLiteral("id")).toString() +
                    QLatin1Char('/') + p.value(QStringLiteral("version")).toString();
        if (QFileInfo::exists(path + QStringLiteral("/manifest.json")))
            QCoreApplication::addLibraryPath(path);
    }
}
bool PluginStore::formatEnabled(const QByteArray &format) {
    static const QSet<QString> disabled = [] {
        QSet<QString> result;
        for (auto value : PluginStore().state().value(QStringLiteral("disabledFormats")).toArray())
            result.insert(value.toString().toLower());
        return result;
    }();
    return !disabled.contains(QString::fromLatin1(format).toLower());
}
} // namespace ix
