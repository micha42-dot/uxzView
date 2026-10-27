// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/codec_registry.h"
#include "core/directory_index_service.h"
#include "core/batch_pipeline.h"
#include "core/file_journal.h"
#include "core/resize_model.h"
#include "core/image_loading.h"
#include "core/image_catalog.h"
#include "core/localization.h"
#include "core/preview_service.h"
#include "ui/main_window.h"
#include "ui/action_search.h"
#include "ui/library_dialog.h"
#include "ui/preview_strip.h"
#include "ui/platform_support.h"
#include "ui/pipeline_dialog.h"
#include "ui/resize_dialog.h"
#include "ui/rotation_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/slideshow_controller.h"
#include <QColorSpace>
#include <QImageReader>
#include <QImageWriter>
#include <QLockFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest>
#include <QtWidgets>
#include <QtConcurrent>
#include <QtEndian>
#include <limits>

class WorkflowTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_{QStringLiteral(IX_WORKFLOW_OUTPUT "/run-workflow-XXXXXX")};
    QString photo(const QString &name, QColor colour) {
        const auto path = temporary_.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QImage image(96, 64, QImage::Format_RGB32);
        image.fill(colour);
        return image.save(path) ? path : QString();
    }
    QAbstractButton *button(QWidget &widget, const QString &text) {
        for (auto *candidate : widget.findChildren<QAbstractButton *>())
            if (candidate->text() == ix::localized(text)) return candidate;
        return nullptr;
    }
  private slots:
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewWorkflowTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
    }
    void latestOpenReplacesDeferredRequest() {
        const auto first = photo(QStringLiteral("latest/a.png"), Qt::red);
        const auto stale = photo(QStringLiteral("latest/b.png"), Qt::green);
        const auto latest = photo(QStringLiteral("latest/c.png"), Qt::blue);
        QVERIFY(!first.isEmpty() && !stale.isEmpty() && !latest.isEmpty());
        ix::MainWindow window;
        window.openPath(first);
        QTRY_VERIFY(!window.isBusy() && window.document().path == first);
        QDialog blocker(&window);
        blocker.setModal(true);
        blocker.show();
        QTRY_COMPARE(QApplication::activeModalWidget(), &blocker);
        window.openPath(stale);
        blocker.hide();
        QCOMPARE(QApplication::activeModalWidget(), nullptr);
        window.openPath(latest);
        QTest::qWait(200); // allow the former 50 ms deferred request to fire
        QTRY_VERIFY(!window.isBusy());
        QCOMPARE(window.document().path, latest);
    }
    void commandSearchUpdatesActionsWithoutRetyping() {
        QAction action(QStringLiteral("Größe ändern"), this);
        action.setData(QStringLiteral("resize"));
        ix::ActionSearch search({&action});
        search.show();
        QTest::keyClicks(&search, QStringLiteral("dpi"));
        auto *model = search.completer()->model();
        const QPersistentModelIndex item(model->index(0, 0));
        QVERIFY(item.flags().testFlag(Qt::ItemIsEnabled));
        action.setEnabled(false);
        QVERIFY(!item.flags().testFlag(Qt::ItemIsEnabled));
        action.setEnabled(true);
        QVERIFY(item.flags().testFlag(Qt::ItemIsEnabled));
        action.setText(QStringLiteral("&Resize image"));
        QCOMPARE(item.data().toString(), QStringLiteral("Resize image"));
        QCOMPARE(search.completer()->completionCount(), 1);
    }
    void commandSearchRetainsRowsWhileTyping() {
        QAction action(QStringLiteral("Resize"), this);
        action.setData(QStringLiteral("resize"));
        ix::ActionSearch search({&action});
        search.show();
        QTest::keyClicks(&search, QStringLiteral("d"));
        auto *model = search.completer()->model();
        const QPersistentModelIndex item(model->index(0, 0));
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
        QTest::keyClicks(&search, QStringLiteral("pi"));
        QVERIFY(item.isValid());
        QVERIFY(resets.isEmpty());
        QVERIFY(inserted.isEmpty());
        QCOMPARE(search.completer()->completionCount(), 1);
    }
    void commandSearchEnterChoosesAvailableMatch() {
        QAction unavailable(QStringLiteral("Resize unavailable"), this);
        QAction available(QStringLiteral("Resize available"), this);
        unavailable.setEnabled(false);
        unavailable.setData(QStringLiteral("resize"));
        available.setData(QStringLiteral("resize"));
        ix::ActionSearch search({&unavailable, &available});
        QSignalSpy triggered(&available, &QAction::triggered);
        search.show();
        QTest::keyClicks(&search, QStringLiteral("dpi"));
        QTest::keyClick(&search, Qt::Key_Return);
        QCOMPARE(triggered.size(), 1);
    }
    void emptyCommandSearchNeverReplaysPreviousMatch() {
        QAction action(QStringLiteral("Resize"), this);
        action.setData(QStringLiteral("resize"));
        ix::ActionSearch search({&action});
        QSignalSpy triggered(&action, &QAction::triggered);
        search.show();
        QTest::keyClicks(&search, QStringLiteral("dpi"));
        QTest::keyClick(&search, Qt::Key_Escape);
        QVERIFY(search.text().isEmpty());
        QTest::keyClick(&search, Qt::Key_Return);
        QCOMPARE(triggered.size(), 0);
        QTest::keyClicks(&search, QStringLiteral("dpi"));
        QTest::keyClick(&search, Qt::Key_Return);
        QTRY_COMPARE(triggered.size(), 1);
        QCoreApplication::processEvents();
        QTest::keyClick(&search, Qt::Key_Return);
        QCOMPARE(triggered.size(), 1);
    }
    void commandSearchRemovesDestroyedActions() {
        auto action = std::make_unique<QAction>(QStringLiteral("Resize"));
        action->setData(QStringLiteral("resize"));
        ix::ActionSearch search({action.get()});
        search.show();
        QTest::keyClicks(&search, QStringLiteral("dpi"));
        auto *model = search.completer()->model();
        QCOMPARE(model->rowCount(), 1);
        action.reset();
        QCOMPARE(model->rowCount(), 0);
        QTest::keyClick(&search, Qt::Key_Return);
    }
    void commandSearchConsumerCanDestroyItsActionsAndWidget() {
        auto action = std::make_unique<QAction>(QStringLiteral("Resize"));
        action->setData(QStringLiteral("resize"));
        auto search = std::make_unique<ix::ActionSearch>(QList<QAction *>{action.get()});
        connect(search.get(), &ix::ActionSearch::finished, this, [&] {
            action.reset();
            search.reset();
        });
        search->show();
        QTest::keyClicks(search.get(), QStringLiteral("dpi"));
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(search.get(), &enter);
        QVERIFY(!search && !action);
        QCoreApplication::processEvents();
    }
    void libraryLabelsShowOnlyStoredAnnotations() {
        const auto plain = photo(QStringLiteral("library-style/plain.png"), Qt::blue);
        const auto rated = photo(QStringLiteral("library-style/rated.png"), Qt::red);
        const auto marked = photo(QStringLiteral("library-style/marked.png"), Qt::green);
        QVERIFY(!plain.isEmpty() && !rated.isEmpty() && !marked.isEmpty());
        const auto previousRoot = qApp->property("ixStorageData");
        const auto restore = qScopeGuard([&] { qApp->setProperty("ixStorageData", previousRoot); });
        qApp->setProperty("ixStorageData", temporary_.filePath(QStringLiteral("library-profile")));
        ix::ImageCatalog catalog;
        QVERIFY(catalog.setAnnotation(rated, 3, QStringLiteral("red")).isEmpty());
        QVERIFY(catalog.setAnnotation(marked, 0, QStringLiteral("green")).isEmpty());
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto *list = dialog->findChild<QListWidget *>(QStringLiteral("catalogImages"));
            QVERIFY(list);
            QCOMPARE(list->count(), 3);
            QHash<QString, QString> labels;
            for (int row = 0; row < list->count(); ++row) {
                const auto *item = list->item(row);
                labels.insert(item->data(Qt::UserRole).toString(), item->text());
            }
            // Capture before assertions so a failed baseline still has a visual.
            QVERIFY(dialog->grab().save(QStringLiteral(IX_WORKFLOW_OUTPUT "/library-style.png")));
            QCOMPARE(labels.value(plain), QStringLiteral("plain.png"));
            QVERIFY(labels.value(rated).contains(QString(3, QChar(0x2605))));
            QVERIFY(labels.value(rated).contains(IX_TEXT("Rot")));
            QVERIFY(!labels.value(marked).contains(QChar(0x2605)));
            QVERIFY(labels.value(marked).contains(IX_TEXT("Grün")));
        });
        ix::libraryDialog(nullptr, {plain, rated, marked}, [](QString) {});
    }
    void cancelledCatalogIndexDoesNotReportSuccess() {
        const auto path = photo(QStringLiteral("catalog-cancel/one.png"), Qt::blue);
        const auto previousRoot = qApp->property("ixStorageData");
        const auto restore = qScopeGuard([&] { qApp->setProperty("ixStorageData", previousRoot); });
        const auto root = temporary_.filePath(QStringLiteral("cancel-profile"));
        QVERIFY(QDir().mkpath(root));
        qApp->setProperty("ixStorageData", root);
        QLockFile lock(root + QStringLiteral("/catalog.json.lock"));
        QVERIFY(lock.tryLock(0));
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto *index = button(*dialog, QStringLiteral("Metadaten indexieren"));
            QVERIFY(index);
            QTimer::singleShot(0, dialog, [&] {
                const auto unlock = qScopeGuard([&] { lock.unlock(); });
                auto *progress = dialog->findChild<QProgressDialog *>();
                QVERIFY(progress);
                auto *cancel = button(*progress, QStringLiteral("Abbrechen"));
                QVERIFY(cancel);
                cancel->click();
            });
            index->click();
            auto *status = dialog->findChild<QLabel *>(QStringLiteral("catalogStatus"));
            QVERIFY(status);
            QVERIFY(status->text().isEmpty());
            QVERIFY(status->isHidden());
        });
        ix::libraryDialog(nullptr, {path}, [](QString) {});
    }
    void directoryRequestsCancelAndCoalesce() {
        QSemaphore started, release;
        std::atomic_bool sawCancellation = false;
        QStringList visited;
        QMutex mutex;
        ix::DirectoryIndexService service(nullptr, [&](const ix::DirectoryRequest &request,
                                                       const std::atomic_bool *cancel) {
            {
                QMutexLocker lock(&mutex);
                visited.append(request.directory);
            }
            if (request.directory == QStringLiteral("old")) {
                started.release();
                release.acquire();
                sawCancellation.store(cancel->load());
            }
            ix::DirectorySnapshot snapshot;
            snapshot.directory = request.directory;
            snapshot.files = {request.directory + QStringLiteral("/one.png")};
            snapshot.positions.insert(snapshot.files.first(), 0);
            return snapshot;
        });
        const auto unblock = qScopeGuard([&] { release.release(); });
        QSignalSpy ready(&service, &ix::DirectoryIndexService::ready);
        service.request({QStringLiteral("old"), {}, 0});
        QVERIFY(started.tryAcquire(1, 5000));
        service.request({QStringLiteral("intermediate"), {}, 0});
        service.request({QStringLiteral("latest"), {}, 0});
        release.release();
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(sawCancellation.load());
        QVERIFY(!service.isBusy());
        const auto snapshot = ready.first().first().value<ix::DirectorySnapshot>();
        QCOMPARE(snapshot.directory, QStringLiteral("latest"));
        QCOMPARE(snapshot.files, QStringList{QStringLiteral("latest/one.png")});
        QCOMPARE(snapshot.positions.value(snapshot.files.first(), -1), 0);
        QMutexLocker lock(&mutex);
        QCOMPARE(visited, (QStringList{QStringLiteral("old"), QStringLiteral("latest")}));
    }
    void cancellingDirectoryIndexDropsPendingResults() {
        QSemaphore started, release;
        std::atomic<int> calls = 0;
        ix::DirectoryIndexService service(nullptr, [&](const ix::DirectoryRequest &request,
                                                       const std::atomic_bool *) {
            ++calls;
            started.release();
            release.acquire();
            ix::DirectorySnapshot snapshot;
            snapshot.directory = request.directory;
            return snapshot;
        });
        const auto unblock = qScopeGuard([&] { release.release(3); });
        QSignalSpy ready(&service, &ix::DirectoryIndexService::ready);
        service.request({QStringLiteral("old"), {}});
        QVERIFY(started.tryAcquire(1, 5000));
        service.request({QStringLiteral("queued"), {}});
        service.cancel();
        release.release();
        QTRY_VERIFY(!service.isBusy());
        QCOMPARE(calls.load(), 1);
        QVERIFY(ready.isEmpty());
    }
    void directoryIndexDestructionCancelsAndJoins() {
        QSemaphore started;
        std::atomic_bool cancelled = false;
        auto service = std::make_unique<ix::DirectoryIndexService>(nullptr,
            [&](const ix::DirectoryRequest &, const std::atomic_bool *cancel) {
                started.release();
                QDeadlineTimer deadline(5000);
                while (!cancel->load() && !deadline.hasExpired())
                    QThread::msleep(1);
                cancelled.store(cancel->load());
                return ix::DirectorySnapshot{};
            });
        service->request({QStringLiteral("active"), {}});
        QVERIFY(started.tryAcquire(1, 5000));
        service.reset();
        QVERIFY(cancelled.load());
    }
    void directoryIndexConsumersCanRestartOrDestroy() {
        const auto scan = [](const ix::DirectoryRequest &request, const std::atomic_bool *) {
            ix::DirectorySnapshot snapshot;
            snapshot.directory = request.directory;
            return snapshot;
        };
        ix::DirectoryIndexService service(nullptr, scan);
        QSignalSpy ready(&service, &ix::DirectoryIndexService::ready);
        connect(&service, &ix::DirectoryIndexService::ready, &service,
                [&](const ix::DirectorySnapshot &snapshot) {
            if (snapshot.directory == QStringLiteral("first"))
                service.request({QStringLiteral("next"), {}});
        });
        service.request({QStringLiteral("first"), {}});
        QTRY_COMPARE(ready.size(), 2);
        QCOMPARE(ready.last().first().value<ix::DirectorySnapshot>().directory, QStringLiteral("next"));
        QVERIFY(!service.isBusy());
        auto *disposable = new ix::DirectoryIndexService(nullptr, scan);
        QPointer<ix::DirectoryIndexService> alive(disposable);
        connect(disposable, &ix::DirectoryIndexService::ready, this, [=] { delete disposable; });
        disposable->request({QStringLiteral("done"), {}});
        QTRY_VERIFY(alive.isNull());
    }
    void directoryIndexRecoversAfterFailure() {
        ix::DirectoryIndexService service(nullptr, [](const ix::DirectoryRequest &request,
                                                       const std::atomic_bool *) {
            if (request.directory == QStringLiteral("bad"))
                throw std::runtime_error("fixture directory error");
            ix::DirectorySnapshot snapshot;
            snapshot.directory = request.directory;
            return snapshot;
        });
        QSignalSpy ready(&service, &ix::DirectoryIndexService::ready);
        QSignalSpy failed(&service, &ix::DirectoryIndexService::failed);
        service.request({QStringLiteral("bad"), {}});
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("fixture directory error"));
        QVERIFY(ready.isEmpty());
        service.request({QStringLiteral("good"), {}});
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.first().first().value<ix::DirectorySnapshot>().directory, QStringLiteral("good"));
    }
    void directoryWorkerDiscoversFormatsAndRetainsExplicitFilters() {
        const auto png = photo(QStringLiteral("worker-formats/photo.png"), Qt::blue);
        const auto jpeg = photo(QStringLiteral("worker-formats/photo.jpg"), Qt::red);
        QVERIFY(!png.isEmpty() && !jpeg.isEmpty());
        const auto folder = QFileInfo(png).absolutePath();
        QFile unrelated(folder + QStringLiteral("/notes.txt"));
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        QCOMPARE(unrelated.write("not an image"), qint64(12));
        unrelated.close();
        ix::DirectoryIndexService service;
        QSignalSpy ready(&service, &ix::DirectoryIndexService::ready);
        service.request({folder, {}, 0, false, false, true});
        QTRY_COMPARE(ready.size(), 1);
        const auto discovered = ready.last().first().value<ix::DirectorySnapshot>();
        QCOMPARE(discovered.files.size(), 2);
        QVERIFY(discovered.files.contains(png) && discovered.files.contains(jpeg));
        service.request({folder, {QStringLiteral("png")}, 0});
        QTRY_COMPARE(ready.size(), 2);
        QCOMPARE(ready.last().first().value<ix::DirectorySnapshot>().files, QStringList{png});
        service.request({folder, {}, 0});
        QTRY_COMPARE(ready.size(), 3);
        const auto unfiltered = ready.last().first().value<ix::DirectorySnapshot>();
        QCOMPARE(unfiltered.files.size(), 3);
        QVERIFY(unfiltered.files.contains(folder + QStringLiteral("/notes.txt")));
    }
    void statusTimestampTracksLoadedRevisionInsteadOfPollingDisk() {
        const auto path = photo(QStringLiteral("timestamp/source.png"), Qt::blue);
        QVERIFY(!path.isEmpty());
        auto changeTime = [&](const QDateTime &time) {
            QFile file(path);
            return file.open(QIODevice::ReadWrite) && file.setFileTime(time, QFileDevice::FileModificationTime);
        };
        QVERIFY(changeTime(QDateTime::fromSecsSinceEpoch(1600000000, QTimeZone::UTC)));
        const auto originalTime = QFileInfo(path).lastModified();
        ix::MainWindow window;
        window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && window.document().path == path);
        QCOMPARE(window.document().sourceModified, originalTime);
        auto *label = window.findChild<QLabel *>(QStringLiteral("sourceModifiedStatus"));
        QVERIFY(label);
        QCOMPARE(label->text(), originalTime.toString(QStringLiteral("dd.MM.yyyy HH:mm")));
        QVERIFY(changeTime(originalTime.addDays(2)));
        const auto changedTime = QFileInfo(path).lastModified();
        QVERIFY(changedTime != originalTime);
        auto *view = window.findChild<ix::ImageView *>();
        view->setZoom(2);
        view->setSelection(QRect(1, 1, 10, 10));
        QCOMPARE(label->text(), originalTime.toString(QStringLiteral("dd.MM.yyyy HH:mm")));
        window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && window.document().sourceModified == changedTime);
        QCOMPARE(label->text(), changedTime.toString(QStringLiteral("dd.MM.yyyy HH:mm")));
    }
    void renamedDocumentAndUndoRetainTargetFileState() {
        const auto source = photo(QStringLiteral("timestamp-rename/source.png"), Qt::blue);
        const auto target = temporary_.filePath(QStringLiteral("timestamp-rename/target.png"));
        QVERIFY(!source.isEmpty());
        ix::MainWindow window(nullptr, temporary_.filePath(QStringLiteral("timestamp-journal")));
        window.openPath(source);
        QTRY_VERIFY(!window.isBusy() && window.document().path == source);
        window.dispatch(QStringLiteral("right"));
        QTRY_VERIFY(!window.isBusy() && window.document().modified);
        bool handled = false;
        QString error;
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer driver;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (elapsed.elapsed() > 10000) {
                error = QStringLiteral("Rename dialog timed out");
                if (dialog) dialog->reject();
            } else if (auto *input = qobject_cast<QInputDialog *>(dialog); input && !handled) {
                handled = true;
                input->setTextValue(QStringLiteral("target.png"));
                input->accept();
            } else if (auto *message = qobject_cast<QMessageBox *>(dialog)) {
                error = message->text();
                message->reject();
            }
        });
        driver.start(10);
        window.dispatch(QStringLiteral("rename"));
        driver.stop();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(handled);
        QTRY_VERIFY(!window.isBusy() && window.document().path == target);
        const auto modified = QFileInfo(target).lastModified();
        QCOMPARE(window.document().sourceModified, modified);
        QCOMPARE(window.document().sourceRevision, ix::imageFileKey(target));
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY(!window.isBusy() && window.document().pixels.size() == QSize(96, 64));
        QCOMPARE(window.document().path, target);
        QCOMPARE(window.document().sourceModified, modified);
        QCOMPARE(window.document().fileBytes, QFileInfo(target).size());
    }
    void mainWindowUsesDirectoryIndexService() {
        const auto path = photo(QStringLiteral("index-service/one.png"), Qt::blue);
        QVERIFY(!path.isEmpty());
        ix::MainWindow window;
        auto *service = window.findChild<ix::DirectoryIndexService *>();
        QVERIFY(service);
        QSignalSpy ready(service, &ix::DirectoryIndexService::ready);
        window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && !ready.isEmpty());
        const auto snapshot = ready.last().first().value<ix::DirectorySnapshot>();
        QCOMPARE(snapshot.directory, QFileInfo(path).absolutePath());
        QCOMPARE(snapshot.files, QStringList{path});
        window.openPath(path);
        QTRY_VERIFY(!window.isBusy());
        QVERIFY(!service->isBusy());
        QCOMPARE(ready.size(), 1); // Navigating in an unchanged folder reuses its index.
    }
    void imageViewHandlesSpaceWithoutWindowShortcuts() {
        ix::ImageView view;
        QImage pixels(16, 8, QImage::Format_RGB32);
        pixels.fill(Qt::blue);
        view.setImage(pixels);
        QSignalSpy navigation(&view, &ix::ImageView::navigate);
        for (auto modifiers : {Qt::NoModifier, Qt::ControlModifier}) {
            for (auto key : {Qt::Key_Space, Qt::Key_Backspace}) {
                QKeyEvent event(QEvent::KeyPress, key, modifiers);
                QVERIFY(view.handleKey(&event));
                QCOMPARE(navigation.size(), 1);
                QCOMPARE(navigation.takeFirst().first().toInt(),
                         (key == Qt::Key_Space ? 1 : -1) * (modifiers == Qt::ControlModifier ? 5 : 1));
            }
        }
        QKeyEvent other(QEvent::KeyPress, Qt::Key_Space, Qt::AltModifier);
        QVERIFY(!view.handleKey(&other));
        QVERIFY(navigation.isEmpty());
    }
    void saveRejectsChangedOrUnexpectedTargets_data() {
        QTest::addColumn<int>("change");
        QTest::newRow("rewritten") << 0;
        QTest::newRow("created") << 1;
        QTest::newRow("deleted") << 2;
    }
    void saveRejectsChangedOrUnexpectedTargets() {
        QFETCH(int, change);
        const auto path = temporary_.filePath(QStringLiteral("save-guard-%1.png").arg(change));
        QImage pixels(32, 16, QImage::Format_RGB32);
        pixels.fill(Qt::red);
        if (change != 1) QVERIFY(pixels.save(path));
        const auto expected = QFileInfo::exists(path) ? ix::imageFileKey(path) : QString();
        if (change == 2) QVERIFY(QFile::remove(path));
        else {
            pixels.fill(Qt::blue);
            QVERIFY(pixels.save(path));
        }
        const auto changedHash = QFileInfo::exists(path) ? ix::sha256File(path) : QString();
        ix::ImageDocument output;
        output.pixels = QImage(47, 23, QImage::Format_RGB32);
        output.pixels.fill(Qt::green);
        const auto error = ix::CodecRegistry().save(output, path, "png", 95, true, expected);
        QVERIFY2(!error.isEmpty(), "A changed target was overwritten/recreated");
        QCOMPARE(QFileInfo::exists(path), change != 2);
        QCOMPARE(QFileInfo::exists(path) ? ix::sha256File(path) : QString(), changedHash);
    }
    void journalRejectsUnknownUndoOperation() {
        const auto source = photo(QStringLiteral("unknown-journal/source.png"), Qt::red);
        const auto target = temporary_.filePath(QStringLiteral("unknown-journal/copy.png"));
        const auto root = temporary_.filePath(QStringLiteral("unknown-journal/history"));
        ix::FileJournal journal(root);
        QVERIFY(journal.perform(QStringLiteral("copy"), source, target).isEmpty());
        const auto hash = ix::sha256File(target);
        QDir entries(root + QStringLiteral("/entries"));
        const auto files = entries.entryList({QStringLiteral("*.json")}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QFile file(entries.filePath(files.first()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto entry = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        entry.insert(QStringLiteral("kind"), QStringLiteral("unsupported-operation"));
        const auto bytes = QJsonDocument(entry).toJson();
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QVERIFY2(!journal.undo().isEmpty(), "Unknown journal operation modified the target");
        QCOMPARE(ix::sha256File(target), hash);
        QCOMPARE(journal.entries().last().toObject().value(QStringLiteral("state")).toString(),
                 QStringLiteral("committed"));
    }
    void saveAcceptsMatchingTargetRevisions() {
        ix::ImageDocument output;
        output.pixels = QImage(47, 23, QImage::Format_RGBA64);
        output.pixels.fill(QColor(17, 129, 231, 101));
        output.pixels.setColorSpace(QColorSpace::DisplayP3);
        for (bool existing : {false, true}) {
            const auto path = temporary_.filePath(QStringLiteral("matching-%1.png").arg(existing));
            if (existing) QVERIFY(output.pixels.save(path));
            const auto expected = existing ? ix::imageFileKey(path) : QString();
            ix::CodecRegistry registry;
            const auto error = registry.save(output, path, "png", 95, true, expected);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            const auto decoded = registry.read(path);
            QVERIFY(decoded.ok());
            QCOMPARE(decoded.document.pixels, output.pixels);
            QCOMPARE(decoded.document.pixels.colorSpace(), output.pixels.colorSpace());
        }
    }
    void extremeDpiIsBoundedAndInvalidProcessingIsRejected() {
        ix::ResizeModel up({1, 1}, 100000);
        up.proportional = false;
        up.adaptDpi = true;
        up.setWidth(100000);
        QCOMPARE(up.dpi(), 100000.0);
        ix::ResizeModel down({100000, 1}, 96);
        down.adaptDpi = true;
        down.proportional = false;
        down.setWidth(1);
        QCOMPARE(down.dpi(), 1.0);
        ix::ResizeModel invalid({1, 1}, std::numeric_limits<double>::infinity());
        QCOMPARE(invalid.dpi(), 96.0);
        invalid.setDpi(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(invalid.dpi(), 96.0);
        QImage pixels(8, 4, QImage::Format_RGB32);
        pixels.fill(Qt::blue);
        ix::ResizeOptions options;
        options.size = {4, 2};
        for (double dpi : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(), -1.0, .001, 1e20}) {
            options.dpi = dpi;
            QVERIFY_EXCEPTION_THROWN(ix::resample(pixels, options), std::runtime_error);
        }
        options.dpi = down.dpi();
        QCOMPARE(ix::resample(pixels, options).dotsPerMeterX(), 39);
    }
    void resizeImageBufferBudgetMatchesSelectedMethod() {
        {
            QImage pixels(10000, 1000, QImage::Format_RGB32);
            QVERIFY(!pixels.isNull());
            pixels.fill(Qt::blue);
            ix::ResizeOptions options;
            options.size = {60000, 100};
            options.filter = ix::ResampleFilter::Nearest;
            const auto result = ix::resample(pixels, options);
            QCOMPARE(result.size(), options.size);
            QCOMPARE(result.format(), pixels.format());
            QCOMPARE(result.pixelColor(59999, 99), QColor(Qt::blue));
        }
        QImage pixels(10000, 2000, QImage::Format_RGB32);
        QVERIFY(!pixels.isNull());
        pixels.fill(Qt::blue);
        ix::ResizeOptions options;
        options.size = pixels.size();
        // Source, 16-bit input, intermediate, output and final conversion
        // exceed 512 MiB; reject before allocating those extra buffers.
        QVERIFY_EXCEPTION_THROWN(ix::resample(pixels, options), std::runtime_error);
    }
    void saveKeepsTargetChangedDuringLossWarning_data() {
        QTest::addColumn<bool>("titlelessMessages");
        QTest::newRow("platform-default") << false;
        QTest::newRow("titleless-macos-dialogs") << true;
    }
    void saveKeepsTargetChangedDuringLossWarning() {
        QFETCH(bool, titlelessMessages);
        const auto source = temporary_.filePath(QStringLiteral("save-warning-source.png"));
        const auto target = temporary_.filePath(QStringLiteral("save-warning-target.jpg"));
        QImage image(32, 16, QImage::Format_ARGB32);
        image.fill(QColor(255, 0, 0, 80));
        QVERIFY(image.save(source));
        QImage initial(41, 29, QImage::Format_RGB32);
        initial.fill(Qt::black);
        QVERIFY(initial.save(target));
        ix::MainWindow window;
        window.openPath(source);
        QTRY_VERIFY(!window.isBusy() && window.document().path == source);
        const auto sourceHash = ix::sha256File(source);
        const auto warningText = ix::CodecRegistry::exportLosses(window.document(), "jpg")
                                     .join(QStringLiteral("\n\n")) +
                                 ix::localized(QStringLiteral("\n\nTrotzdem speichern?"));
        const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restore = qScopeGuard([native] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native);
        });
        bool chosen = false, changed = false;
        QString protectedHash, dialogError;
        QElapsedTimer deadline;
        deadline.start();
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (deadline.elapsed() > 10000 && dialogError.isEmpty())
                dialogError = QStringLiteral("Save warning workflow exceeded 10 seconds");
            if (!dialogError.isEmpty()) {
                if (auto *dialog = qobject_cast<QDialog *>(modal)) dialog->reject();
                return;
            }
            if (auto *dialog = qobject_cast<QFileDialog *>(modal)) {
                if (!chosen) {
                    auto *editor = dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
                    if (!editor) {
                        dialogError = QStringLiteral("Expected the Qt save filename editor");
                        dialog->reject();
                        return;
                    }
                    chosen = true;
                    editor->setText(target);
                    QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
                }
            } else if (auto *message = qobject_cast<QMessageBox *>(modal)) {
                // QMessageBox::setWindowTitle is deliberately ignored on macOS.
                // Also exercise that behavior on other CI hosts.
                if (titlelessMessages) message->QWidget::setWindowTitle(QString());
                if (!changed && message->icon() == QMessageBox::Warning &&
                    message->text() == warningText &&
                    message->standardButtons() == (QMessageBox::Yes | QMessageBox::Cancel)) {
                    QImage newer(113, 71, QImage::Format_RGB32);
                    newer.fill(Qt::blue);
                    changed = newer.save(target);
                    protectedHash = ix::sha256File(target);
                }
                if (auto *yes = message->button(QMessageBox::Yes)) yes->click();
                else if (auto *ok = message->button(QMessageBox::Ok)) ok->click();
            }
        });
        timer.start(10);
        window.dispatch(QStringLiteral("saveAs"));
        timer.stop();
        QVERIFY2(dialogError.isEmpty(), qPrintable(dialogError));
        QVERIFY(chosen);
        QVERIFY2(changed, "Expected the JPEG loss warning before rewriting the save destination");
        QVERIFY(!protectedHash.isEmpty());
        QCOMPARE(ix::sha256File(target), protectedHash);
        QCOMPARE(QImage(target).size(), QSize(113, 71));
        QCOMPARE(ix::sha256File(source), sourceHash);
        QCOMPARE(window.document().path, source);
    }
    void pipelineRejectsSourceChangedByDecoder() {
        class ChangingCodec final : public ix::ImageCodec {
          public:
            bool accepts(const QString &) const override { return true; }
            QList<ix::FormatCapability> capabilities() const override { return {}; }
            ix::ImageResult read(const QString &path, int) const override {
                ix::ImageDocument old;
                old.pixels = QImage(path);
                old.path = path;
                old.format = "png";
                QImage changed(113, 71, QImage::Format_RGB32);
                changed.fill(Qt::blue);
                if (!changed.save(path)) return {{}, QStringLiteral("fixture write failed")};
                return {old, {}};
            }
        };
        const auto source = photo(QStringLiteral("pipeline-changing/source.png"), Qt::red);
        ix::PipelineJob job;
        job.inputs = {source};
        job.outputDirectory = temporary_.filePath(QStringLiteral("pipeline-changing/output"));
        job.journalRoot = temporary_.filePath(QStringLiteral("pipeline-changing/journal"));
        job.parallelism = 1;
        job.profile.steps = {{QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        const auto plan = ix::BatchPipeline::plan(job);
        QCOMPARE(plan.size(), 1);
        QVERIFY(plan.first().error.isEmpty());
        ix::CodecRegistry registry;
        registry.add(std::make_shared<ChangingCodec>());
        std::atomic_bool cancel = false;
        const auto result = ix::BatchPipeline::run(job, plan, cancel, {}, &registry);
        QCOMPARE(result.size(), 1);
        QVERIFY2(!result.first().error.isEmpty(), "Pipeline published stale pixels after a decoder-side source rewrite");
        QVERIFY(!QFileInfo::exists(plan.first().output));
        QCOMPARE(QImage(source).size(), QSize(113, 71));
    }
    void resizeShortcutsSelectAbsoluteMode_data() {
        QTest::addColumn<QString>("choice");
        for (const auto &choice : {"640 x 480", "An Bildschirm anpassen",
                 "Bildschirmgröße (nicht proportional)", "Halb", "Doppelt", "Seiten vertauschen", "custom"})
            QTest::newRow(choice) << QString::fromUtf8(choice);
    }
    void resizeShortcutsSelectAbsoluteMode() {
        QFETCH(QString, choice);
        const auto saved = QSettings().value(QStringLiteral("resize/presets"));
        const auto restore = qScopeGuard([&] {
            if (saved.isValid()) QSettings().setValue(QStringLiteral("resize/presets"), saved);
            else QSettings().remove(QStringLiteral("resize/presets"));
        });
        QSettings().setValue(QStringLiteral("resize/presets"), QStringList{QStringLiteral("90 x 60")});
        ix::ResizeDialog dialog({400, 200}, 96);
        auto *percent = button(dialog, QStringLiteral("Prozentsatz vom Original:"));
        auto *absolute = button(dialog, QStringLiteral("Neue Größe:"));
        QVERIFY(percent && absolute);
        percent->click();
        if (choice == QStringLiteral("custom")) {
            QComboBox *presets = nullptr;
            for (auto *combo : dialog.findChildren<QComboBox *>())
                if (combo->findText(QStringLiteral("90 x 60")) >= 0) presets = combo;
            QVERIFY(presets);
            presets->setCurrentIndex(1);
            QVERIFY(QMetaObject::invokeMethod(presets, "activated", Q_ARG(int, 1)));
        } else {
            auto *selected = button(dialog, choice);
            QVERIFY(selected);
            selected->click();
        }
        QVERIFY2(absolute->isChecked(), qPrintable(choice));
        QVERIFY(dialog.findChild<QDoubleSpinBox *>(QStringLiteral("width"))->isEnabled());
        QVERIFY(!dialog.findChild<QDoubleSpinBox *>(QStringLiteral("percentWidth"))->isEnabled());
    }
    void resizePresetsCommitOnlyOnAccept() {
        const auto key = QStringLiteral("resize/presets");
        const auto saved = QSettings().value(key);
        const auto restore = qScopeGuard([&] {
            if (saved.isValid()) QSettings().setValue(key, saved);
            else QSettings().remove(key);
        });
        const QStringList initial{QStringLiteral("320 x 240")};
        QSettings().setValue(key, initial);
        for (bool accept : {false, true}) {
            ix::ResizeDialog dialog({400, 200}, 96);
            dialog.findChild<QDoubleSpinBox *>(QStringLiteral("width"))->setValue(900);
            auto *add = button(dialog, QStringLiteral("Zu Standards hinzuf."));
            QVERIFY(add);
            add->click();
            QCOMPARE(QSettings().value(key).toStringList(), initial);
            if (accept) static_cast<QDialog *>(&dialog)->accept();
            else dialog.reject();
            QCOMPARE(QSettings().value(key).toStringList(),
                     accept ? QStringList({initial.first(), QStringLiteral("900 x 450")}) : initial);
        }
    }
    void resizeDerivedDpiDisablesConflictingInputs() {
        ix::ResizeDialog dialog({400, 200}, 96);
        dialog.findChild<QDoubleSpinBox *>(QStringLiteral("width"))->setValue(800);
        auto *adapt = button(dialog, QStringLiteral("DPI auf neue Größe anpassen (proport.)"));
        auto *inch = button(dialog, QStringLiteral("inch"));
        auto *pixel = button(dialog, QStringLiteral("Pixel"));
        auto *dpi = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("dpi"));
        QVERIFY(adapt && inch && pixel && dpi);
        inch->click();
        adapt->click();
        QVERIFY(!dpi->isEnabled());
        QVERIFY(pixel->isChecked());
        QVERIFY(!inch->isEnabled());
        QCOMPARE(dialog.options().size, QSize(800, 400));
        QCOMPARE(dialog.options().dpi, 192.0);
        QCOMPARE(dpi->value(), 192.0);
        adapt->click();
        QVERIFY(dpi->isEnabled() && inch->isEnabled());
        QCOMPARE(dpi->value(), 96.0);
    }
    void numericEditorsUseInterfaceDecimalSeparator() {
        const auto language = ix::uiLanguage();
        const auto restore = qScopeGuard([&] { ix::setUiLanguage(language); });
        QVERIFY(ix::setUiLanguage(QStringLiteral("de")));
        ix::ResizeDialog resize({400, 200}, 96);
        auto *width = resize.findChild<QDoubleSpinBox *>(QStringLiteral("width"));
        QCOMPARE(width->locale().decimalPoint(), QStringLiteral(","));
        auto *inch = button(resize, QStringLiteral("inch"));
        QVERIFY(inch);
        inch->click();
        resize.show();
        resize.activateWindow();
        width->setFocus();
        QTRY_VERIFY(width->hasFocus());
        width->selectAll();
        QTest::keyClicks(width, QStringLiteral("2,5"));
        QTest::keyClick(width, Qt::Key_Tab);
        QCOMPARE(resize.options().size, QSize(240, 120));
        QImage image(32, 16, QImage::Format_RGB32);
        image.fill(Qt::blue);
        ix::RotationDialog rotation(image);
        auto *angle = rotation.findChild<QDoubleSpinBox *>(QStringLiteral("angle"));
        QCOMPARE(angle->locale().decimalPoint(), QStringLiteral(","));
        rotation.show();
        rotation.activateWindow();
        angle->setFocus();
        QTRY_VERIFY(angle->hasFocus());
        angle->selectAll();
        QTest::keyClicks(angle, QStringLiteral("12,5"));
        QTest::keyClick(angle, Qt::Key_Tab);
        QCOMPARE(rotation.options().angle, 12.5);
    }
    void rotationFastModeDisablesSmoothing() {
        QImage image(32, 16, QImage::Format_RGB32);
        image.fill(Qt::blue);
        ix::RotationDialog dialog(image);
        auto *fast = dialog.findChild<QCheckBox *>(QStringLiteral("fastMethod"));
        auto *smooth = dialog.findChild<QCheckBox *>(QStringLiteral("antialias"));
        QVERIFY(fast && smooth && smooth->isChecked());
        fast->setChecked(true);
        QVERIFY(!smooth->isEnabled());
        QVERIFY(!dialog.options().antialias);
        fast->setChecked(false);
        QVERIFY(smooth->isEnabled() && dialog.options().antialias);
        auto *canvas = dialog.findChild<QCheckBox *>(QStringLiteral("keepCanvas"));
        auto *background = dialog.findChild<QCheckBox *>(QStringLiteral("originalBackground"));
        canvas->setChecked(true);
        background->setChecked(true);
        canvas->setChecked(false);
        QVERIFY(!dialog.options().originalAsBackground);
    }
    void pipelineFilterMatchesAppliedClampAndResizeNames() {
        ix::PipelineDialog pipeline({});
        auto *chain = pipeline.findChild<QListWidget *>(QStringLiteral("pipelineChain"));
        QVERIFY(chain && chain->count() > 0);
        auto *item = chain->item(0);
        auto step = item->data(Qt::UserRole).toJsonObject();
        auto parameters = step.value(QStringLiteral("parameters")).toObject();
        parameters.insert(QStringLiteral("filter"), 99);
        step.insert(QStringLiteral("parameters"), parameters);
        item->setData(Qt::UserRole, step);
        int index = -2;
        QStringList names;
        QTimer::singleShot(0, &pipeline, [&] {
            if (auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                if (auto *filter = editor->findChild<QComboBox *>()) {
                    index = filter->currentIndex();
                    for (int i = 0; i < filter->count(); ++i) names << filter->itemText(i);
                }
                editor->accept();
            }
        });
        QVERIFY(QMetaObject::invokeMethod(chain, "itemDoubleClicked", Q_ARG(QListWidgetItem *, item)));
        QCOMPARE(index, int(ix::ResampleFilter::Nearest));
        ix::ResizeDialog resize({400, 200}, 96);
        auto *filter = resize.findChild<QComboBox *>(QStringLiteral("filter"));
        QCOMPARE(names.size(), filter->count());
        for (int i = 0; i < names.size(); ++i) QCOMPARE(names[i], filter->itemText(i));
    }
    void koreanInterfaceUsesReadableFonts() {
        const auto language = ix::uiLanguage();
        const auto appFont = QApplication::font();
        const auto restore = qScopeGuard([&] {
            ix::setUiLanguage(language);
            QApplication::setFont(appFont);
        });
        QVERIFY(ix::setUiLanguage(QStringLiteral("ko")));
        ix::configureUiFont();
        QVERIFY(ix::referenceFont(7).pointSize() >= 9);
        QVERIFY(ix::referenceFont(8).pointSize() >= 9);
        QCOMPARE(QApplication::font().families(), ix::referenceFont(9).families());
#ifdef Q_OS_WIN
        if (QFontDatabase::families().contains(QStringLiteral("Malgun Gothic"))) {
            QCOMPARE(QFontInfo(QApplication::font()).family(), QStringLiteral("Malgun Gothic"));
            QVERIFY(QFontMetrics(QApplication::font()).inFontUcs4(0xd55c)); // 한
            QVERIFY(QFontMetrics(QApplication::font()).inFontUcs4(0xae00)); // 글
        }
#endif
        ix::SettingsDialog settings;
        settings.show();
        ix::ResizeDialog resize({400, 200}, 96);
        resize.show();
        QCoreApplication::processEvents();
        QVERIFY(settings.grab().save(QStringLiteral(IX_WORKFLOW_OUTPUT "/korean-settings-font.png")));
        QVERIFY(resize.grab().save(QStringLiteral(IX_WORKFLOW_OUTPUT "/korean-resize-font.png")));
        QCOMPARE(resize.font().families(), QApplication::font().families());
        QVERIFY(resize.font().pointSize() >= 9);
        auto *filter = resize.findChild<QComboBox *>(QStringLiteral("filter"));
        QVERIFY(filter && filter->count() == 7);
        filter->setCurrentIndex(int(ix::ResampleFilter::Nearest));
        QCOMPARE(resize.options().filter, ix::ResampleFilter::Nearest);
        QVERIFY(!resize.options().fastDownscale);
    }
    void cacheClearDoesNotRestoreOlderInFlightDocument() {
        ix::ImageLoadCache::clear();
        ix::ImageLoadCache::configure(192, 16, true);
        const auto path = photo(QStringLiteral("cache-clear-race.bmp"), Qt::blue);
        QVERIFY(!path.isEmpty());
        class PausedCodec final : public ix::ImageCodec {
          public:
            mutable QSemaphore started, resume;
            mutable std::atomic_int reads = 0;
            bool accepts(const QString &) const override { return true; }
            QList<ix::FormatCapability> capabilities() const override { return {}; }
            ix::ImageResult read(const QString &path, int) const override {
                const bool first = ++reads == 1;
                ix::ImageDocument d;
                d.path = path; d.format = "bmp";
                d.pixels = QImage(16, 16, QImage::Format_RGB32);
                d.pixels.fill(first ? Qt::red : Qt::blue);
                if (first) { started.release(); resume.acquire(); }
                return {d, {}};
            }
        };
        auto codec = std::make_shared<PausedCodec>();
        ix::CodecRegistry registry;
        registry.add(codec);
        auto pending = QtConcurrent::run([registry, path] { return ix::ImageLoadCache::read(registry, path); });
        const auto finish = qScopeGuard([&] {
            codec->resume.release();
            pending.waitForFinished();
            ix::ImageLoadCache::clear();
        });
        QVERIFY(codec->started.tryAcquire(1, 5000));
        ix::ImageLoadCache::clear();
        const auto current = ix::ImageLoadCache::read(registry, path);
        QVERIFY(current.ok());
        QCOMPARE(current.document.pixels.pixelColor(0, 0), QColor(Qt::blue));
        codec->resume.release();
        pending.waitForFinished();
        QVERIFY(pending.result().ok());
        QCOMPARE(pending.result().document.pixels.pixelColor(0, 0), QColor(Qt::red));
        const auto cached = ix::ImageLoadCache::read(registry, path);
        QVERIFY(cached.ok());
        QCOMPARE(cached.document.pixels.pixelColor(0, 0), QColor(Qt::blue));
        QCOMPARE(codec->reads.load(), 2);
    }
    void documentCacheChargesRetainedPayload_data() {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("characters");
        QTest::addColumn<bool>("fits");
        QTest::addColumn<int>("budgetMiB");
        QTest::newRow("image-comment") << 0 << 600000 << false << 1;
        QTest::newRow("document-text") << 1 << 600000 << false << 1;
        QTest::newRow("metadata-text") << 2 << 600000 << false << 1;
        QTest::newRow("icc-profile") << 3 << 600000 << false << 1;
        QTest::newRow("unsupported-icc-still-retains-bytes") << 4 << 600000 << false << 1;
        QTest::newRow("small-metadata-still-caches") << 2 << 100 << true << 1;
        QTest::newRow("large-estimate-cannot-overflow-maximum-budget") << 5 << 0 << false << 512;
    }
    void documentCacheChargesRetainedPayload() {
        QFETCH(int, kind); QFETCH(int, characters); QFETCH(bool, fits);
        QFETCH(int, budgetMiB);
        ix::ImageLoadCache::clear();
        ix::ImageLoadCache::configure(budgetMiB, 0, true);
        const auto restore = qScopeGuard([] {
            ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192, 16, true);
        });
        class PayloadCodec final : public ix::ImageCodec {
          public:
            int kind = 0, characters = 0;
            mutable int reads = 0;
            bool accepts(const QString &) const override { return true; }
            QList<ix::FormatCapability> capabilities() const override { return {}; }
            ix::ImageResult read(const QString &path, int) const override {
                ++reads;
                ix::ImageDocument d;
                d.path = path; d.format = "bmp";
                d.pixels = QImage(16, 16, QImage::Format_RGB32);
                d.pixels.fill(Qt::blue);
                const QString text(characters, QLatin1Char('x'));
                if (kind == 0) d.pixels.setText(QStringLiteral("Comment"), text);
                else if (kind == 1) d.text.insert(QStringLiteral("Comment"), text);
                else if (kind == 2 || kind == 5) {
                    auto metadata = std::make_shared<ix::Metadata>();
                    if (kind == 2) metadata->tags.insert(QStringLiteral("Xmp.dc.description"), text);
                    else metadata->nativeBytes = std::numeric_limits<qint64>::max();
                    d.metadata = metadata;
                } else {
                    auto icc = QColorSpace(QColorSpace::SRgb).iccProfile();
                    icc.append(QByteArray(characters * 2, '\0'));
                    qToBigEndian(quint32(icc.size()), reinterpret_cast<uchar *>(icc.data()));
                    if (kind == 4) icc[36] = 'x';
                    d.pixels.setColorSpace(QColorSpace::fromIccProfile(icc));
                }
                return {d, {}};
            }
        };
        const auto path = photo(QStringLiteral("cache-payload.bmp"), Qt::blue);
        QVERIFY(!path.isEmpty());
        auto codec = std::make_shared<PayloadCodec>();
        codec->kind = kind; codec->characters = characters;
        ix::CodecRegistry registry;
        registry.add(codec);
        const auto first = ix::ImageLoadCache::read(registry, path);
        const auto second = ix::ImageLoadCache::read(registry, path);
        QVERIFY(first.ok() && second.ok());
        QCOMPARE(first.document.pixels, second.document.pixels);
        if (kind == 0) QCOMPARE(second.document.pixels.text(QStringLiteral("Comment")).size(), characters);
        else if (kind == 1) QCOMPARE(second.document.text.value(QStringLiteral("Comment")).size(), characters);
        else if (kind == 2) {
            QVERIFY(second.document.metadata);
            QCOMPARE(second.document.metadata->tags.value(QStringLiteral("Xmp.dc.description")).size(), characters);
        } else if (kind == 5) {
            QVERIFY(second.document.metadata);
            QCOMPARE(second.document.metadata->nativeBytes, std::numeric_limits<qint64>::max());
        } else {
            QCOMPARE(second.document.pixels.colorSpace().isValid(), kind == 3);
            QVERIFY(second.document.pixels.colorSpace().iccProfile().size() > 1024 * 1024);
        }
        QCOMPARE(codec->reads, fits ? 1 : 2);
    }
    void previewCacheChargesImageComments_data() {
        QTest::addColumn<int>("characters");
        QTest::newRow("large-comment") << 600000;
        QTest::newRow("small-comment-still-caches") << 100;
    }
    void previewCacheChargesImageComments() {
        QFETCH(int, characters);
        ix::ImageLoadCache::clear();
        ix::ImageLoadCache::configure(0, 1, true);
        const auto restore = qScopeGuard([] {
            ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192, 16, true);
        });
        const auto path = temporary_.filePath(QStringLiteral("preview-text-budget.png"));
        QImage source(64, 64, QImage::Format_RGB32);
        source.fill(Qt::blue);
        source.setText(QStringLiteral("Comment"), QString(characters, QLatin1Char('x')));
        QVERIFY(source.save(path));
        const auto first = ix::loadPreview(path, {32, 32});
        const auto second = ix::loadPreview(path, {32, 32});
        QVERIFY(!first.isNull() && !second.isNull());
        QCOMPARE(first, second);
        QCOMPARE(second.text(QStringLiteral("Comment")).size(), characters);
        QCOMPARE(first.cacheKey() == second.cacheKey(), characters == 100);
    }
    void previewsReuseOnlyEligibleCachedOriginals_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("static-png") << 0;
        QTest::newRow("animation") << 1;
        QTest::newRow("multipage") << 2;
        QTest::newRow("modified") << 3;
        QTest::newRow("raw") << 4;
        QTest::newRow("static-bmp") << 5;
        QTest::newRow("jpeg-reduced-decoder") << 6;
    }
    void previewsReuseOnlyEligibleCachedOriginals() {
        QFETCH(int, kind);
        ix::ImageLoadCache::clear();
        ix::ImageLoadCache::configure(192, 0, true);
        const auto restore = qScopeGuard([] {
            ix::ImageLoadCache::clear();
            ix::ImageLoadCache::configure(192, 16, true);
        });
        const auto path = photo(QStringLiteral("borrow-preview-%1.%2")
                                    .arg(kind).arg(kind == 5 ? QStringLiteral("bmp") : QStringLiteral("png")),
                                Qt::blue);
        QVERIFY(!path.isEmpty());
        class DecodedOriginal final : public ix::ImageCodec {
          public:
            int kind = 0;
            bool accepts(const QString &) const override { return true; }
            QList<ix::FormatCapability> capabilities() const override { return {}; }
            ix::ImageResult read(const QString &path, int) const override {
                ix::ImageDocument d;
                d.path = path;
                d.format = kind == 4 ? "dng" : kind == 5 ? "bmp" : kind == 6 ? "jpeg" : "png";
                d.animated = kind == 1;
                d.pageCount = kind == 2 ? 2 : 1;
                d.modified = kind == 3;
                d.pixels = QImage(96, 64, QImage::Format_RGBA64);
                d.pixels.fill(QColor(11, 200, 77, 123));
                d.pixels.setColorSpace(QColorSpace::DisplayP3);
                return {d, {}};
            }
        };
        ix::CodecRegistry registry;
        auto codec = std::make_shared<DecodedOriginal>();
        codec->kind = kind;
        registry.add(codec);
        const auto original = ix::ImageLoadCache::read(registry, path);
        QVERIFY(original.ok());
        const auto originalBytes = QFileInfo(path).size();
        const auto originalKey = original.document.pixels.cacheKey();
        const auto preview = ix::loadPreview(path, {30, 20});
        QVERIFY(!preview.isNull());
        if (kind == 0 || kind == 5) {
            QCOMPARE(preview, original.document.pixels.scaled(30, 20, Qt::KeepAspectRatio,
                                                              Qt::SmoothTransformation));
            QCOMPARE(preview.format(), QImage::Format_RGBA64);
            QCOMPARE(preview.colorSpace(), original.document.pixels.colorSpace());
        } else {
            QCOMPARE(preview.pixelColor(0, 0), QColor(Qt::blue));
        }
        QCOMPARE(original.document.pixels.cacheKey(), originalKey);
        // A one-pixel dimension change can produce an equally sized compressed
        // PNG in the same filesystem timestamp tick. This case needs a distinct
        // revision; same-size/preserved-mtime rewrites have a separate test.
        QImage replacement(512, 257, QImage::Format_RGB32);
        replacement.fill(Qt::red);
        QVERIFY(replacement.save(path));
        QVERIFY2(QFileInfo(path).size() != originalBytes, "Replacement fixture must change encoded size");
        QVERIFY2(ix::imageFileKey(path) != original.document.sourceRevision,
                 "Replacement fixture must have a distinct file revision before checking the cache");
        const auto refreshed = ix::loadPreview(path, {30, 20});
        QVERIFY(!refreshed.isNull());
        QCOMPARE(refreshed.pixelColor(0, 0), QColor(Qt::red));
    }
    void previewsResumeAfterStopWithoutNavigation() {
        const auto first = photo(QStringLiteral("resume/a.png"), Qt::red);
        const auto second = photo(QStringLiteral("resume/b.png"), Qt::blue);
        const QStringList files{first, second};
        ix::PreviewStrip strip;
        strip.setFiles(files, 0);
        auto *button = strip.findChild<QToolButton *>(QStringLiteral("neighborPreview"));
        QVERIFY(button);
        // Hide/show can cancel work before its queued completion is delivered.
        strip.stop();
        strip.setFiles(files, 0);
        QCOMPARE(strip.findChild<QToolButton *>(QStringLiteral("neighborPreview")), button);
        QTRY_VERIFY(button->property("previewImageKey").isValid());
        QCOMPARE(button->icon().pixmap(68, 50).toImage().pixelColor(34, 25), QColor(Qt::blue));
    }
    void randomSlideshowSkipsManuallySelectedImage() {
        const QStringList files{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
        ix::SlideshowController controller;
        const auto shown = controller.next(files, 0, true, false);
        QVERIFY(shown.path == files[1] || shown.path == files[2]);
        const int manuallySelected = shown.path == files[1] ? 2 : 1;
        const auto next = controller.next(files, manuallySelected, true, false);
        QVERIFY2(next.finished && next.path.isEmpty(), "Random round reopened the current manual selection");
        const auto repeated = controller.next(files, manuallySelected, true, true);
        QVERIFY(!repeated.finished && !repeated.path.isEmpty() && repeated.path != files[manuallySelected]);
    }
    void cachedRawDoesNotPublishPreview() {
        const auto path = QStringLiteral(IX_WORKFLOW_RAW_DIRECTORY "/pentax-k10d.dng");
        if (!QFileInfo::exists(path)) {
            if (qEnvironmentVariableIntValue("IX_REQUIRE_RAW")) QFAIL("Required RAW fixture is missing");
            QSKIP("RAW fixture not downloaded");
        }
        ix::ImageLoadCache::clear();
        ix::ImageLoadCache::configure(192, 16, true);
        const auto releaseCache = qScopeGuard([] { ix::ImageLoadCache::clear(); });
        ix::CodecRegistry registry;
        const ix::ImageLoadRequest request{path, 0, false, false, true};
        int previews = 0;
        const auto first = ix::loadImage(registry, request, [&](QImage image) {
            QVERIFY(!image.isNull());
            ++previews;
        });
        QVERIFY2(first.error.isEmpty() && !first.document.empty(), qPrintable(first.error));
        if (previews == 0) {
            if (qEnvironmentVariableIntValue("IX_REQUIRE_RAW")) QFAIL("Required embedded RAW preview unavailable");
            QSKIP("Embedded RAW preview module unavailable");
        }
        QCOMPARE(previews, 1);
        previews = 0;
        const auto cached = ix::loadImage(registry, request, [&](QImage) { ++previews; });
        QVERIFY2(cached.error.isEmpty() && !cached.document.empty(), qPrintable(cached.error));
        QCOMPARE(cached.document.pixels.cacheKey(), first.document.pixels.cacheKey());
        QCOMPARE(cached.document.sourceRevision, first.document.sourceRevision);
        QCOMPARE(previews, 0);
    }
    void explicitOpenCancelsOldFolderNavigation() {
        const auto first = photo(QStringLiteral("queued/a.png"), Qt::red);
        const auto loading = photo(QStringLiteral("queued/b.png"), Qt::green);
        const auto obsolete = photo(QStringLiteral("queued/c.png"), Qt::yellow);
        const auto chosen = photo(QStringLiteral("chosen/only.png"), Qt::blue);
        QVERIFY(!first.isEmpty() && !loading.isEmpty() && !obsolete.isEmpty() && !chosen.isEmpty());
        QSettings().remove(QStringLiteral("files/recent"));
        ix::MainWindow window;
        window.openPath(first);
        QTRY_VERIFY(!window.isBusy() && window.document().path == first);
        auto indexed = [&] {
            for (auto *label : window.findChildren<QLabel *>())
                if (label->text() == QStringLiteral("1 / 3")) return true;
            return false;
        };
        QTRY_VERIFY(indexed());
        window.openPath(loading);
        QVERIFY(window.isBusy());
        window.dispatch(QStringLiteral("next"));
        window.openPath(chosen);
        QTRY_VERIFY(!window.isBusy() && window.document().path == chosen);
        QVERIFY(!QSettings().value(QStringLiteral("files/recent")).toStringList().contains(obsolete));
    }
    void rotatedPreviewRetainsDetail() {
        QImage source(80, 320, QImage::Format_RGB32);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                source.setPixelColor(x, y, (y / 8) % 2 ? Qt::white : Qt::black);
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(source.save(&buffer, "JPEG", 100));
        for (const auto orientation : {6, 8}) {
            const auto path = temporary_.filePath(QStringLiteral("rotated-%1.jpg").arg(orientation));
            // Minimal little-endian EXIF IFD, orientation only. QImageWriter's
            // transformation can rotate pixels instead of writing an EXIF tag.
            auto exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
            exif[24] = char(orientation);
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto encoded = jpeg.left(2) + QByteArray::fromHex("ffe10022") + exif + jpeg.mid(2);
            QCOMPARE(file.write(encoded), encoded.size());
            file.close();
            QImageReader reader(path);
            QVERIFY(reader.transformation().testFlag(QImageIOHandler::TransformationRotate90));
            reader.setAutoTransform(true);
            const auto reference = reader.read().scaled(QSize(160, 40), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const auto preview = ix::loadPreview(path, {160, 40});
            QCOMPARE(preview.size(), reference.size());
            double error = 0;
            for (int y = 0; y < preview.height(); ++y)
                for (int x = 0; x < preview.width(); ++x)
                    error += std::abs(preview.pixelColor(x, y).red() - reference.pixelColor(x, y).red());
            error /= preview.width() * preview.height();
            QVERIFY2(error < 10, qPrintable(QStringLiteral("Mean luminance error: %1").arg(error)));
        }
    }
    void randomSlideshowHandlesChangedDirectory() {
        ix::SlideshowController controller;
        QStringList files;
        for (int i = 0; i < 1000; ++i)
            files.append(QString::number(i));
        QSet<QString> visited{files.first()};
        for (int i = 0; i < 30; ++i) {
            const auto next = controller.next(files, 0, true, false);
            QVERIFY(!next.finished && !next.path.isEmpty() && !visited.contains(next.path));
            visited.insert(next.path);
        }
        files.removeIf([](const QString &path) { return path.toInt() % 2; });
        for (;;) {
            const auto next = controller.next(files, 0, true, false);
            if (next.finished) break;
            QVERIFY(files.contains(next.path));
            QVERIFY(!visited.contains(next.path));
            visited.insert(next.path);
        }
        for (const auto &file : files) QVERIFY(visited.contains(file));
        const auto repeated = controller.next(files, 0, true, true);
        QVERIFY(!repeated.finished && files.contains(repeated.path) && repeated.path != files.first());
    }
    void renameRechecksSourceAfterDialog() {
        const auto path = photo(QStringLiteral("rename/before.png"), Qt::red);
        const auto target = temporary_.filePath(QStringLiteral("rename/after.png"));
        ix::MainWindow window(nullptr, temporary_.filePath(QStringLiteral("journal")));
        window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && window.document().path == path);
        bool handled = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (!dialog || handled) return;
            handled = true;
            QImage changed(113, 71, QImage::Format_RGB32);
            changed.fill(Qt::blue);
            QVERIFY(changed.save(path));
            dialog->setTextValue(QStringLiteral("after.png"));
            dialog->accept();
        });
        timer.start(10);
        window.dispatch(QStringLiteral("rename"));
        timer.stop();
        QVERIFY(handled);
        QTRY_VERIFY(!window.isBusy() && window.document().path == target);
        QTRY_COMPARE(window.document().pixels.size(), QSize(113, 71));
        QCOMPARE(window.document().pixels.pixelColor(0, 0), QColor(Qt::blue));
        QCOMPARE(window.document().sourceRevision, ix::imageFileKey(target));
        QCOMPARE(window.document().sourceModified, QFileInfo(target).lastModified());
        QVERIFY(!window.document().modified);
    }
    void exportKeepsPixelsProfileAndExistingFileOnFailure() {
        ix::ImageDocument document;
        document.pixels = QImage(79, 43, QImage::Format_RGBA64);
        document.pixels.fill(QColor(17, 129, 231, 101));
        document.pixels.setColorSpace(QColorSpace::DisplayP3);
        document.pixels.setDotsPerMeterX(11811);
        document.pixels.setDotsPerMeterY(11811);
        const auto path = temporary_.filePath(QStringLiteral("export.png"));
        ix::CodecRegistry registry;
        QVERIFY(registry.save(document, path, "png", 95, true).isEmpty());
        const auto restored = registry.read(path);
        QVERIFY(restored.ok());
        QCOMPARE(restored.document.pixels, document.pixels);
        QCOMPARE(restored.document.pixels.colorSpace(), document.pixels.colorSpace());
        QCOMPARE(restored.document.pixels.dotsPerMeterX(), document.pixels.dotsPerMeterX());
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto bytes = saved.readAll();
        saved.close();
        QVERIFY(!registry.save(document, path, "unsupported-format", 95, true).isEmpty());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), bytes);
    }
};
QTEST_MAIN(WorkflowTests)
#include "workflow_ixview.moc"
