// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/file_operations.h"
#include "core/file_revision.h"
#include "core/codec_registry.h"
#include "core/batch_processor.h"
#include "core/image_catalog.h"
#include <QScopeGuard>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
namespace {
bool put(const QString &path, const QByteArray &bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray get(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
}
class FileOperationTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_;
  private slots:
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewFileOperationTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        qApp->setProperty("ixStorageData", temporary_.filePath(QStringLiteral("data")));
    }
    void operationsDoNotCreateHistory_data() {
        QTest::addColumn<QString>("kind");
        for (const auto *kind : {"copy", "move", "delete"}) QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void operationsDoNotCreateHistory() {
        QFETCH(QString, kind); QTemporaryDir folder; QVERIFY(folder.isValid());
        const auto source = folder.filePath(QStringLiteral("source.bin"));
        const auto target = folder.filePath(QStringLiteral("target.bin"));
        QVERIFY(put(source, "original pixels"));
        const auto before = ix::imageFileKey(source);
        ix::FileOperations operations([&](const QString &path, QString *trash) {
            *trash = target; return QFile::rename(path, target) ? QString() : QStringLiteral("failure");
        });
        const auto result = operations.perform(kind, source, target);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QVERIFY(result.applied);
        QCOMPARE(get(target), QByteArray("original pixels"));
        QCOMPARE(QFileInfo::exists(source), kind == QStringLiteral("copy"));
        if (kind != QStringLiteral("delete")) {
            QCOMPARE(result.sourceRevision, before); QCOMPARE(result.targetRevision, ix::imageFileKey(target));
        }
        QVERIFY(!QFileInfo::exists(temporary_.filePath(QStringLiteral("data/file-journal"))));
    }
    void rejectsExistingTargetsAndChangedSources_data() {
        QTest::addColumn<QString>("kind");QTest::addColumn<bool>("peerWrite");
        for (const auto *kind : {"copy", "move"}) for (bool peer : {false,true})
            QTest::newRow((QByteArray(kind)+(peer?"-changed":"-exists")).constData()) << QString::fromLatin1(kind) << peer;
    }
    void rejectsExistingTargetsAndChangedSources() {
        QFETCH(QString, kind); QFETCH(bool, peerWrite); QTemporaryDir folder;
        const auto source=folder.filePath(QStringLiteral("source")), target=folder.filePath(QStringLiteral("target"));
        QVERIFY(put(source,"original")); if (!peerWrite) QVERIFY(put(target,"keep"));
        ix::FileOperations operations({}, [&] {
            return !peerWrite || put(source,"changed by peer") ? QString() : QStringLiteral("fixture failure");
        });
        const auto result=operations.perform(kind,source,target); QVERIFY(!result.applied && !result.error.isEmpty());
        QCOMPARE(get(source),peerWrite?QByteArray("changed by peer"):QByteArray("original"));
        QCOMPARE(QFileInfo::exists(target),!peerWrite); if(!peerWrite) QCOMPARE(get(target),QByteArray("keep"));
    }
    void trashRejectsSourceChangedDuringPreparation() {
        QTemporaryDir folder;
        const auto source = folder.filePath(QStringLiteral("changed.bin"));
        QVERIFY(put(source, "original"));
        bool trashed = false;
        ix::FileOperations operations([&](const QString &, QString *) {
            trashed = true; return QString();
        }, [&] { return put(source, "new external pixels") ? QString() : QStringLiteral("fixture failure"); });
        const auto result = operations.perform(QStringLiteral("delete"), source);
        QVERIFY(!result.applied && !result.error.isEmpty());
        QVERIFY(!trashed);
        QCOMPARE(get(source), QByteArray("new external pixels"));
    }
    void unindexedRenameDoesNotRewriteCatalog() {
        QTemporaryDir folder;
        const auto prior = qApp->property("ixStorageData");
        const auto restore = qScopeGuard([&] { qApp->setProperty("ixStorageData", prior); });
        qApp->setProperty("ixStorageData", folder.path());
        const auto source = folder.filePath(QStringLiteral("source.bin"));
        const auto target = folder.filePath(QStringLiteral("target.bin"));
        const auto catalog = folder.filePath(QStringLiteral("catalog.json"));
        QVERIFY(put(source, "pixels"));
        const QByteArray original = "{\n  \"another-image.png\": {\"rating\": 4}\n}\n";
        QVERIFY(put(catalog, original));
        const auto revision = ix::imageFileKey(catalog);
        const auto result = ix::FileOperations().perform(QStringLiteral("move"), source, target);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.applied);
        QCOMPARE(get(catalog), original);
        QCOMPARE(ix::imageFileKey(catalog), revision);
    }
    void rejectsStaleSourceAuthorization_data() {
        QTest::addColumn<QString>("kind");
        for (const auto *kind : {"copy", "move", "delete"}) QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void rejectsStaleSourceAuthorization() {
        QFETCH(QString, kind);
        QTemporaryDir folder;
        const auto source = folder.filePath(QStringLiteral("source"));
        const auto target = folder.filePath(QStringLiteral("target"));
        QVERIFY(put(source, "original"));
        const auto expected = ix::imageFileKey(source);
        QVERIFY(put(source, "replaced during confirmation"));
        bool trashed = false;
        ix::FileOperations operations([&](const QString &, QString *) { trashed = true; return QString(); });
        const auto result = operations.perform(kind, source, target, expected);
        QVERIFY(!result.applied && !result.error.isEmpty()); QVERIFY(!trashed);
        QCOMPARE(get(source), QByteArray("replaced during confirmation"));
        QVERIFY(!QFileInfo::exists(target));
    }
    void batchResultsKeepIdentityAcrossRunsAndReusedNames() {
        QTemporaryDir folder;
        const auto prior = qApp->property("ixStorageData");
        const auto restore = qScopeGuard([&] { qApp->setProperty("ixStorageData", prior); });
        qApp->setProperty("ixStorageData", folder.filePath(QStringLiteral("data")));
        const auto a = folder.filePath(QStringLiteral("a")), b = folder.filePath(QStringLiteral("b"));
        const auto c = folder.filePath(QStringLiteral("c")), d = folder.filePath(QStringLiteral("d"));
        QVERIFY(put(a, "first")); QVERIFY(put(c, "second"));
        ix::BatchFileChanges changes;
        ix::FileOperations operations;
        for (const auto &move : QList<QPair<QString, QString>>{{a,b}, {c,a}, {a,d}, {b,a}}) {
            const auto outcome = operations.perform(QStringLiteral("move"), move.first, move.second);
            QVERIFY2(outcome.error.isEmpty(), qPrintable(outcome.error)); QVERIFY(outcome.applied);
            changes.add({move.first, move.second, outcome.error, outcome.applied,
                         outcome.sourceRevision, outcome.targetRevision});
        }
        QCOMPARE(changes.resolve(a), a); QCOMPARE(changes.resolve(c), d);
        QCOMPARE(get(changes.resolve(a)), QByteArray("first"));
        QCOMPARE(get(changes.resolve(c)), QByteArray("second"));
        QVERIFY(changes.relocation(a).has_value());
        QCOMPARE(changes.relocation(a)->targetRevision, ix::imageFileKey(a));
        const QSet<QString> affected{a,b,c,d};
        const auto paths = changes.affectedPaths();
        QCOMPARE(QSet<QString>(paths.begin(), paths.end()), affected);
        changes.add({QStringLiteral("untouched"), QStringLiteral("failed"), QStringLiteral("rejected"), false});
        QCOMPARE(changes.resolve(QStringLiteral("untouched")), QStringLiteral("untouched"));
        QVERIFY(!changes.affectedPaths().contains(QStringLiteral("failed")));
    }
    void batchRelocationRejectsPixelReuseAfterExternalRewrite() {
        QTemporaryDir folder;
        const auto prior = qApp->property("ixStorageData");
        const auto restore = qScopeGuard([&] { qApp->setProperty("ixStorageData", prior); });
        qApp->setProperty("ixStorageData", folder.filePath(QStringLiteral("data")));
        const auto a=folder.filePath(QStringLiteral("a")), b=folder.filePath(QStringLiteral("b"));
        const auto c=folder.filePath(QStringLiteral("c"));
        QVERIFY(put(a, "original"));
        ix::FileOperations operations; ix::BatchFileChanges changes;
        auto first=operations.perform(QStringLiteral("move"),a,b); QVERIFY(first.applied);
        changes.add({a,b,first.error,true,first.sourceRevision,first.targetRevision});
        QVERIFY(put(b, "external replacement"));
        auto second=operations.perform(QStringLiteral("move"),b,c); QVERIFY(second.applied);
        changes.add({b,c,second.error,true,second.sourceRevision,second.targetRevision});
        QCOMPARE(changes.resolve(a),c);
        QVERIFY(changes.relocation(a)->sourceRevision.isEmpty());
        QCOMPARE(get(c),QByteArray("external replacement"));
    }
    void trashFailureNeverPermanentlyDeletes() {
        const auto source=temporary_.filePath(QStringLiteral("keep.bin")); QVERIFY(put(source,"keep"));
        ix::FileOperations operations([](const QString &,QString *){return QStringLiteral("unavailable");});
        const auto result=operations.perform(QStringLiteral("delete"),source); QVERIFY(!result.applied && !result.error.isEmpty());
        QCOMPARE(get(source),QByteArray("keep"));
    }
    void batchRenameRetainsMoveWhenCatalogReportsWarning() {
        QTemporaryDir folder;const auto source=folder.filePath(QStringLiteral("source.bin"));
        QVERIFY(put(source,"original"));
        const auto prior=qApp->property("ixStorageData");
        auto restore=qScopeGuard([&]{qApp->setProperty("ixStorageData",prior);});
        qApp->setProperty("ixStorageData",folder.path());
        QVERIFY(put(folder.filePath(QStringLiteral("catalog.json")),"broken"));
        ix::BatchOptions options;options.inputs={source};options.outputDirectory=folder.path();
        options.renameOnly=true;options.namePattern=QStringLiteral("renamed");
        std::atomic_bool cancel=false;
        const auto results=ix::BatchProcessor::run(options,cancel);
        QCOMPARE(results.size(),1);QVERIFY(results[0].moved);QVERIFY(!results[0].error.isEmpty());
        QVERIFY(!QFileInfo::exists(source));QCOMPARE(get(results[0].output),QByteArray("original"));
    }
    void saveGuardCancelsAtomicReplacement_data() {
        QTest::addColumn<bool>("exists");QTest::newRow("create")<<false;QTest::newRow("replace")<<true;
    }
    void saveRejectsNonFileTargets() {
        QTemporaryDir folder; const auto target=folder.filePath(QStringLiteral("directory.png"));
        QVERIFY(QDir().mkpath(target));
        ix::ImageDocument image;image.pixels=QImage(4,3,QImage::Format_RGB32);image.pixels.fill(Qt::blue);
        QVERIFY(!ix::CodecRegistry().save(image,target,"png",95,true).isEmpty());
        QVERIFY(QFileInfo(target).isDir());
#ifdef Q_OS_UNIX
        const auto original=folder.filePath(QStringLiteral("original.png")), link=folder.filePath(QStringLiteral("link.png"));
        QVERIFY(put(original,"original bytes"));QVERIFY(QFile::link(original,link));
        QVERIFY(!ix::CodecRegistry().save(image,link,"png",95,true).isEmpty());
        QCOMPARE(get(original),QByteArray("original bytes"));QVERIFY(QFileInfo(link).isSymLink());
#endif
    }
    void saveGuardCancelsAtomicReplacement() {
        QFETCH(bool,exists); QTemporaryDir folder;const auto path=folder.filePath(QStringLiteral("output.png"));
        if(exists) QVERIFY(put(path,"original bytes"));
        ix::ImageDocument image;image.pixels=QImage(40,30,QImage::Format_RGB32);image.pixels.fill(Qt::green);
        const auto expected=exists?ix::imageFileKey(path):QString();
        bool called=false;
        auto error=ix::CodecRegistry().save(image,path,"png",95,true,expected,[&]{called=true;return QStringLiteral("cancelled");});
        QVERIFY(called && !error.isEmpty());QCOMPARE(QFileInfo::exists(path),exists);
        if(exists) QCOMPARE(get(path),QByteArray("original bytes"));
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--profile-rename"))) {
        QTemporaryDir folder;
        if (!folder.isValid()) return 2;
        app.setProperty("ixStorageData", folder.filePath(QStringLiteral("data")));
        auto source=folder.filePath(QStringLiteral("source.bin"));
        auto target=folder.filePath(QStringLiteral("target.bin"));
        QFile payload(source);
        if (!payload.open(QIODevice::WriteOnly)) return 3;
        const QByteArray chunk(1024*1024,'x');
        for(int i=0;i<64;++i) if(payload.write(chunk)!=chunk.size()) return 3;
        payload.close();
        QJsonArray hashTimes,renameTimes;
        ix::FileOperations operations;
        for(int i=0;i<5;++i) {
            QElapsedTimer timer;timer.start();
            if(ix::sha256File(source).isEmpty()) return 4;
            hashTimes.append(timer.nsecsElapsed()/1e6);
            timer.restart();const auto result=operations.perform(QStringLiteral("move"),source,target);
            renameTimes.append(timer.nsecsElapsed()/1e6);
            if(!result.applied || !result.error.isEmpty()) return 5;
            source.swap(target);
        }
        const QJsonObject report{{QStringLiteral("bytes"),64*1024*1024},
            {QStringLiteral("removed_hash_component_ms"),hashTimes},{QStringLiteral("current_rename_ms"),renameTimes},
            {QStringLiteral("note"),QStringLiteral("Warm filesystem, same-volume temporary file, empty metadata catalog. Separate SHA-256 component versus current direct rename; not an old/new application benchmark.")}};
        QFile output(app.arguments().value(app.arguments().indexOf(QStringLiteral("--profile-rename"))+1));
        if(!output.open(QIODevice::WriteOnly)) return 6;
        return output.write(QJsonDocument(report).toJson())>0?0:6;
    }
    FileOperationTests tests;
    return QTest::qExec(&tests,argc,argv);
}
#include "file_operations_ixview.moc"
