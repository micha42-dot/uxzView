// SPDX-License-Identifier: GPL-3.0-or-later
#include "tool_store.h"
#include "directory_commit.h"
#include "extension_storage.h"
#include "json_storage.h"
#include "plugin_store.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QUuid>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtEndian>
namespace ix {
namespace {
constexpr qint64 limit = 512LL * 1024 * 1024;
const QByteArray magic("UXZTOOL1\n");
bool hashValid(const QString &hash) {
    return QRegularExpression(QStringLiteral("^[a-fA-F0-9]{64}$")).match(hash).hasMatch();
}
bool pathValid(const QString &path) {
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_+./-]{1,180}$")).match(path).hasMatch() ||
        path.startsWith(QLatin1Char('/')))
        return false;
    for (const auto &part : path.split(QLatin1Char('/')))
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..") ||
            part.endsWith(QLatin1Char('.')) ||
            QRegularExpression(QStringLiteral("^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\\..*)?$"), QRegularExpression::CaseInsensitiveOption).match(part).hasMatch())
            return false;
    return true;
}
QString stateError(const QString &root) {
    const auto path = root + QStringLiteral("/tools.json");
    const auto error = jsonStorageError(path, false, 4 * 1024 * 1024);
    if (!error.isEmpty() || !QFileInfo::exists(path)) return error;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return file.errorString();
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value(QStringLiteral("schema")).toInt() != 1 || !object.value(QStringLiteral("packages")).isObject())
        return QStringLiteral("Ungültiger Werkzeugdatenspeicher; keine Änderung vorgenommen.");
    if (object.contains(QStringLiteral("layout")) && object.value(QStringLiteral("layout")).toString() != QStringLiteral("flat"))
        return QStringLiteral("Unbekannte Werkzeugordner-Struktur; keine Änderung vorgenommen.");
    return {};
}
bool containedFile(const QString &root, const QString &file) {
    const auto parent = QFileInfo(root).canonicalFilePath();
    const auto actual = QFileInfo(file).canonicalFilePath();
    return !parent.isEmpty() && QFileInfo(file).isFile() && !QFileInfo(file).isSymLink() &&
#ifdef Q_OS_WIN
        actual.startsWith(parent + QLatin1Char('/'), Qt::CaseInsensitive);
#else
        actual.startsWith(parent + QLatin1Char('/'));
#endif
}
QJsonObject header(QFile &file, QString &error) {
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit || file.read(magic.size()) != magic) {
        error = QStringLiteral("Kein gültiges Werkzeugpaket oder Paket zu groß.");
        return {};
    }
    const auto sizeBytes = file.read(4);
    if (sizeBytes.size() != 4) {
        error = QStringLiteral("Paketkopf ist unvollständig.");
        return {};
    }
    const auto size = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(sizeBytes.constData()));
    if (!size || size > 1024 * 1024) {
        error = QStringLiteral("Paketbeschreibung ist zu groß.");
        return {};
    }
    const auto bytes = file.read(size);
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (bytes.size() != size || parse.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Paketbeschreibung ist beschädigt.");
        return {};
    }
    const auto manifest = document.object();
    error = ToolStore::compatibility(manifest);
    if (!error.isEmpty())
        return {};
    const auto files = manifest.value(QStringLiteral("files")).toArray();
    QSet<QString> paths;
    QSet<QString> exactPaths;
    qint64 total = file.pos();
    for (const auto &entry : files) {
        const auto item = entry.toObject();
        const auto path = item.value(QStringLiteral("path")).toString();
        const qint64 length = item.value(QStringLiteral("bytes")).toInteger(-1);
        if (!pathValid(path) || path.compare(QStringLiteral("manifest.json"), Qt::CaseInsensitive) == 0 || paths.contains(path.toCaseFolded()) || length <= 0 || length > limit ||
            !hashValid(item.value(QStringLiteral("sha256")).toString())) {
            error = QStringLiteral("Ungültige Datei, Prüfsumme oder doppelter Pfad im Paket.");
            return {};
        }
        paths.insert(path.toCaseFolded());
        exactPaths.insert(path);
        total += length;
        if (total > limit) {
            error = QStringLiteral("Werkzeugpaket überschreitet 512 MiB.");
            return {};
        }
    }
    const auto required = manifest.value(QStringLiteral("kind")).toString() == QStringLiteral("ocr-language")
        ? QStringLiteral("data/") + manifest.value(QStringLiteral("language")).toString() + QStringLiteral(".traineddata")
        : manifest.value(QStringLiteral("entryPoint")).toString();
    if (files.isEmpty() || files.size() > 256 || total != file.size() || !exactPaths.contains(required)) {
        error = QStringLiteral("Paketgröße oder benötigte Werkzeugdatei stimmt nicht.");
        return {};
    }
    return manifest;
}
}
ToolStore::ToolStore(QString root) : root_(root.isEmpty() ? ExtensionStorage::activeRoot(ExtensionStorage::Domain::Tools) : std::move(root)) {}
QJsonObject ToolStore::state() const {
    QFile file(root_ + QStringLiteral("/tools.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QString ToolStore::writeState(const QJsonObject &state) const {
    QSaveFile file(root_ + QStringLiteral("/tools.json"));
    const auto bytes = QJsonDocument(state).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return file.errorString();
    return {};
}
QJsonArray ToolStore::installed() const {
    QJsonArray result;
    const auto packages = state().value(QStringLiteral("packages")).toObject();
    for (auto it = packages.constBegin(); it != packages.constEnd(); ++it)
        result.append(it.value());
    return result;
}
QJsonObject ToolStore::package(const QString &id) const {
    return state().value(QStringLiteral("packages")).toObject().value(id).toObject();
}
QString ToolStore::compatibility(const QJsonObject &m) {
    const auto id = m.value(QStringLiteral("id")).toString();
    const auto kind = m.value(QStringLiteral("kind")).toString();
    if (m.value(QStringLiteral("schema")).toInt() != 1 ||
        !QRegularExpression(QStringLiteral("^[a-z0-9][a-z0-9.-]{1,63}$")).match(id).hasMatch() || id.contains(QStringLiteral("..")) ||
        !QRegularExpression(QStringLiteral("^[0-9]+(?:\\.[0-9]+){1,3}$")).match(m.value(QStringLiteral("version")).toString()).hasMatch())
        return QStringLiteral("Unbekannte oder ungültige Werkzeugbeschreibung.");
    if (kind == QStringLiteral("ocr-language")) {
        const auto language = m.value(QStringLiteral("language")).toString();
        if (!QRegularExpression(QStringLiteral("^[a-z]{3}$")).match(language).hasMatch() || id != QStringLiteral("tessdata-") + language)
            return QStringLiteral("Ungültiges OCR-Sprachpaket.");
    } else if (kind == QStringLiteral("image-tool")) {
        const auto protocol = m.value(QStringLiteral("protocol")).toString();
        if ((id != QStringLiteral("zxing-cpp") || protocol != QStringLiteral("zxing-json-v1")) &&
            (id != QStringLiteral("tesseract-ocr") || protocol != QStringLiteral("tesseract-text-v1")))
            return QStringLiteral("Dieses Werkzeugprotokoll wird noch nicht unterstützt.");
        if (!pathValid(m.value(QStringLiteral("entryPoint")).toString()) ||
            m.value(QStringLiteral("os")).toString() != PluginStore::platform() ||
            m.value(QStringLiteral("arch")).toString() != QSysInfo::currentCpuArchitecture())
            return QStringLiteral("Werkzeug passt nicht zu Betriebssystem oder Prozessor.");
    } else
        return QStringLiteral("Paket ist keine Funktionserweiterung.");
    return {};
}
QString ToolStore::payloadRoot() const {
    const auto stored = state();
    return !stored.isEmpty() && stored.value(QStringLiteral("layout")).toString() != QStringLiteral("flat")
        ? root_ + QStringLiteral("/tools") : root_;
}
QString ToolStore::packagePath(const QJsonObject &m) const {
    return payloadRoot() + QLatin1Char('/') + m.value(QStringLiteral("id")).toString() + QLatin1Char('/') +
           m.value(QStringLiteral("version")).toString();
}
QString ToolStore::executable(const QString &id) const {
    const auto item = package(id);
    if (item.value(QStringLiteral("kind")).toString() != QStringLiteral("image-tool") ||
        !item.value(QStringLiteral("enabled")).toBool() || !compatibility(item).isEmpty())
        return {};
    const auto file = packagePath(item) + QLatin1Char('/') + item.value(QStringLiteral("entryPoint")).toString();
    return containedFile(packagePath(item), file) ? file : QString();
}
QString ToolStore::languageFile(const QString &language) const {
    const auto item = package(QStringLiteral("tessdata-") + language);
    if (!item.value(QStringLiteral("enabled")).toBool() || !compatibility(item).isEmpty())
        return {};
    const auto file = packagePath(item) + QStringLiteral("/data/") + language + QStringLiteral(".traineddata");
    return containedFile(packagePath(item), file) ? file : QString();
}
QStringList ToolStore::languages() const {
    QStringList result;
    for (const auto &value : installed()) {
        const auto language = value.toObject().value(QStringLiteral("language")).toString();
        if (!language.isEmpty() && !languageFile(language).isEmpty())
            result.append(language);
    }
    result.sort();
    return result;
}
QJsonObject ToolStore::inspect(const QString &path, QString &error) {
    error.clear();
    QFile file(path);
    return header(file, error);
}
QString ToolStore::install(const QString &path, const QJsonObject &expected, const std::atomic_bool &cancel) {
    if (root_ == ExtensionStorage::activeRoot(ExtensionStorage::Domain::Tools) && ExtensionStorage::restartPending(ExtensionStorage::Domain::Tools))
        return QStringLiteral("Bitte zuerst neu starten, um den neuen Erweiterungsordner zu verwenden.");
    if (cancel.load())
        return QStringLiteral("Installation abgebrochen.");
    QFile input(path);
    QString error;
    const auto manifest = header(input, error);
    if (!error.isEmpty())
        return error;
    if (!expected.isEmpty()) {
        for (const auto *key : {"id", "kind", "version", "os", "arch", "protocol", "language", "entryPoint"})
            if (expected.value(QString::fromLatin1(key)) != manifest.value(QString::fromLatin1(key)))
                return QStringLiteral("Werkzeugpaket stimmt nicht mit dem ausgewählten Katalogeintrag überein.");
        if (expected.value(QStringLiteral("bytes")).toInteger(-1) != input.size() ||
            !hashValid(expected.value(QStringLiteral("sha256")).toString()))
            return QStringLiteral("Downloadgröße oder Paketprüfsumme fehlt oder stimmt nicht.");
    }
    if (!QDir().mkpath(root_))
        return QStringLiteral("Erweiterungsordner konnte nicht angelegt werden.");
    QLockFile lock(root_ + QStringLiteral("/install.lock"));
    if (!lock.tryLock(0))
        return QStringLiteral("Die Erweiterungsverwaltung ist belegt.");
    error = stateError(root_);
    if (!error.isEmpty())
        return error;
    const auto destination = packagePath(manifest);
    if (QFileInfo::exists(destination))
        return QStringLiteral("Diese Werkzeugversion ist bereits installiert.");
    QTemporaryDir stage(root_ + QStringLiteral("/.tool-stage-XXXXXX"));
    if (!stage.isValid())
        return QStringLiteral("Werkzeugpaket konnte nicht vorbereitet werden.");
    QCryptographicHash packageHash(QCryptographicHash::Sha256);
    const auto offset = input.pos();
    input.seek(0);
    packageHash.addData(input.read(offset));
    for (const auto &entry : manifest.value(QStringLiteral("files")).toArray()) {
        const auto item = entry.toObject();
        const auto filePath = stage.filePath(item.value(QStringLiteral("path")).toString());
        QSaveFile output(filePath);
        if (!QDir().mkpath(QFileInfo(filePath).absolutePath()) || !output.open(QIODevice::WriteOnly))
            return QStringLiteral("Werkzeugdatei konnte nicht geschrieben werden.");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 remaining = item.value(QStringLiteral("bytes")).toInteger();
        while (remaining > 0) {
            if (cancel.load())
                return QStringLiteral("Installation abgebrochen.");
            const auto bytes = input.read(std::min<qint64>(remaining, 1024 * 1024));
            if (bytes.isEmpty() || output.write(bytes) != bytes.size())
                return QStringLiteral("Werkzeugdatei ist unvollständig.");
            remaining -= bytes.size();
            hash.addData(bytes);
            packageHash.addData(bytes);
        }
        if (QString::fromLatin1(hash.result().toHex()).compare(item.value(QStringLiteral("sha256")).toString(), Qt::CaseInsensitive) != 0)
            return QStringLiteral("SHA-256 einer Werkzeugdatei stimmt nicht.");
        if (item.value(QStringLiteral("executable")).toBool() &&
            !output.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther))
            return QStringLiteral("Werkzeugdatei konnte nicht ausführbar gemacht werden.");
        if (!output.commit())
            return QStringLiteral("Werkzeugdatei konnte nicht gespeichert werden.");
    }
    if (!expected.isEmpty() && QString::fromLatin1(packageHash.result().toHex()).compare(expected.value(QStringLiteral("sha256")).toString(), Qt::CaseInsensitive) != 0)
        return QStringLiteral("SHA-256 des Werkzeugpakets stimmt nicht.");
    if (cancel.load())
        return QStringLiteral("Installation abgebrochen.");
    auto reduced = manifest;
    const auto previous = package(manifest.value(QStringLiteral("id")).toString());
    reduced.insert(QStringLiteral("enabled"), previous.isEmpty() || previous.value(QStringLiteral("enabled")).toBool());
    QSaveFile description(stage.filePath(QStringLiteral("manifest.json")));
    const auto bytes = QJsonDocument(reduced).toJson();
    if (!description.open(QIODevice::WriteOnly) || description.write(bytes) != bytes.size() || !description.commit())
        return QStringLiteral("Werkzeugbeschreibung konnte nicht gespeichert werden.");
    if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !detail::commitDirectory(stage.path(), destination, cancel))
        return cancel.load() ? QStringLiteral("Installation abgebrochen.") : QStringLiteral("Werkzeugpaket konnte nicht übernommen werden.");
    auto state = this->state();
    if (state.isEmpty()) state.insert(QStringLiteral("layout"), QStringLiteral("flat"));
    auto packages = state.value(QStringLiteral("packages")).toObject();
    packages.insert(manifest.value(QStringLiteral("id")).toString(), reduced);
    state.insert(QStringLiteral("schema"), 1);
    state.insert(QStringLiteral("packages"), packages);
    error = writeState(state);
    if (!error.isEmpty() && QDir().rename(destination, stage.path()))
        return error;
    return error;
}
ToolUse::ToolUse() = default;
ToolUse::~ToolUse() = default;
namespace {
bool safeDirectory(const QString &root, const QString &path, bool recursive = false) {
    const auto base = QFileInfo(root).canonicalFilePath();
    const auto physical = QFileInfo(path).canonicalFilePath();
#ifdef Q_OS_WIN
    constexpr auto cs = Qt::CaseInsensitive;
#else
    constexpr auto cs = Qt::CaseSensitive;
#endif
    if (base.isEmpty() || physical.isEmpty() ||
        !physical.startsWith(base + QLatin1Char('/'), cs)) return false;
    auto cursor = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const auto absoluteRoot = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    while (cursor.compare(absoluteRoot, cs) != 0) {
        const QFileInfo info(cursor);
        if (!info.isDir() || info.isSymLink() || info.isJunction()) return false;
        const auto parent = info.absolutePath();
        if (parent == cursor) return false;
        cursor = parent;
    }
    if (recursive) {
        QDirIterator entries(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories);
        while (entries.hasNext()) {
            entries.next();
            const auto info = entries.fileInfo();
            if (info.isSymLink() || info.isJunction() || (!info.isFile() && !info.isDir())) return false;
        }
    }
    return true;
}
}
std::unique_ptr<ToolUse> ToolStore::acquire(const QString &id, const QStringList &languages, QString &error) const {
    error.clear();
    if (!QFileInfo(root_).isDir()) {
        error = QStringLiteral("Werkzeug unter Optionen → Erweiterungen installieren oder aktivieren."); return {};
    }
    QLockFile lock(root_ + QStringLiteral("/install.lock"));
    if (!lock.tryLock(0)) { error = QStringLiteral("Die Erweiterungsverwaltung ist belegt."); return {}; }
    if (root_ == ExtensionStorage::activeRoot(ExtensionStorage::Domain::Tools) && ExtensionStorage::restartPending(ExtensionStorage::Domain::Tools)) {
        error = QStringLiteral("Bitte üxzView zuerst neu starten."); return {};
    }
    error = stateError(root_);
    if (!error.isEmpty()) return {};
    auto use = std::make_unique<ToolUse>();
    use->executable_ = executable(id);
    if (use->executable_.isEmpty()) {
        error = QStringLiteral("Werkzeug unter Optionen → Erweiterungen installieren oder aktivieren."); return {};
    }
    QStringList ids{id};
    for (const auto &language : languages) {
        const auto file = languageFile(language);
        if (file.isEmpty()) { error = QStringLiteral("Sprachdaten konnten nicht vorbereitet werden."); return {}; }
        use->models_.append(file);
        ids.append(QStringLiteral("tessdata-") + language);
    }
    ids.removeDuplicates();
    for (const auto &packageId : ids) {
        if (!safeDirectory(root_, packagePath(package(packageId)))) { error = QStringLiteral("Unsicherer Erweiterungspfad."); return {}; }
        const auto directory = root_ + QStringLiteral("/.tool-use/") + packageId;
        if (!QDir().mkpath(directory) || !safeDirectory(root_, directory)) {
            error = QStringLiteral("Werkzeug konnte nicht reserviert werden."); return {};
        }
        auto lease = std::make_unique<QLockFile>(directory + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".lock"));
        lease->setStaleLockTime(0);
        if (!lease->tryLock(0)) { error = QStringLiteral("Werkzeug konnte nicht reserviert werden."); return {}; }
        use->locks_.push_back(std::move(lease));
    }
    return use;
}
QString ToolStore::remove(const QString &id, const std::atomic_bool &cancel) {
    if (root_ == ExtensionStorage::activeRoot(ExtensionStorage::Domain::Tools) && ExtensionStorage::restartPending(ExtensionStorage::Domain::Tools))
        return QStringLiteral("Bitte üxzView zuerst neu starten.");
    if (!QRegularExpression(QStringLiteral("^[a-z0-9][a-z0-9.-]{1,63}$")).match(id).hasMatch() || id.contains(QStringLiteral("..")))
        return QStringLiteral("Unbekannte oder ungültige Werkzeugbeschreibung.");
    QLockFile lock(root_ + QStringLiteral("/install.lock"));
    if (!lock.tryLock(0)) return QStringLiteral("Die Erweiterungsverwaltung ist belegt.");
    auto error = stateError(root_);
    if (!error.isEmpty()) return error;
    auto state = this->state();
    auto packages = state.value(QStringLiteral("packages")).toObject();
    const auto item = packages.value(id).toObject();
    if (item.isEmpty()) return QStringLiteral("Werkzeug ist nicht installiert.");
    if (item.value(QStringLiteral("id")).toString() != id || !compatibility(item).isEmpty())
        return QStringLiteral("Unbekannte oder ungültige Werkzeugbeschreibung.");
    const auto leases = root_ + QStringLiteral("/.tool-use/") + id;
    if (QFileInfo::exists(leases)) {
        if (!safeDirectory(root_, leases, true)) return QStringLiteral("Unsicherer Erweiterungspfad.");
        for (const auto &name : QDir(leases).entryList(QDir::Files | QDir::Hidden | QDir::System)) {
            QLockFile reader(leases + QLatin1Char('/') + name);
            reader.setStaleLockTime(0);
            if (!reader.tryLock(0)) return QStringLiteral("Erweiterung wird gerade verwendet.");
        }
    }
    const auto source = payloadRoot() + QLatin1Char('/') + id;
    const bool exists = QFileInfo::exists(source);
    if ((exists || QFileInfo(source).isSymLink() || QFileInfo(source).isJunction()) && !safeDirectory(root_, source, true)) return QStringLiteral("Unsicherer Erweiterungspfad.");
    if (cancel.load()) return QStringLiteral("Abgebrochen.");
    const auto quarantine = payloadRoot() + QStringLiteral("/.remove-") + id + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (exists && !detail::commitDirectory(source, quarantine, cancel))
        return cancel.load() ? QStringLiteral("Abgebrochen.") : QStringLiteral("Erweiterung konnte nicht entfernt werden.");
    packages.remove(id);
    state.insert(QStringLiteral("packages"), packages);
    error = writeState(state);
    if (!error.isEmpty()) {
        const std::atomic_bool restore{false};
        if (exists && !detail::commitDirectory(quarantine, source, restore))
            return error + QStringLiteral(" Wiederherstellung fehlgeschlagen: ") + quarantine;
        return error;
    }
    // Only the validated package tree is deleted, after its state update commits.
    if (exists && (!safeDirectory(root_, quarantine, true) || !QDir(quarantine).removeRecursively()))
        return QStringLiteral("Erweiterung entfernt; verbliebene Dateien konnten nicht gelöscht werden: ") + quarantine;
    QDir().rmdir(leases);
    return {};
}
QString ToolStore::setEnabled(const QString &id, bool enabled) {
    if (root_ == ExtensionStorage::activeRoot(ExtensionStorage::Domain::Tools) && ExtensionStorage::restartPending(ExtensionStorage::Domain::Tools))
        return QStringLiteral("Bitte zuerst neu starten, um den neuen Erweiterungsordner zu verwenden.");
    QLockFile lock(root_ + QStringLiteral("/install.lock"));
    if (!lock.tryLock(0))
        return QStringLiteral("Die Erweiterungsverwaltung ist belegt.");
    auto error = stateError(root_);
    if (!error.isEmpty())
        return error;
    auto state = this->state();
    auto packages = state.value(QStringLiteral("packages")).toObject();
    if (!packages.contains(id))
        return QStringLiteral("Werkzeug ist nicht installiert.");
    auto item = packages.value(id).toObject();
    item.insert(QStringLiteral("enabled"), enabled);
    packages.insert(id, item);
    state.insert(QStringLiteral("packages"), packages);
    return writeState(state);
}
} // namespace ix
