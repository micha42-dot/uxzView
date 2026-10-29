// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/app_storage.h"
#include "core/batch_pipeline.h"
#include "core/file_operations.h"
#include "core/file_revision.h"
#include "core/image_catalog.h"
#include "core/plugin_store.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>
namespace {
bool put(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray get(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace
class StorageTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_{QStringLiteral(IX_STORAGE_OUTPUT "/run-storage-XXXXXX")};
    int number_ = 0;
    QString root_;
    QString path(const QString &name) const { return root_ + QLatin1Char('/') + name; }
  private slots:
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxView"));
        QCoreApplication::setApplicationName(QStringLiteral("IxView"));
    }
    void init() {
        root_ = temporary_.path() + QStringLiteral("/%1").arg(++number_);
        QVERIFY(QDir().mkpath(root_));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path(QStringLiteral("normal-settings")));
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, path(QStringLiteral("system-settings")));
        qApp->setProperty("ixStoragePortable", false);
        qApp->setProperty("ixStorageData", QVariant());
        qApp->setProperty("ixStorageCache", QVariant());
    }
    void normalModeKeepsSettingsLocation() {
        const auto file = QSettings().fileName();
        const auto format = QSettings::defaultFormat();
        QVERIFY(ix::AppStorage::initialize(path(QStringLiteral("app"))).isEmpty());
        QVERIFY(!ix::AppStorage::isPortable());
        QCOMPARE(QSettings().fileName(), file);
        QCOMPARE(QSettings::defaultFormat(), format);
        QVERIFY(!QFileInfo::exists(path(QStringLiteral("app/IxView-data"))));
    }
    void settingsAreIsolatedAndTyped() {
        const auto normal = QSettings().fileName();
        {
            QSettings settings;
            settings.setValue(QStringLiteral("normalOnly"), 77);
            settings.sync();
        }
        const auto home = path(QStringLiteral("app-ä-日本"));
        QVERIFY(ix::AppStorage::initialize(home, true).isEmpty());
        QVERIFY(ix::AppStorage::isPortable());
        QVERIFY(ix::AppStorage::portableRequested(home));
        QVERIFY(!QSettings().contains(QStringLiteral("normalOnly")));
        const QColor color(QStringLiteral("#315579"));
        {
            QSettings settings;
            settings.setValue(QStringLiteral("view/background"), color);
            settings.setValue(QStringLiteral("capture/shortcut"), QStringLiteral("Ctrl+Shift+F10"));
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
            QCOMPARE(settings.fileName(), home + QStringLiteral("/IxView-data/settings/IxView/IxView.ini"));
        }
        QCOMPARE(QSettings().value(QStringLiteral("view/background")).value<QColor>(), color);
        QSettings original(normal, QSettings::IniFormat);
        QCOMPARE(original.value(QStringLiteral("normalOnly")).toInt(), 77);
        QVERIFY(!original.contains(QStringLiteral("capture/shortcut")));
    }
    void importsOwnedDataWithoutChangingOriginals() {
        const auto home = path(QStringLiteral("app")), source = path(QStringLiteral("user-data"));
        QVERIFY(put(source + QStringLiteral("/profiles/test.json"), QByteArray("{\"name\":\"test\"}")));
        QVERIFY(put(source + QStringLiteral("/extensions/package/codec.bin"), QByteArray("codec bytes")));
        QVERIFY(put(source + QStringLiteral("/file-journal/trash/backup"), QByteArray("original pixels")));
        const auto journal = QJsonDocument(QJsonObject{{QStringLiteral("backup"), source + QStringLiteral("/file-journal/trash/backup")}}).toJson();
        QVERIFY(put(source + QStringLiteral("/file-journal/entries/entry.json"), journal));
        const QVariantMap values{{QStringLiteral("ui/language"), QStringLiteral("fr")},
                                 {QStringLiteral("resize/presets"), QVariantMap{{QStringLiteral("small"), 800}}}};
        std::atomic_bool cancel = false;
        QVERIFY(ix::AppStorage::preparePortable(home, values, source, cancel).isEmpty());
        QVERIFY(!ix::AppStorage::isPortable()); // Switching is deferred to restart.
        const auto data = home + QStringLiteral("/IxView-data");
        QCOMPARE(get(data + QStringLiteral("/extensions/package/codec.bin")), QByteArray("codec bytes"));
        QVERIFY(!QFileInfo::exists(data + QStringLiteral("/file-journal")));
        QCOMPARE(get(source + QStringLiteral("/file-journal/trash/backup")), QByteArray("original pixels"));
        QCOMPARE(get(source + QStringLiteral("/file-journal/entries/entry.json")), journal);
        QVERIFY(ix::AppStorage::initialize(home).isEmpty());
        QCOMPARE(QSettings().value(QStringLiteral("ui/language")).toString(), QStringLiteral("fr"));
        QCOMPARE(QSettings().value(QStringLiteral("resize/presets")).toMap().value(QStringLiteral("small")).toInt(), 800);
    }
    void movedFolderRetainsRatingsAndOriginals() {
        const auto home = path(QStringLiteral("before")), moved = path(QStringLiteral("after"));
        const auto source = path(QStringLiteral("user-data"));
        const auto image = home + QStringLiteral("/photos/one.json");
        const auto original = QJsonDocument(QJsonObject{{QStringLiteral("path"), image}}).toJson();
        QVERIFY(put(image, original));
        ix::ImageCatalog catalog(source + QStringLiteral("/catalog.json"));
        QVERIFY(catalog.setAnnotation(image, 4, QStringLiteral("green")).isEmpty());
        auto records = catalog.records();
        auto record = records.value(image).toObject();
        record.insert(QStringLiteral("metadataText"), image);
        records.insert(image, record);
        QVERIFY(put(catalog.path(), QJsonDocument(records).toJson()));
        const QVariantMap settings{{QStringLiteral("files/recent"), QStringList{image}},
                                   {QStringLiteral("files/directory"), home + QStringLiteral("/photos")},
                                   {QStringLiteral("extensions/catalog"), QUrl::fromLocalFile(home + QStringLiteral("/offline/catalog.json")).toString()}};
        std::atomic_bool cancel = false;
        QVERIFY(ix::AppStorage::preparePortable(home, settings, source, cancel).isEmpty());
        QVERIFY(ix::AppStorage::initialize(home).isEmpty());
        // The same-process fixture may still have a briefly pending Windows
        // file close; real deployment probes also move after process exit.
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(moved).isDir() || QDir().rename(home, moved), 3000);
        QVERIFY(!QFileInfo::exists(home));
        QVERIFY(ix::AppStorage::initialize(moved).isEmpty());
        const auto destination = moved + QStringLiteral("/photos/one.json");
        QCOMPARE(QSettings().value(QStringLiteral("files/recent")).toStringList(), QStringList{destination});
        QCOMPARE(QUrl(QSettings().value(QStringLiteral("extensions/catalog")).toString()).toLocalFile(),
                 moved + QStringLiteral("/offline/catalog.json"));
        QCOMPARE(ix::ImageCatalog().record(destination).value(QStringLiteral("rating")).toInt(), 4);
        QCOMPARE(ix::ImageCatalog().record(destination).value(QStringLiteral("metadataText")).toString(), image);
        QCOMPARE(get(destination), original);
        QVERIFY(!QFileInfo::exists(moved + QStringLiteral("/IxView-data/file-journal")));
    }
    void cancellationLeavesSourceAndModeIntact() {
        const auto source = path(QStringLiteral("source")), home = path(QStringLiteral("app"));
        QVERIFY(put(source + QStringLiteral("/profiles/test.json"), QByteArray("{}")));
        std::atomic_bool cancel = true;
        QVERIFY(!ix::AppStorage::preparePortable(home, {}, source, cancel).isEmpty());
        QVERIFY(!ix::AppStorage::portableRequested(home));
        QVERIFY(!QFileInfo::exists(home + QStringLiteral("/IxView-data")));
        QCOMPARE(get(source + QStringLiteral("/profiles/test.json")), QByteArray("{}"));
        QLockFile lock(source + QStringLiteral("/catalog.json.lock"));
        QVERIFY(lock.tryLock(0));
        cancel.store(false);
        QVERIFY(!ix::AppStorage::preparePortable(home, {}, source, cancel).isEmpty());
        QVERIFY(!ix::AppStorage::portableRequested(home));
        QVERIFY(!QFileInfo::exists(home + QStringLiteral("/IxView-data")));
    }
    void existingPortableProfileIsNeverOverwritten() {
        const auto home = path(QStringLiteral("app"));
        QVERIFY(ix::AppStorage::initialize(home, true).isEmpty());
        {
            QSettings settings;
            settings.setValue(QStringLiteral("retained"), 42);
            settings.sync();
        }
        QVERIFY(ix::AppStorage::disablePortable(home).isEmpty());
        QVERIFY(!ix::AppStorage::portableRequested(home));
        QVERIFY(ix::AppStorage::isPortable()); // Remains active until restart.
        std::atomic_bool cancel = false;
        QVERIFY(ix::AppStorage::preparePortable(home, {{QStringLiteral("retained"), 99}}, path(QStringLiteral("source")), cancel).isEmpty());
        QCOMPARE(QSettings().value(QStringLiteral("retained")).toInt(), 42);
        const auto unknown = path(QStringLiteral("unknown"));
        QVERIFY(put(unknown + QStringLiteral("/IxView-data/unrelated.txt"), QByteArray("keep")));
        QVERIFY(!ix::AppStorage::preparePortable(unknown, {}, path(QStringLiteral("source")), cancel).isEmpty());
        QCOMPARE(get(unknown + QStringLiteral("/IxView-data/unrelated.txt")), QByteArray("keep"));
        QVERIFY(!ix::AppStorage::portableRequested(unknown));
    }
    void corruptLocationFailsWithoutFallback() {
        const auto home = path(QStringLiteral("app"));
        const auto originalSettings = QSettings().fileName();
        QVERIFY(put(home + QStringLiteral("/portable.mode"), QByteArray()));
        QVERIFY(put(home + QStringLiteral("/IxView-data/location.json"), QByteArray("broken")));
        QVERIFY(!ix::AppStorage::initialize(home).isEmpty());
        QVERIFY(!ix::AppStorage::isPortable());
        QCOMPARE(QSettings().fileName(), originalSettings);
        QCOMPARE(get(home + QStringLiteral("/IxView-data/location.json")), QByteArray("broken"));
        const auto blocked = path(QStringLiteral("blocked"));
        QVERIFY(put(blocked + QStringLiteral("/IxView-data"), QByteArray("not a directory")));
        QVERIFY(!ix::AppStorage::initialize(blocked, true).isEmpty());
        QVERIFY(!ix::AppStorage::portableRequested(blocked));
    }
    void sourceRecursionAndCorruptJsonAreRejected() {
        std::atomic_bool cancel = false;
        const auto source = path(QStringLiteral("user"));
        QVERIFY(QDir().mkpath(source));
        QVERIFY(!ix::AppStorage::preparePortable(source + QStringLiteral("/app"), {}, source, cancel).isEmpty());
        QVERIFY(put(source + QStringLiteral("/catalog.json"), QByteArray("broken")));
        const auto home = path(QStringLiteral("app"));
        QVERIFY(!ix::AppStorage::preparePortable(home, {}, source, cancel).isEmpty());
        QVERIFY(!ix::AppStorage::portableRequested(home));
        QVERIFY(!QFileInfo::exists(home + QStringLiteral("/IxView-data")));
        QCOMPARE(get(source + QStringLiteral("/catalog.json")), QByteArray("broken"));
    }
    void externalPathsAndSimilarPrefixesStayUnchanged() {
        const auto source = path(QStringLiteral("user")), home = path(QStringLiteral("app"));
        const auto external = path(QStringLiteral("external/photo.png"));
        const auto similar = source + QStringLiteral("-other/watermark.png");
        const auto profile = QJsonDocument(QJsonObject{{QStringLiteral("image"), similar},
            {QStringLiteral("name"), source + QStringLiteral("/profile label")},
            {QStringLiteral("text"), source + QStringLiteral("/literal watermark text")}}).toJson();
        QVERIFY(put(source + QStringLiteral("/profiles/test.json"), profile));
        std::atomic_bool cancel = false;
        QVERIFY(ix::AppStorage::preparePortable(home, {{QStringLiteral("recent"), QStringList{external, similar}}}, source, cancel).isEmpty());
        QVERIFY(ix::AppStorage::initialize(home).isEmpty());
        QCOMPARE(QSettings().value(QStringLiteral("recent")).toStringList(), QStringList({external, similar}));
        QCOMPARE(get(home + QStringLiteral("/IxView-data/profiles/test.json")), profile);
        const auto volumeRoot = QDir::rootPath();
        const auto key = QDir(volumeRoot).filePath(QStringLiteral("photos/portable.png"));
        QVERIFY(put(home + QStringLiteral("/IxView-data/catalog.json"),
                    QJsonDocument(QJsonObject{{key, QJsonObject{{QStringLiteral("rating"), 3}}}}).toJson()));
        QVERIFY(put(home + QStringLiteral("/IxView-data/location.json"),
                    QJsonDocument(QJsonObject{{QStringLiteral("schema"), 1}, {QStringLiteral("home"), volumeRoot}}).toJson()));
        QVERIFY(ix::AppStorage::initialize(home).isEmpty());
        QCOMPARE(ix::ImageCatalog().record(home + QStringLiteral("/photos/portable.png"))
                     .value(QStringLiteral("rating")).toInt(), 3);
    }
    void everyDefaultBackendUsesPortableData() {
        const auto home = path(QStringLiteral("app"));
        QVERIFY(ix::AppStorage::initialize(home, true).isEmpty());
        const auto data = home + QStringLiteral("/IxView-data");
        QCOMPARE(ix::ImageCatalog().path(), data + QStringLiteral("/catalog.json"));
        QCOMPARE(ix::PluginStore().root(), data + QStringLiteral("/extensions"));
        QVERIFY(ix::PluginStore().setBuiltinEnabled({QStringLiteral("bmp")}, false).isEmpty());
        QVERIFY(QFileInfo::exists(data + QStringLiteral("/extensions/state.json")));
        const auto image = path(QStringLiteral("image.png"));
        QImage pixels(4, 3, QImage::Format_RGB32);
        pixels.fill(Qt::red);
        QVERIFY(pixels.save(image));
        ix::PipelineJob job;
        job.profile.steps = {{QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        std::atomic_bool cancel = false;
        const auto result = ix::BatchPipeline::preview(job, image, cancel);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(QFileInfo::exists(data + QStringLiteral("/cache/previews")));
        QCOMPARE(QDir(data + QStringLiteral("/cache/previews")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    }
};
QTEST_GUILESS_MAIN(StorageTests)
#include "storage_ixview.moc"
