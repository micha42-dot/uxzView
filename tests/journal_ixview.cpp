// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/file_journal.h"
#include "core/file_revision.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>
namespace {
bool put(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}
QByteArray get(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool rewriteLast(const QString &root, const std::function<void(QJsonObject &)> &change) {
    const QDir folder(root + QStringLiteral("/entries"));
    const auto names = folder.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (names.isEmpty()) return false;
    const auto path = folder.filePath(names.last());
    auto object = QJsonDocument::fromJson(get(path)).object();
    change(object);
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
ix::FileJournal::WriteGuard failState(const QString &state) {
    return [state](const QJsonObject &entry) {
        return entry.value(QStringLiteral("state")).toString() == state ? QStringLiteral("injected full disk") : QString();
    };
}
} // namespace
class JournalTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_{QStringLiteral(IX_JOURNAL_OUTPUT "/run-journal-XXXXXX")};
    QString home_, root_, source_, target_;
    int number_ = 0;
    bool apply(const QString &kind, ix::FileJournal &journal, QString &error) {
        if (!put(source_, QByteArray("new pixels"))) return false;
        if (kind == QStringLiteral("replace") && !put(target_, QByteArray("old pixels"))) return false;
        if (kind == QStringLiteral("create") || kind == QStringLiteral("replace"))
            error = journal.installOutput(source_, target_, kind == QStringLiteral("replace"),
                kind == QStringLiteral("replace") ? ix::imageFileKey(target_) : QString());
        else error = journal.perform(kind, source_, kind == QStringLiteral("delete") ? QString() : target_);
        return true;
    }
    QJsonObject last() const { return ix::FileJournal(root_).entries().last().toObject(); }
  private slots:
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewJournalTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        qApp->setProperty("ixStorageData", temporary_.filePath(QStringLiteral("application-data")));
    }
    void init() {
        home_ = temporary_.filePath(QString::number(++number_));
        root_ = home_ + QStringLiteral("/journal");
        source_ = home_ + QStringLiteral("/source.bin");
        target_ = home_ + QStringLiteral("/target.bin");
    }
    void completedOperationsRecover_data() {
        QTest::addColumn<QString>("kind");
        for (const auto *kind : {"copy", "move", "delete", "create", "replace"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void completedOperationsRecover() {
        QFETCH(QString, kind);
        ix::FileJournal faulty(root_, failState(QStringLiteral("committed")));
        QString error;
        QVERIFY(apply(kind, faulty, error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
        ix::FileJournal journal(root_);
        const auto recovered = journal.recover();
        QVERIFY2(recovered.isEmpty(), qPrintable(recovered));
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("committed"));
        const auto storedTarget = last().value(QStringLiteral("target")).toString();
        QCOMPARE(get(storedTarget), QByteArray("new pixels"));
        QVERIFY(journal.recover().isEmpty()); // idempotent, no second file move
        QVERIFY(journal.undo().isEmpty());
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("undone"));
        if (kind == QStringLiteral("replace")) QCOMPARE(get(target_), QByteArray("old pixels"));
        else QVERIFY(!QFileInfo::exists(storedTarget));
        if (kind != QStringLiteral("create") && kind != QStringLiteral("replace"))
            QCOMPARE(get(source_), QByteArray("new pixels"));
    }
    void interruptedUndoRetryDoesNotUndoOlder_data() { completedOperationsRecover_data(); }
    void interruptedUndoRetryDoesNotUndoOlder() {
        QFETCH(QString, kind);
        ix::FileJournal journal(root_);
        const auto olderSource = home_ + QStringLiteral("/older-source.bin");
        const auto olderTarget = home_ + QStringLiteral("/older-target.bin");
        QVERIFY(put(olderSource, QByteArray("older")));
        QVERIFY(journal.perform(QStringLiteral("copy"), olderSource, olderTarget).isEmpty());
        QString error;
        QVERIFY(apply(kind, journal, error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        ix::FileJournal faulty(root_, failState(QStringLiteral("undone")));
        QVERIFY(!faulty.undo().isEmpty());
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("undoPending"));
        QVERIFY(journal.undo().isEmpty()); // finish this Undo, do not select its predecessor
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("undone"));
        QCOMPARE(get(olderTarget), QByteArray("older"));
        QCOMPARE(journal.entries().first().toObject().value(QStringLiteral("state")).toString(), QStringLiteral("committed"));
    }
    void interruptedReplacementRestoresMissingTarget_data() {
        QTest::addColumn<QString>("state");
        QTest::newRow("pending") << QStringLiteral("pending");
        QTest::newRow("failed-rollback") << QStringLiteral("failed");
    }
    void interruptedReplacementRestoresMissingTarget() {
        QFETCH(QString, state);
        ix::FileJournal faulty(root_, failState(QStringLiteral("committed")));
        QString error;
        QVERIFY(apply(QStringLiteral("replace"), faulty, error));
        QVERIFY(!error.isEmpty());
        QVERIFY(QFile::rename(target_, source_)); // crash between the two forward moves
        QVERIFY(rewriteLast(root_, [&](QJsonObject &entry) { entry.insert(QStringLiteral("state"), state); }));
        QVERIFY(ix::FileJournal(root_).recover().isEmpty());
        QCOMPARE(get(target_), QByteArray("old pixels"));
        QCOMPARE(get(source_), QByteArray("new pixels"));
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QVERIFY(ix::FileJournal(root_).recover().isEmpty());
    }
    void interruptedReplacementUndoRestoresBackup() {
        ix::FileJournal journal(root_);
        QString error;
        QVERIFY(apply(QStringLiteral("replace"), journal, error));
        QVERIFY(error.isEmpty());
        const auto discard = root_ + QStringLiteral("/trash/interrupted-undo.bin");
        QVERIFY(QFile::rename(target_, discard));
        QVERIFY(rewriteLast(root_, [&](QJsonObject &entry) {
            entry.insert(QStringLiteral("state"), QStringLiteral("undoPending"));
            entry.insert(QStringLiteral("discard"), discard);
        }));
        QVERIFY(journal.recover().isEmpty());
        QCOMPARE(get(target_), QByteArray("old pixels"));
        QCOMPARE(get(discard), QByteArray("new pixels"));
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("undone"));
    }
    void changedFilesStayUntouched_data() {
        QTest::addColumn<bool>("changeBackup");
        QTest::newRow("external-target") << false;
        QTest::newRow("external-backup") << true;
    }
    void changedFilesStayUntouched() {
        QFETCH(bool, changeBackup);
        ix::FileJournal faulty(root_, failState(QStringLiteral("committed")));
        QString error;
        QVERIFY(apply(QStringLiteral("replace"), faulty, error));
        const auto backup = last().value(QStringLiteral("backup")).toString();
        QVERIFY(put(changeBackup ? backup : target_, QByteArray("external changes")));
        const auto targetHash = ix::sha256File(target_), backupHash = ix::sha256File(backup);
        ix::FileJournal journal(root_);
        QVERIFY(!journal.recover().isEmpty());
        QVERIFY(!journal.undo().isEmpty());
        QCOMPARE(ix::sha256File(target_), targetHash);
        QCOMPARE(ix::sha256File(backup), backupHash);
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
    }
    void duplicateMoveRemainsPending() {
        ix::FileJournal faulty(root_, failState(QStringLiteral("committed")));
        QString error;
        QVERIFY(apply(QStringLiteral("move"), faulty, error));
        QVERIFY(put(source_, QByteArray("new pixels")));
        QVERIFY(!ix::FileJournal(root_).recover().isEmpty());
        QCOMPARE(get(source_), QByteArray("new pixels"));
        QCOMPARE(get(target_), QByteArray("new pixels"));
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
    }
    void failedRecoveryRetainsDurableIntent() {
        ix::FileJournal faulty(root_, failState(QStringLiteral("committed")));
        QString error;
        QVERIFY(apply(QStringLiteral("replace"), faulty, error));
        QVERIFY(!faulty.recover().isEmpty());
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
        QVERIFY(ix::FileJournal(root_).recover().isEmpty());
    }
    void recoveryHonorsInterprocessLock() {
        ix::FileJournal faulty(root_, failState(QStringLiteral("committed")));
        QString error;
        QVERIFY(apply(QStringLiteral("copy"), faulty, error));
        QLockFile lock(root_ + QStringLiteral("/journal.lock"));
        QVERIFY(lock.tryLock(0));
        QVERIFY(!ix::FileJournal(root_).recover().isEmpty());
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
        lock.unlock();
        QVERIFY(ix::FileJournal(root_).recover().isEmpty());
    }
    void recoveryDoesNotCreateEmptyJournal() {
        QVERIFY(ix::FileJournal(root_).recover().isEmpty());
        QVERIFY(!QFileInfo::exists(root_));
    }
    void incompleteCopyIsNotCommitted() {
        ix::FileJournal journal(root_);
        QString error;
        QVERIFY(apply(QStringLiteral("copy"), journal, error));
        QVERIFY(QFile::remove(target_));
        QVERIFY(rewriteLast(root_, [](QJsonObject &entry) { entry.insert(QStringLiteral("state"), QStringLiteral("pending")); }));
        QVERIFY(journal.recover().isEmpty());
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QCOMPARE(get(source_), QByteArray("new pixels"));
    }
    void legacyUndoWithoutDiscardIsNotGuessed() {
        ix::FileJournal journal(root_);
        QString error;
        QVERIFY(apply(QStringLiteral("copy"), journal, error));
        const auto discard = root_ + QStringLiteral("/trash/legacy-undo.bin");
        QVERIFY(QFile::rename(target_, discard));
        QVERIFY(rewriteLast(root_, [](QJsonObject &entry) { entry.insert(QStringLiteral("state"), QStringLiteral("undoPending")); }));
        QVERIFY(!journal.recover().isEmpty());
        QCOMPARE(get(discard), QByteArray("new pixels"));
        QCOMPARE(last().value(QStringLiteral("state")).toString(), QStringLiteral("undoPending"));
    }
    void backupOutsideJournalIsRejected() {
        ix::FileJournal faulty(root_, failState(QStringLiteral("committed")));
        QString error;
        QVERIFY(apply(QStringLiteral("replace"), faulty, error));
        const auto external = home_ + QStringLiteral("/outside.bin");
        QVERIFY(put(external, QByteArray("old pixels")));
        QVERIFY(QFile::rename(target_, source_));
        QVERIFY(rewriteLast(root_, [&](QJsonObject &entry) { entry.insert(QStringLiteral("backup"), external); }));
        QVERIFY(!ix::FileJournal(root_).recover().isEmpty());
        QCOMPARE(get(external), QByteArray("old pixels"));
        QVERIFY(!QFileInfo::exists(target_));
    }
};
QTEST_GUILESS_MAIN(JournalTests)
#include "journal_ixview.moc"
