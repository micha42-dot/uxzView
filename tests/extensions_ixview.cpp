// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/app_storage.h"
#include "core/directory_commit.h"
#include "core/extension_storage.h"
#include "core/plugin_store.h"
#include "core/tool_store.h"
#include "core/recognition_input.h"
#include "ui/tool_dialog.h"
#include "ui/main_window.h"
#include "ui/image_view.h"
#include "ui/platform_support.h"
#include <QtTest>
#include <QtWidgets>
#include <QProcess>
#include <QtEndian>
#include <atomic>
#include <QtConcurrent>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace {
bool put(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray get(const QString &path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QJsonObject manifest() {
    return {{QStringLiteral("schema"), 1}, {QStringLiteral("id"), QStringLiteral("zxing-cpp")},
        {QStringLiteral("version"), QStringLiteral("3.0.0")}, {QStringLiteral("kind"), QStringLiteral("image-tool")},
        {QStringLiteral("os"), ix::PluginStore::platform()}, {QStringLiteral("arch"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("protocol"), QStringLiteral("zxing-json-v1")}, {QStringLiteral("entryPoint"), QStringLiteral("bin/reader")}};
}
QByteArray archive(QJsonObject item, const QByteArray &payload, QString path = QStringLiteral("bin/reader")) {
    item.insert(QStringLiteral("files"), QJsonArray{QJsonObject{{QStringLiteral("path"), path},
        {QStringLiteral("bytes"), payload.size()}, {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("executable"), true}}});
    const auto bytes = QJsonDocument(item).toJson(QJsonDocument::Compact);
    QByteArray prefix("UXZTOOL1\n");
    const quint32 size = qToBigEndian(quint32(bytes.size()));
    prefix.append(reinterpret_cast<const char *>(&size), 4);
    return prefix + bytes + payload;
}
QJsonArray realCatalog() {
    return QJsonDocument::fromJson(get(QStringLiteral(IX_TOOL_PACKS "/uxzView-tools.json"))).object().value(QStringLiteral("packages")).toArray();
}
QImage qrImage() {
    // Version 1 QR containing ABCDEF. Matrix from ZXing-C++ QREncoderTest,
    // Copyright 2008 ZXing authors, Apache-2.0 (fixture attribution below).
    const QStringList rows{
        QStringLiteral("111111100101001111111"), QStringLiteral("100000101010101000001"),
        QStringLiteral("101110100000001011101"), QStringLiteral("101110100100101011101"),
        QStringLiteral("101110100101001011101"), QStringLiteral("100000101001101000001"),
        QStringLiteral("111111101010101111111"), QStringLiteral("000000001000100000000"),
        QStringLiteral("000011110110101100010"), QStringLiteral("000011011100111101101"),
        QStringLiteral("100001100101000111011"), QStringLiteral("100111001111000010000"),
        QStringLiteral("011111101010111001100"), QStringLiteral("000000001100011000101"),
        QStringLiteral("111111101111000001100"), QStringLiteral("100000101101000101111"),
        QStringLiteral("101110101001000110011"), QStringLiteral("101110100011010000111"),
        QStringLiteral("101110100101000110000"), QStringLiteral("100000100100100110001"),
        QStringLiteral("111111100010010000111")};
    QImage image(290, 290, QImage::Format_RGB32); image.fill(Qt::white);
    QPainter painter(&image);
    for (int y = 0; y < rows.size(); ++y) for (int x = 0; x < rows[y].size(); ++x)
        if (rows[y][x] == QLatin1Char('1')) painter.fillRect((x + 4) * 10, (y + 4) * 10, 10, 10, Qt::black);
    return image;
}
}
class ExtensionTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_{QStringLiteral(IX_EXTENSION_OUTPUT "/run-extensions-XXXXXX")};
    QString folder_;
    int number_ = 0;
    QString path(const QString &name) const { return folder_ + QLatin1Char('/') + name; }
    void installReal(const QStringList &ids) {
        const auto catalog = realCatalog();
        QVERIFY2(!catalog.isEmpty(), "Build optional packs with scripts/build_tool_packs.py first");
        std::atomic_bool cancel{false};
        for (const auto &id : ids) {
            bool found = false;
            for (const auto &value : catalog) {
                const auto item = value.toObject();
                if (item.value(QStringLiteral("id")).toString() != id) continue;
                found = true;
                const auto error = ix::ToolStore().install(QStringLiteral(IX_TOOL_PACKS "/") + item.value(QStringLiteral("packageUrl")).toString(), item, cancel);
                QVERIFY2(error.isEmpty(), qPrintable(error));
            }
            QVERIFY(found);
        }
    }
  private slots:
    void initTestCase() { QVERIFY(temporary_.isValid()); QCoreApplication::setOrganizationName(QStringLiteral("IxView")); QCoreApplication::setApplicationName(QStringLiteral("IxView")); }
    void init() {
        const auto name = QByteArray(QTest::currentTestFunction());
        if ((name.startsWith("real") || name == "managerSectionsInstallAndDisable") && realCatalog().isEmpty()) {
            if (qEnvironmentVariableIsSet("IX_REQUIRE_TOOLS")) QFAIL("Optional tool packs are required; run scripts/build_tool_packs.py");
            QSKIP("Optional native packs not built; run scripts/build_tool_packs.py to include real tool checks");
        }
        folder_ = temporary_.path() + QStringLiteral("/%1-ä-日本").arg(++number_);
        QVERIFY(QDir().mkpath(folder_));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path(QStringLiteral("settings")));
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, path(QStringLiteral("system")));
        qApp->setProperty("ixStorageData", path(QStringLiteral("profile")));
        qApp->setProperty("ixStorageHome", path(QStringLiteral("app")));
        qApp->setProperty("ixStorageCache", path(QStringLiteral("cache")));
        qApp->setProperty("ixExtensionRoot", QVariant());
        qApp->setProperty("ixToolRoot", QVariant());
        ix::ExtensionStorage::initialize();
    }
    void installEnableAndRejectCorruption() {
        const auto file = path(QStringLiteral("tool.ixtool"));
        const QByteArray payload(2 * 1024 * 1024 + 17, 'x');
        auto bytes = archive(manifest(), payload);
        QVERIFY(put(file, bytes));
        std::atomic_bool cancel{false};
        ix::ToolStore store;
        QVERIFY(store.install(file, {}, cancel).isEmpty());
        QCOMPARE(get(store.executable(QStringLiteral("zxing-cpp"))), payload);
        QVERIFY(!store.install(file, {}, cancel).isEmpty());
        QVERIFY(store.setEnabled(QStringLiteral("zxing-cpp"), false).isEmpty());
        QVERIFY(store.executable(QStringLiteral("zxing-cpp")).isEmpty());
        auto upgrade = manifest(); upgrade.insert(QStringLiteral("version"), QStringLiteral("3.0.1"));
        QVERIFY(put(file, archive(upgrade, payload)));
        QVERIFY(store.install(file, {}, cancel).isEmpty());
        QVERIFY(store.executable(QStringLiteral("zxing-cpp")).isEmpty());
        QVERIFY(store.setEnabled(QStringLiteral("zxing-cpp"), true).isEmpty());
        auto next = manifest(); next.insert(QStringLiteral("version"), QStringLiteral("3.0.2"));
        bytes = archive(next, payload); bytes[bytes.size() - 1] = 'y';
        QVERIFY(put(file, bytes));
        QVERIFY(!store.install(file, {}, cancel).isEmpty());
        QCOMPARE(get(store.executable(QStringLiteral("zxing-cpp"))), payload);
        QCOMPARE(store.installed().size(), 1);
        QString error;
        auto first = store.acquire(QStringLiteral("zxing-cpp"), {}, error);
        QVERIFY2(first, qPrintable(error));
        auto second = store.acquire(QStringLiteral("zxing-cpp"), {}, error);
        QVERIFY2(second, qPrintable(error));
        QVERIFY(!store.remove(QStringLiteral("zxing-cpp"), cancel).isEmpty());
        const auto pinned = first->executable();
        auto latest = manifest(); latest.insert(QStringLiteral("version"), QStringLiteral("3.0.3"));
        QVERIFY(put(file, archive(latest, "updated")));
        QVERIFY(store.install(file, {}, cancel).isEmpty());
        QCOMPARE(get(first->executable()), payload);
        QCOMPARE(first->executable(), pinned);
        QCOMPARE(get(store.executable(QStringLiteral("zxing-cpp"))), QByteArray("updated"));
        first.reset();
        QVERIFY(!store.remove(QStringLiteral("zxing-cpp"), cancel).isEmpty());
        second.reset();
        cancel.store(true);
        QVERIFY(!store.remove(QStringLiteral("zxing-cpp"), cancel).isEmpty());
        QVERIFY(!store.executable(QStringLiteral("zxing-cpp")).isEmpty());
        cancel.store(false);
        QVERIFY(!store.remove(QStringLiteral("../outside"), cancel).isEmpty());
        QVERIFY(store.setEnabled(QStringLiteral("zxing-cpp"), false).isEmpty());
        const auto packageRoot = store.root() + QStringLiteral("/zxing-cpp");
        QVERIFY(QFileInfo::exists(packageRoot + QStringLiteral("/3.0.0")));
        QVERIFY(QFileInfo::exists(packageRoot + QStringLiteral("/3.0.1")));
        const auto outside = path(QStringLiteral("outside-package"));
        QVERIFY(put(outside + QStringLiteral("/keep"), "keep outside files"));
        const auto linked = packageRoot + QStringLiteral("/linked");
#ifdef Q_OS_WIN
        QProcess junction;
        junction.setNativeArguments(QStringLiteral("/d /c mklink /J \"%1\" \"%2\"").arg(
            QDir::toNativeSeparators(linked), QDir::toNativeSeparators(outside)));
        junction.start(QStringLiteral("cmd.exe"), QStringList{});
        QVERIFY(junction.waitForFinished());
        const auto junctionMessage = junction.readAllStandardError() + junction.readAllStandardOutput();
        QVERIFY2(junction.exitCode() == 0, junctionMessage.constData());
        QVERIFY(QFileInfo(linked).isJunction());
#else
        QVERIFY(QFile::link(outside, linked));
#endif
        QVERIFY(!store.remove(QStringLiteral("zxing-cpp"), cancel).isEmpty());
        QCOMPARE(get(outside + QStringLiteral("/keep")), QByteArray("keep outside files"));
        QVERIFY(!store.package(QStringLiteral("zxing-cpp")).isEmpty());
#ifdef Q_OS_WIN
        QVERIFY(QDir().rmdir(linked));
#else
        QVERIFY(QFile::remove(linked));
#endif
#ifdef Q_OS_WIN
        const auto stateFile = store.root() + QStringLiteral("/tools.json");
        const auto before = get(stateFile);
        const auto nativeState = QDir::toNativeSeparators(stateFile);
        HANDLE held = CreateFileW(reinterpret_cast<LPCWSTR>(nativeState.utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        const auto failed = store.remove(QStringLiteral("zxing-cpp"), cancel);
        CloseHandle(held);
        QVERIFY(!failed.isEmpty());
        QCOMPARE(get(stateFile), before);
        QVERIFY(QFileInfo::exists(packageRoot + QStringLiteral("/3.0.0")));
        QVERIFY(!store.package(QStringLiteral("zxing-cpp")).isEmpty());
#endif
        QVERIFY(store.remove(QStringLiteral("zxing-cpp"), cancel).isEmpty());
        QCOMPARE(get(outside + QStringLiteral("/keep")), QByteArray("keep outside files"));
        QVERIFY(store.installed().isEmpty());
        QVERIFY(!QFileInfo::exists(packageRoot));
        QVERIFY(put(file, archive(manifest(), payload)));
        QVERIFY(store.install(file, {}, cancel).isEmpty());
        QCOMPARE(get(store.executable(QStringLiteral("zxing-cpp"))), payload);
    }
    void defaultFolderAndLegacyLayout() {
        using Domain = ix::ExtensionStorage::Domain;
        QCOMPARE(ix::ExtensionStorage::defaultRoot(Domain::Tools), path(QStringLiteral("app/tools")));
        QCOMPARE(ix::ToolStore().root(), path(QStringLiteral("app/tools")));
        QCOMPARE(ix::PluginStore().root(), path(QStringLiteral("profile/extensions")));
        const auto file = path(QStringLiteral("tool.ixtool"));
        QVERIFY(put(file, archive(manifest(), "reader bytes")));
        std::atomic_bool cancel{false};
        ix::ToolStore fresh;
        QVERIFY(fresh.install(file, {}, cancel).isEmpty());
        QCOMPARE(fresh.executable(QStringLiteral("zxing-cpp")), path(QStringLiteral("app/tools/zxing-cpp/3.0.0/bin/reader")));
        QVERIFY(!QFileInfo::exists(path(QStringLiteral("app/tools/tools"))));
        // An old shared store has no layout field and nests tool payloads.
        const auto legacy = path(QStringLiteral("custom-extensions"));
        auto old = manifest(); old.insert(QStringLiteral("enabled"), false);
        QVERIFY(put(legacy + QStringLiteral("/tools.json"), QJsonDocument(QJsonObject{
            {QStringLiteral("schema"), 1}, {QStringLiteral("packages"), QJsonObject{{QStringLiteral("zxing-cpp"), old}}}}).toJson()));
        QVERIFY(put(legacy + QStringLiteral("/tools/zxing-cpp/3.0.0/bin/reader"), "legacy bytes"));
        QSettings().setValue(QStringLiteral("extensions/root"), legacy);
        ix::ExtensionStorage::initialize();
        ix::ToolStore preserved;
        QCOMPARE(preserved.root(), legacy);
        QVERIFY(preserved.executable(QStringLiteral("zxing-cpp")).isEmpty());
        QVERIFY(preserved.setEnabled(QStringLiteral("zxing-cpp"), true).isEmpty());
        QCOMPARE(get(preserved.executable(QStringLiteral("zxing-cpp"))), QByteArray("legacy bytes"));
        QVERIFY(preserved.remove(QStringLiteral("zxing-cpp"), cancel).isEmpty());
        QVERIFY(!QFileInfo::exists(legacy + QStringLiteral("/tools/zxing-cpp")));
        // A separately selected tool directory does not move image codecs.
        QSettings().setValue(QStringLiteral("tools/root"), fresh.root());
        ix::ExtensionStorage::initialize();
        QCOMPARE(ix::ToolStore().root(), fresh.root());
        QCOMPARE(ix::PluginStore().root(), legacy);
    }
    void portableToolsTravelWithoutDuplication() {
        const auto home = path(QStringLiteral("portable"));
        const auto tools = home + QStringLiteral("/tools");
        QVERIFY(put(tools + QStringLiteral("/example/payload"), "local tool"));
        std::atomic_bool cancel{false};
        auto error = ix::AppStorage::preparePortable(home, {{QStringLiteral("tools/root"), tools}}, path(QStringLiteral("profile")), cancel);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(get(tools + QStringLiteral("/example/payload")), QByteArray("local tool"));
        QVERIFY(!QFileInfo::exists(home + QStringLiteral("/IxView-data/tools")));
        const auto external = path(QStringLiteral("external-tools"));
        QVERIFY(put(external + QStringLiteral("/example/payload"), "external tool"));
        QVERIFY(put(external + QStringLiteral("/.tool-use/example/job.lock"), "transient"));
        const auto imported = path(QStringLiteral("imported"));
        error = ix::AppStorage::preparePortable(imported, {{QStringLiteral("tools/root"), external}}, path(QStringLiteral("profile")), cancel);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(get(imported + QStringLiteral("/IxView-data/tools/example/payload")), QByteArray("external tool"));
        QVERIFY(!QFileInfo::exists(imported + QStringLiteral("/IxView-data/tools/.tool-use")));
        QCOMPARE(get(external + QStringLiteral("/example/payload")), QByteArray("external tool"));
        QSettings settings(imported + QStringLiteral("/IxView-data/settings/IxView/IxView.ini"), QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("tools/root")).toString(), imported + QStringLiteral("/IxView-data/tools"));
    }
    void rejectsPathsPlatformsCatalogMismatchAndCancellation() {
        const auto file = path(QStringLiteral("tool.ixtool"));
        std::atomic_bool cancel{false};
        ix::ToolStore store;
        for (const auto &bad : {QStringLiteral("../outside"), QStringLiteral("bin/NUL"), QStringLiteral("bin/a."), QStringLiteral("/absolute"), QStringLiteral("bin//reader")}) {
            auto item = manifest(); item.insert(QStringLiteral("entryPoint"), bad);
            QVERIFY(put(file, archive(item, "abc", bad)));
            QVERIFY(!store.install(file, {}, cancel).isEmpty());
        }
        auto wrong = manifest(); wrong.insert(QStringLiteral("os"), QStringLiteral("unknown"));
        QVERIFY(put(file, archive(wrong, "abc")));
        QVERIFY(!store.install(file, {}, cancel).isEmpty());
        auto wrongCase = manifest(); wrongCase.insert(QStringLiteral("entryPoint"), QStringLiteral("bin/Reader"));
        QVERIFY(put(file, archive(wrongCase, "abc")));
        QVERIFY(!store.install(file, {}, cancel).isEmpty());
        auto bytes = archive(manifest(), "abc");
        QVERIFY(put(file, bytes));
        auto expected = manifest(); expected.insert(QStringLiteral("bytes"), bytes.size()); expected.insert(QStringLiteral("sha256"), QString(64, QLatin1Char('0')));
        QVERIFY(!store.install(file, expected, cancel).isEmpty());
        expected.insert(QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
        expected.insert(QStringLiteral("entryPoint"), QStringLiteral("bin/other"));
        QVERIFY(!store.install(file, expected, cancel).isEmpty());
        cancel.store(true);
        QVERIFY(!store.install(file, {}, cancel).isEmpty());
        QVERIFY(store.installed().isEmpty());
        bytes.chop(1); QVERIFY(put(file, bytes));
        QString error; QVERIFY(ix::ToolStore::inspect(file, error).isEmpty()); QVERIFY(!error.isEmpty());
    }
    void preservesCorruptState() {
        const auto state = ix::ExtensionStorage::activeRoot(ix::ExtensionStorage::Domain::Tools) + QStringLiteral("/tools.json");
        const auto file = path(QStringLiteral("tool.ixtool"));
        QVERIFY(put(file, archive(manifest(), "abc")));
        std::atomic_bool cancel{false};
        for (const auto &bytes : {QByteArray("{broken"), QByteArray("{}"), QByteArray("{\"schema\":2,\"packages\":{}}")}) {
            QVERIFY(put(state, bytes));
            QVERIFY(!ix::ToolStore().install(file, {}, cancel).isEmpty());
            QVERIFY(!ix::ToolStore().remove(QStringLiteral("zxing-cpp"), cancel).isEmpty());
            QCOMPARE(get(state), bytes);
        }
    }
    void folderCopyAndRestartBoundary() {
        auto source = ix::ExtensionStorage::activeRoot(ix::ExtensionStorage::Domain::Tools);
        QVERIFY(put(source + QStringLiteral("/tools/example/data"), "payload"));
        QVERIFY(put(source + QStringLiteral("/.tool-use/example/job.lock"), "transient"));
        const auto target = path(QStringLiteral("new-extensions"));
        std::atomic_bool cancel{true};
        QVERIFY(!ix::ExtensionStorage::prepareRoot(target, cancel, ix::ExtensionStorage::Domain::Tools).isEmpty());
        QVERIFY(!QFileInfo::exists(target));
        cancel.store(false);
        QVERIFY(!ix::ExtensionStorage::prepareRoot(source + QStringLiteral("/nested"), cancel, ix::ExtensionStorage::Domain::Tools).isEmpty());
        QVERIFY(ix::ExtensionStorage::prepareRoot(target, cancel, ix::ExtensionStorage::Domain::Tools).isEmpty());
        QCOMPARE(get(target + QStringLiteral("/tools/example/data")), QByteArray("payload"));
        QVERIFY(!QFileInfo::exists(target + QStringLiteral("/.tool-use")));
        QCOMPARE(get(source + QStringLiteral("/tools/example/data")), QByteArray("payload"));
        QVERIFY(ix::ExtensionStorage::restartPending(ix::ExtensionStorage::Domain::Tools));
        QCOMPARE(ix::ExtensionStorage::activeRoot(ix::ExtensionStorage::Domain::Tools), source);
        QVERIFY(!ix::ToolStore().setEnabled(QStringLiteral("zxing-cpp"), true).isEmpty());
        ix::ExtensionStorage::initialize();
        QCOMPARE(ix::ExtensionStorage::activeRoot(ix::ExtensionStorage::Domain::Tools), target);
        QVERIFY(!ix::ExtensionStorage::restartPending(ix::ExtensionStorage::Domain::Tools));
        const auto occupied = path(QStringLiteral("occupied")); QVERIFY(put(occupied + QStringLiteral("/keep"), "original"));
        QVERIFY(!ix::ExtensionStorage::prepareRoot(occupied, cancel, ix::ExtensionStorage::Domain::Tools).isEmpty());
        QCOMPARE(get(occupied + QStringLiteral("/keep")), QByteArray("original"));
    }
    void portableImportIncludesCustomExtensions() {
        const auto custom = path(QStringLiteral("external-tools"));
        QVERIFY(put(custom + QStringLiteral("/tools/example/bin/tool"), "executable bytes"));
        QVERIFY(put(custom + QStringLiteral("/.tool-use/example/job.lock"), "transient"));
        QSettings().setValue(QStringLiteral("extensions/root"), custom);
        QVariantMap values{{QStringLiteral("extensions/root"), custom}};
        std::atomic_bool cancel{false};
        const auto home = path(QStringLiteral("portable"));
        const auto error = ix::AppStorage::preparePortable(home, values, path(QStringLiteral("profile")), cancel);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(get(home + QStringLiteral("/IxView-data/extensions/tools/example/bin/tool")), QByteArray("executable bytes"));
        QVERIFY(!QFileInfo::exists(home + QStringLiteral("/IxView-data/extensions/.tool-use")));
        QVERIFY(QFileInfo::exists(custom + QStringLiteral("/tools/example/bin/tool")));
        QSettings ini(home + QStringLiteral("/IxView-data/settings/IxView/IxView.ini"), QSettings::IniFormat);
        QCOMPARE(ini.value(QStringLiteral("extensions/root")).toString(), home + QStringLiteral("/IxView-data/extensions"));
#ifdef Q_OS_WIN
        // Reproduce an actual Windows rename denial, then release the handle.
        // A short-lived sharing lock must not discard a completed import.
        const auto prepared = path(QStringLiteral("prepared"));
        const auto published = path(QStringLiteral("published"));
        QVERIFY(put(prepared + QStringLiteral("/keep"), "original"));
        const auto native = QDir::toNativeSeparators(prepared);
        HANDLE held = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        QVERIFY(!QDir().rename(prepared, published));
        std::atomic_bool entered{false};
        auto job = QtConcurrent::run([&] { entered.store(true); return ix::detail::commitDirectory(prepared, published, cancel); });
        while (!entered.load()) QThread::yieldCurrentThread();
        QTest::qWait(40);
        CloseHandle(held);
        job.waitForFinished();
        QVERIFY(job.result());
        QCOMPARE(get(published + QStringLiteral("/keep")), QByteArray("original"));
        QVERIFY(!QFileInfo::exists(prepared));
        QVERIFY(put(prepared + QStringLiteral("/keep"), "second"));
        QVERIFY(!ix::detail::commitDirectory(prepared, published, cancel));
        QCOMPARE(get(prepared + QStringLiteral("/keep")), QByteArray("second"));
        QCOMPARE(get(published + QStringLiteral("/keep")), QByteArray("original"));
        held = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        QVERIFY(!ix::detail::commitDirectory(prepared, path(QStringLiteral("blocked")), cancel));
        CloseHandle(held);
        QVERIFY(QFileInfo::exists(prepared));
        cancel.store(true);
        QVERIFY(!ix::detail::commitDirectory(prepared, path(QStringLiteral("cancelled")), cancel));
        QVERIFY(QFileInfo::exists(prepared));
#endif
    }
    void portableImportRejectsAppInsideCustomExtensions() {
        const auto custom = path(QStringLiteral("external-tools"));
        QVERIFY(put(custom + QStringLiteral("/original"), "preserve"));
        const auto home = custom + QStringLiteral("/viewer");
        std::atomic_bool cancel{false};
        const auto error = ix::AppStorage::preparePortable(home,
            {{QStringLiteral("extensions/root"), custom}}, path(QStringLiteral("profile")), cancel);
        QVERIFY(!error.isEmpty());
        QCOMPARE(get(custom + QStringLiteral("/original")), QByteArray("preserve"));
        QVERIFY(!QFileInfo::exists(home + QStringLiteral("/portable.mode")));
        QVERIFY(!QFileInfo::exists(home + QStringLiteral("/IxView-data")));
        QVERIFY(QDir(home).entryList({QStringLiteral(".ixview-profile-*")}, QDir::Dirs | QDir::Hidden).isEmpty());
    }
    void realQrReaderAndNoCode() {
        installReal({QStringLiteral("zxing-cpp")});
        const auto image = qrImage();
        const auto file = path(QStringLiteral("input.ppm")); QVERIFY(image.save(file, "ppm"));
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
        environment.insert(QStringLiteral("PATH"), environment.value(QStringLiteral("SystemRoot")) + QStringLiteral("/System32"));
#endif
        process.setProcessEnvironment(environment); process.setWorkingDirectory(folder_);
        process.start(ix::ToolStore().executable(QStringLiteral("zxing-cpp")), {QStringLiteral("-json"), QStringLiteral("input.ppm")});
        QVERIFY(process.waitForFinished(30000)); QCOMPARE(process.exitCode(), 0);
        auto codes = QJsonDocument::fromJson(process.readAllStandardOutput()).array();
        QCOMPARE(codes.size(), 1); QCOMPARE(codes[0].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("ABCDEF"));
        // Independent linear Code 39 fixture: start, A, B, C, stop.
        QImage barcode(300, 150, QImage::Format_RGB32); barcode.fill(Qt::white);
        {
            QPainter painter(&barcode); int x = 30;
            for (int pattern : {0x094, 0x109, 0x049, 0x148, 0x094}) {
                for (int bit = 8; bit >= 0; --bit) {
                    const int width = ((pattern >> bit) & 1) ? 6 : 3;
                    if (bit % 2 == 0) painter.fillRect(x, 25, width, 100, Qt::black);
                    x += width;
                }
                x += 3;
            }
        }
        QVERIFY(barcode.save(file, "ppm"));
        process.start(ix::ToolStore().executable(QStringLiteral("zxing-cpp")), {QStringLiteral("-json"), QStringLiteral("input.ppm")});
        QVERIFY(process.waitForFinished(30000)); QCOMPARE(process.exitCode(), 0);
        codes = QJsonDocument::fromJson(process.readAllStandardOutput()).array();
        QCOMPARE(codes.size(), 1); QCOMPARE(codes[0].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("ABC"));
        QImage blank(100, 100, QImage::Format_RGB32); blank.fill(Qt::white); QVERIFY(blank.save(file, "ppm"));
        process.start(ix::ToolStore().executable(QStringLiteral("zxing-cpp")), {QStringLiteral("-json"), QStringLiteral("input.ppm")});
        QVERIFY(process.waitForFinished(30000)); QCOMPARE(process.exitCode(), 0);
        QCOMPARE(QJsonDocument::fromJson(process.readAllStandardOutput()).array().size(), 0);
    }
    void managerSectionsInstallAndDisable() {
#ifdef IX_TEST_EXTENSIONS_MODULE
        QLibrary library(ix::optionalModulePath(QStringLiteral(IX_TEST_EXTENSIONS_MODULE)));
        library.setLoadHints(QLibrary::PreventUnloadHint);
        auto open = reinterpret_cast<void (*)(QWidget *)>(library.resolve("ixOpenExtensionManager"));
        QVERIFY2(open, qPrintable(library.errorString()));
        bool complete = false; int stage = 0;
        QTimer timer; timer.setInterval(30);
        connect(&timer, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *tabs = dialog->findChild<QTabWidget *>(QStringLiteral("extensionSections"));
            if (!tabs || tabs->count() != 2) return;
            auto *table = dialog->findChild<QTableWidget *>(QStringLiteral("functionExtensionTable"));
            auto *install = dialog->findChild<QPushButton *>(QStringLiteral("installCatalogExtension"));
            auto *toggle = dialog->findChild<QPushButton *>(QStringLiteral("toggleExtension"));
            auto *remove = dialog->findChild<QPushButton *>(QStringLiteral("removeExtension"));
            if (!table || !install || !toggle || !remove) return;
            tabs->setCurrentIndex(1);
            if (stage == 0) {
                if (table->rowCount() != 7) { dialog->reject(); return; }
                for (int row = 0; row < table->rowCount(); ++row) {
                    auto item = table->item(row, 0)->data(Qt::UserRole).toJsonObject();
                    if (item.value(QStringLiteral("id")).toString() == QStringLiteral("zxing-cpp")) {
                        table->selectRow(row); stage = 1; install->click(); break;
                    }
                }
            } else if (stage == 1 && table->isEnabled() && !ix::ToolStore().executable(QStringLiteral("zxing-cpp")).isEmpty()) {
                // reload replaces selection; find the newly installed row.
                for (int row = 0; row < table->rowCount(); ++row) {
                    auto item = table->item(row, 0)->data(Qt::UserRole).toJsonObject();
                    if (item.value(QStringLiteral("id")).toString() == QStringLiteral("zxing-cpp") && item.value(QStringLiteral("origin")).toString() == QStringLiteral("installed")) {
                        table->selectRow(row);
                        dialog->grab().save(QStringLiteral(IX_EXTENSION_OUTPUT "/function-extensions.png"));
                        toggle->click();
                        if (!ix::ToolStore().executable(QStringLiteral("zxing-cpp")).isEmpty()) { dialog->reject(); return; }
                        for (int r = 0; r < table->rowCount(); ++r)
                            if (table->item(r, 0)->data(Qt::UserRole).toJsonObject().value(QStringLiteral("origin")).toString() == QStringLiteral("installed")) table->selectRow(r);
                        stage = 2; remove->click(); break;
                    }
                }
            } else if (stage == 2 && table->isEnabled() && ix::ToolStore().installed().isEmpty()) {
                complete = !QFileInfo::exists(ix::ToolStore().root() + QStringLiteral("/tools/zxing-cpp"));
                dialog->reject();
            }
        });
        QTimer deadline; deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, qApp, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
        timer.start(); deadline.start(15000);
        open(nullptr);
        QVERIFY(complete);
#endif
    }
    void realQrDialogUsesSelectionAndPreservesPixels() {
        installReal({QStringLiteral("zxing-cpp")});
        QImage image(600, 340, QImage::Format_RGB32); image.fill(Qt::white);
        { QPainter painter(&image); painter.drawImage(300, 20, qrImage()); }
        const auto original = image;
        const auto imagePath = path(QStringLiteral("qr-selection.png"));
        QVERIFY(image.save(imagePath));
        ix::MainWindow window;
        window.show();
        window.openPath(imagePath);
        QTRY_COMPARE(window.document().path, imagePath);
        QTRY_VERIFY(!window.isBusy());
        auto *view = window.findChild<ix::ImageView *>();
        QVERIFY(view);
        view->setCentered(false);
        view->setZoom(1.0);
        auto *surface = view->viewport();
        const auto origin = view->visibleImageRect().topLeft();
        const auto start = origin + QPoint(300, 20), end = origin + QPoint(589, 309);
        QTest::mousePress(surface, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(surface, end);
        QTest::mouseRelease(surface, Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(view->selection(), QRect(300, 20, 290, 290));
        const auto selected = view->selection();
        QString result; bool completed = false, menuOpened = false;
        QTimer timer; timer.setInterval(25);
        connect(&timer, &QTimer::timeout, this, [&] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                if (menu->objectName() == QStringLiteral("imageRecognitionMenu")) {
                    menuOpened = true;
                    auto *action = window.findChild<QAction *>(QStringLiteral("zxing-cpp"));
                    menu->close();
                    QTimer::singleShot(0, &window, [action] { action->trigger(); });
                    return;
                }
            }
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *output = dialog->findChild<QPlainTextEdit *>(QStringLiteral("toolResult"));
            if (output && !output->toPlainText().isEmpty()) {
                result = output->toPlainText(); completed = true;
                QVERIFY(dialog->findChild<QPushButton *>(QStringLiteral("copyToolResult"))->isEnabled());
                QVERIFY(dialog->findChild<QPushButton *>(QStringLiteral("saveToolResult"))->isEnabled());
                dialog->grab().save(QStringLiteral(IX_EXTENSION_OUTPUT "/qr-tool.png")); dialog->reject();
            }
        });
        timer.start();
        QTimer deadline; deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, qApp, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
        deadline.start(15000);
        QTest::mouseClick(surface, Qt::RightButton, Qt::NoModifier, origin + selected.center());
        QTRY_VERIFY(completed);
        QVERIFY(menuOpened); QVERIFY(result.contains(QStringLiteral("ABCDEF")));
        QCOMPARE(view->selection(), selected); QCOMPARE(window.document().pixels, original); QCOMPARE(image, original);
        QSignalSpy menus(view, &ix::ImageView::recognitionMenuRequested);
        const auto center = origin + selected.center();
        QTest::mousePress(surface, Qt::RightButton, Qt::NoModifier, center);
        QTest::mouseMove(surface, center + QPoint(-20, 5));
        QTest::mouseRelease(surface, Qt::RightButton, Qt::NoModifier, center + QPoint(-20, 5));
        QCOMPARE(menus.size(), 0);
        QCOMPARE(view->selection().topLeft(), selected.topLeft() + QPoint(-20, 5));
        window.close();
    }
    void realOcrDialogAndLanguages() {
        installReal({QStringLiteral("tesseract-ocr"), QStringLiteral("tessdata-eng"), QStringLiteral("tessdata-deu"), QStringLiteral("tessdata-fra"), QStringLiteral("tessdata-kor"), QStringLiteral("tessdata-jpn")});
        QCOMPARE(ix::ToolStore().languages().size(), 5);
        QString acquireError;
        auto use = ix::ToolStore().acquire(QStringLiteral("tesseract-ocr"), {QStringLiteral("eng")}, acquireError);
        QVERIFY2(use, qPrintable(acquireError));
        const std::atomic_bool keepGoing{false};
        QVERIFY(!ix::ToolStore().remove(QStringLiteral("tessdata-eng"), keepGoing).isEmpty());
        QVERIFY(ix::ToolStore().remove(QStringLiteral("tessdata-fra"), keepGoing).isEmpty());
        use.reset();
        installReal({QStringLiteral("tessdata-fra")});
        QVERIFY(ix::ToolStore().setEnabled(QStringLiteral("tessdata-deu"), false).isEmpty());
        QCOMPARE(ix::ToolStore().languages().size(), 4);
        QSettings().setValue(QStringLiteral("tools/ocrLanguages"), QStringList{QStringLiteral("eng")});
        QImage image(1400, 240, QImage::Format_RGB32); image.fill(Qt::white);
        { QPainter painter(&image); painter.setPen(Qt::black); QFont font(QStringLiteral("Arial")); font.setPixelSize(72); painter.setFont(font); painter.drawText(60, 140, QStringLiteral("Hello uxzView 12345")); }
        QString result; bool completed = false;
        QTimer timer; timer.setInterval(25);
        connect(&timer, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *output = dialog->findChild<QPlainTextEdit *>(QStringLiteral("toolResult"));
            if (output && !output->toPlainText().isEmpty()) {
                result = output->toPlainText(); completed = true;
                QVERIFY(dialog->findChild<QPushButton *>(QStringLiteral("copyToolResult"))->isEnabled());
                QVERIFY(dialog->findChild<QPushButton *>(QStringLiteral("saveToolResult"))->isEnabled());
                dialog->grab().save(QStringLiteral(IX_EXTENSION_OUTPUT "/ocr-tool.png")); dialog->reject();
            }
        });
        timer.start();
        QTimer deadline; deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, qApp, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
        deadline.start(30000);
        ix::imageToolDialog(nullptr, QStringLiteral("tesseract-ocr"), image, {});
        QVERIFY(completed); QVERIFY2(result.contains(QStringLiteral("12345")) && result.contains(QStringLiteral("Hello")), qPrintable(result));
        QVERIFY(QDir(path(QStringLiteral("cache/tool-jobs"))).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }
    void recognitionPreparationPreservesPixelsAndFiles() {
        QImage image(3, 2, QImage::Format_ARGB32); image.fill(Qt::transparent);
        image.setPixelColor(1, 0, Qt::red);
        const auto original = image.copy();
        const auto directory = path(QStringLiteral("input-job"));
        QVERIFY(QDir().mkpath(directory));
        const auto model = path(QStringLiteral("eng.traineddata"));
        QVERIFY(put(model, "model fixture"));
        std::atomic_bool cancel{false};
        const QList<ix::RecognitionLanguage> languages{{QStringLiteral("eng"), model}};
        QVERIFY(ix::prepareRecognitionInput(image, {1, 0, 1, 2}, directory, languages, cancel).isEmpty());
        const QImage input(directory + QStringLiteral("/input.ppm"));
        QCOMPARE(input.size(), QSize(1, 2));
        QCOMPARE(input.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(input.pixelColor(0, 1), QColor(Qt::white));
        QCOMPARE(image, original);
        QCOMPARE(get(directory + QStringLiteral("/tessdata/eng.traineddata")), get(model));
        const auto before = get(directory + QStringLiteral("/input.ppm"));
        QVERIFY(!ix::prepareRecognitionInput(image, image.rect(), directory, {}, cancel).isEmpty());
        QCOMPARE(get(directory + QStringLiteral("/input.ppm")), before);
        const auto empty = path(QStringLiteral("cancelled-job"));
        QVERIFY(QDir().mkpath(empty));
        cancel.store(true);
        QVERIFY(!ix::prepareRecognitionInput(image, image.rect(), empty, {}, cancel).isEmpty());
        QVERIFY(QDir(empty).entryList(QDir::Files).isEmpty());
        cancel.store(false);
        QVERIFY(!ix::prepareRecognitionInput(image, QRect(20, 20, 2, 2), empty, {}, cancel).isEmpty());
        QVERIFY(!ix::prepareRecognitionInput(image, image.rect(), empty,
            {{QStringLiteral("../eng"), model}}, cancel).isEmpty());
        QVERIFY(QDir(empty).entryList(QDir::Files).isEmpty());
    }
    void realToolRestartTracksOnlyToolsFolder() {
        installReal({QStringLiteral("zxing-cpp")});
        QSettings().setValue(QStringLiteral("extensions/root"), path(QStringLiteral("other-codecs")));
        QVERIFY(ix::ExtensionStorage::restartPending());
        QVERIFY(!ix::ExtensionStorage::restartPending(ix::ExtensionStorage::Domain::Tools));
        bool completed = false;
        QTimer poll; poll.setInterval(10);
        connect(&poll, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (qobject_cast<QMessageBox *>(dialog)) { dialog->reject(); return; }
            auto *output = dialog->findChild<QPlainTextEdit *>(QStringLiteral("toolResult"));
            if (output && output->toPlainText().contains(QStringLiteral("ABCDEF"))) {
                completed = true; dialog->reject();
            }
        });
        QTimer deadline; deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, qApp, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        poll.start(); deadline.start(15000);
        ix::imageToolDialog(nullptr, QStringLiteral("zxing-cpp"), qrImage(), {});
        QVERIFY(completed);
        poll.stop();
        QSettings().remove(QStringLiteral("extensions/root"));
        QSettings().setValue(QStringLiteral("tools/root"), path(QStringLiteral("other-tools")));
        bool restartMessage = false;
        QTimer::singleShot(0, qApp, [&] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            restartMessage = message && message->text().contains(QStringLiteral("neu starten"));
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        ix::imageToolDialog(nullptr, QStringLiteral("zxing-cpp"), qrImage(), {});
        QVERIFY(restartMessage);
    }
    void realToolCancellationCleansTemporaryInput() {
        QImage image(4096, 4096, QImage::Format_RGB32); image.fill(Qt::white);
#ifdef IX_TEST_EXTENSIONS_MODULE
        bool cancelledSetup = false;
        QTimer::singleShot(0, qApp, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                cancelledSetup = dialog->objectName() == QStringLiteral("extensionManagerDialog");
                dialog->reject();
            }
        });
        ix::imageToolDialog(nullptr, QStringLiteral("zxing-cpp"), image, {});
        QVERIFY(cancelledSetup);
        QVERIFY(ix::ToolStore().installed().isEmpty());
#else
        installReal({QStringLiteral("zxing-cpp")});
#endif
        bool requested = false, completed = false, setupOpened = false, installing = false;
        QTimer timer; timer.setInterval(5);
        connect(&timer, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (dialog->objectName() == QStringLiteral("extensionManagerDialog")) {
                auto *tabs = dialog->findChild<QTabWidget *>(QStringLiteral("extensionSections"));
                auto *table = dialog->findChild<QTableWidget *>(QStringLiteral("functionExtensionTable"));
                if (!tabs || !table) { dialog->reject(); return; }
                if (installing) {
                    if (table->isEnabled() && !ix::ToolStore().executable(QStringLiteral("zxing-cpp")).isEmpty()) dialog->reject();
                    return;
                }
                if (table->currentRow() < 0) { dialog->reject(); return; }
                const auto item = table->item(table->currentRow(), 0)->data(Qt::UserRole).toJsonObject();
                setupOpened = tabs->currentIndex() == 1 && item.value(QStringLiteral("id")).toString() == QStringLiteral("zxing-cpp");
                if (!installing && setupOpened) {
                    installing = true;
                    dialog->findChild<QPushButton *>(QStringLiteral("installCatalogExtension"))->click();
                }
                return;
            }
            auto *cancel = dialog->findChild<QPushButton *>(QStringLiteral("cancelImageTool"));
            auto *run = dialog->findChild<QPushButton *>(QStringLiteral("runImageTool"));
            auto *status = dialog->findChild<QLabel *>(QStringLiteral("imageToolStatus"));
            auto *manage = dialog->findChild<QPushButton *>(QStringLiteral("manageToolExtensions"));
            if (!requested && cancel && cancel->isEnabled()) {
                QVERIFY(manage && !manage->isEnabled());
                QVERIFY(!dialog->findChild<QPushButton *>(QStringLiteral("copyToolResult"))->isEnabled());
                requested = true; cancel->click();
            } else if (requested && run && run->isEnabled() && status) {
                completed = status->text().contains(QStringLiteral("Abgebrochen")); dialog->reject();
            }
        });
        QTimer deadline; deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, qApp, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
        timer.start(); deadline.start(15000);
        ix::imageToolDialog(nullptr, QStringLiteral("zxing-cpp"), image, {});
#ifdef IX_TEST_EXTENSIONS_MODULE
        QVERIFY(setupOpened);
#endif
        QVERIFY(requested); QVERIFY(completed);
        const std::atomic_bool keepGoing{false};
        QVERIFY(ix::ToolStore().remove(QStringLiteral("zxing-cpp"), keepGoing).isEmpty());
        QVERIFY(QDir(path(QStringLiteral("cache/tool-jobs"))).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }

};
QTEST_MAIN(ExtensionTests)
#include "extensions_ixview.moc"
