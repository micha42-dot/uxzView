#include "core/image_storage.h"
#include "animation_fixture.h"
#include "core/image_document.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/preview_service.h"
#include "core/document_history.h"
#include "core/image_memory.h"
#include "core/image_loading.h"
#include "core/file_operations.h"
#include "core/jpeg_transform.h"
#include "core/batch_pipeline.h"
#include "core/batch_memory.h"
#include <QColorSpace>
#include "ui/main_window.h"
#include "ui/library_dialog.h"
#include "core/localization.h"
#include <QClipboard>
#include <QImageReader>
#include <QSemaphore>
#include <QSettings>
#include <QtConcurrent>
#include <QtTest>
#include <QtWidgets>
#include <thread>
#include <limits>
#include <future>
#include <functional>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#ifdef IX_TEST_ARCHIVE
#include <KZip>
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif

namespace {
// Model a native show() that dispatches completion before QDialog::exec() can
// start. Fast catalog jobs must finish through the future, never a dialog loop.
class CatalogShowProbe final : public QObject {
  public:
    bool injected = false;
    std::function<void()> progressShown;
    std::function<void(QDialog *)> resultShown;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show && object->objectName() == QStringLiteral("duplicateDialog"))
            if (auto *dialog = qobject_cast<QDialog *>(object); dialog && resultShown)
                resultShown(dialog);
        if (!injected && event->type() == QEvent::Show && qobject_cast<QProgressDialog *>(object)) {
            injected = true;
            if (progressShown) progressShown();
            QElapsedTimer timer; timer.start();
            while (timer.elapsed() < 80) {
                QCoreApplication::processEvents();
                QThread::msleep(1);
            }
        }
        return false;
    }
};
QJsonObject checkCatalogProgress(const QString &action, bool closeResultDuringShow = false) {
    QTemporaryDir folder;
    if (!folder.isValid()) return {{QStringLiteral("error"), QStringLiteral("Temporary folder failed")}};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, folder.path());
    qApp->setProperty("ixStorageData", folder.filePath(QStringLiteral("profile")));
    bool started = false, completed = false;
    int resultModality = -1, resultType = -1;
    QString resultTitle;
    // Empty catalog work normally finishes before the delayed progress window
    // appears. Hold its worker until Show so this actually exercises that race.
    auto *pool = QThreadPool::globalInstance();
    const int oldThreadCount = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore releaseWorker;
    QFuture<void> blocker;
    const auto restorePool = qScopeGuard([&] {
        releaseWorker.release();
        if (blocker.isStarted()) blocker.waitForFinished();
        pool->setMaxThreadCount(oldThreadCount);
    });
    CatalogShowProbe probe; qApp->installEventFilter(&probe);
    probe.progressShown = [&] { releaseWorker.release(); };
    probe.resultShown = [&](QDialog *matches) {
        resultModality = int(matches->windowModality());
        resultType = int(matches->windowType());
        resultTitle = matches->windowTitle();
        if (closeResultDuringShow) { completed = true; matches->reject(); }
    };
    const auto remove = qScopeGuard([&] { qApp->removeEventFilter(&probe); });
    QTimer watchdog; watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, qApp, [&] {
        std::fputs("catalog-progress-deadline\n", stderr); std::fflush(stderr);
        std::_Exit(17); // A regression must not strand the parent suite in a modal loop.
    });
    QTimer driver;
    auto findDuplicateDialog = [] {
        if (auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            if (active->objectName() == QStringLiteral("duplicateDialog"))
                return active;
        for (auto *top : QApplication::topLevelWidgets())
            if (auto *match = qobject_cast<QDialog *>(top))
                if (match->objectName() == QStringLiteral("duplicateDialog") && match->isVisible())
                    return match;
        return static_cast<QDialog *>(nullptr);
    };
    QObject::connect(&driver, &QTimer::timeout, qApp, [&] {
        if (auto *matches = findDuplicateDialog()) {
            resultModality = int(matches->windowModality());
            resultType = int(matches->windowType());
            resultTitle = matches->windowTitle();
            completed = true;
            matches->reject();
            return;
        }
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        if (dialog->objectName() != QStringLiteral("libraryDialog")) return;
        if (started && action == QStringLiteral("Metadaten indexieren")) {
            auto *status = dialog->findChild<QLabel *>(QStringLiteral("catalogStatus"));
            completed = status && !status->text().isEmpty();
        }
        if (completed) { dialog->reject(); return; }
        if (!started && dialog->property("catalogReady").toBool())
            for (auto *button : dialog->findChildren<QAbstractButton *>())
                if (button->text() == ix::localized(action)) {
                    started = true;
                    blocker = QtConcurrent::run(pool, [&] { releaseWorker.acquire(); });
                    QTimer::singleShot(0, button, [button] { button->click(); });
                    break;
                }
    });
    driver.start(5); watchdog.start(5000);
    ix::libraryDialog(nullptr, {}, {});
    driver.stop(); watchdog.stop();
    return {{QStringLiteral("action"), action},
            {QStringLiteral("result_modality"), resultModality},
            {QStringLiteral("result_type"), resultType},
            {QStringLiteral("result_title"), resultTitle},
            {QStringLiteral("started"), started}, {QStringLiteral("completed"), completed},
            {QStringLiteral("show_injected"), probe.injected}};
}
QJsonObject checkSmallCacheBudget() {
    ix::initializeImageMemoryLimit(8);
    QTemporaryDir folder;
    if (!folder.isValid()) return {{QStringLiteral("error"),QStringLiteral("Temporary folder failed")}};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,folder.path());
    qApp->setProperty("ixStorageData",folder.filePath(QStringLiteral("profile")));
    ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(512,64,true);
    QImage pixels(512,384,QImage::Format_RGB32); pixels.fill(Qt::blue);
    QStringList paths;
    for (int i=0;i<12;++i) {
        const auto path=folder.filePath(QStringLiteral("source-%1.bmp").arg(i));
        if (!pixels.save(path)) return {{QStringLiteral("error"),QStringLiteral("Fixture failed")}};
        paths.append(path);
    }
    for (int i=0;i<3;++i) if (ix::loadPreview(paths[i],{512,384}).isNull())
        return {{QStringLiteral("error"),QStringLiteral("Preview failed")}};
    const auto previewBefore=ix::ImageLoadCache::usage().previews;
    ix::CodecRegistry registry;
    qint64 maximum=0;
    for (const auto &path:paths) {
        const auto read=ix::ImageLoadCache::read(registry,path);
        if (!read.ok()) return {{QStringLiteral("error"),read.error}};
        const auto usage=ix::ImageLoadCache::usage(); maximum=std::max(maximum,usage.total());
        if (usage.total()>8*ix::MiB || usage.total()!=ix::ImageLoadCache::retainedBytes())
            return {{QStringLiteral("error"),QStringLiteral("Aggregate cache budget exceeded")}};
    }
    const auto usage=ix::ImageLoadCache::usage();
    if (previewBefore<=0 || usage.previews>=previewBefore || usage.budget!=8*ix::MiB)
        return {{QStringLiteral("error"),QStringLiteral("Original cache did not reclaim preview capacity")}};
    ix::ImageLoadCache::clear();
    if (ix::ImageLoadCache::retainedBytes()!=0) return {{QStringLiteral("error"),QStringLiteral("Cache charge leaked")}};
    return {{QStringLiteral("maximum"),maximum},{QStringLiteral("budget"),usage.budget}};
}
QJsonObject processMemory() {
    QJsonObject result;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX memory{};
    using Read = BOOL (WINAPI *)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    const auto read = reinterpret_cast<Read>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
    if (read && read(GetCurrentProcess(), reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&memory), sizeof(memory))) {
        result.insert(QStringLiteral("private_bytes"), qint64(memory.PrivateUsage));
        result.insert(QStringLiteral("working_set_bytes"), qint64(memory.WorkingSetSize));
        result.insert(QStringLiteral("peak_working_set_bytes"), qint64(memory.PeakWorkingSetSize));
    }
#endif
    return result;
}
QJsonObject profileDocumentRelease() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return {{QStringLiteral("error"), QStringLiteral("Temporary directory failed")}};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings().setValue(QStringLiteral("performance/imageCacheMiB"), 0);
    QSettings().setValue(QStringLiteral("performance/previewCacheMiB"), 0);
    const auto path = QDir::current().filePath(QStringLiteral("build/audit-fixtures/large.png"));
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!QFileInfo::exists(path)) {
        QImage image(6000, 4000, QImage::Format_RGB32); image.fill(QColor(37, 129, 211));
        if (!image.save(path)) return {{QStringLiteral("error"), QStringLiteral("Fixture write failed")}};
    }
    ix::MainWindow window;
    QJsonArray samples;
    const auto configured = qEnvironmentVariableIntValue("IX_AUDIT_MEMORY_CYCLES");
    const int cycles = configured > 0 ? std::clamp(configured, 1, 100) : 3;
    for (int pass = 0; pass < cycles; ++pass) {
        QElapsedTimer timer; timer.start();
        window.openPath(path);
        while (window.isBusy() && timer.elapsed() < 15000) {
            QApplication::processEvents(); QThread::msleep(1);
        }
        if (window.isBusy() || window.document().empty()) return {{QStringLiteral("error"), QStringLiteral("Load failed")}};
        const auto loadMs = timer.nsecsElapsed() / 1e6;
        auto loaded = processMemory();
        const auto imageBytes = window.document().pixels.sizeInBytes();
        auto previous = window.document().pixels;
        QImage tiny(3, 2, QImage::Format_RGB32); tiny.fill(Qt::red);
        QApplication::clipboard()->setImage(tiny);
        window.dispatch(QStringLiteral("paste"));
        ix::ImageLoadCache::clear();
        const bool released = previous.isDetached();
        previous = {};
        QApplication::processEvents();
        samples.append(QJsonObject{{QStringLiteral("load_ms"), loadMs}, {QStringLiteral("loaded"), loaded},
            {QStringLiteral("replaced"), processMemory()}, {QStringLiteral("image_bytes"), imageBytes},
            {QStringLiteral("old_pixels_released"), released}});
    }
    QApplication::clipboard()->clear();
    return {{QStringLiteral("samples"), samples}, {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("note"), QStringLiteral("6000x4000 RGB PNG, hidden window, document/thumbnail caches disabled. Default three uncached loads per fresh process, configurable with IX_AUDIT_MEMORY_CYCLES; first includes process initialization, OS cache not flushed. Pixel sharing checked before releasing the observer; process memory sampled afterward. Working set includes allocator/plugin overhead and is not live-buffer accounting.")}};
}
}

class MaintenanceTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_;
  private slots:
    void catalogCompletionDuringShowCannotStrandDialog_data() {
        // ASCII ids on the child command line: the German labels contain an
        // umlaut ("Ähnliche Bilder") and shell/runner quoting has corrupted
        // non-ASCII arguments before. The child maps the id back to the label.
        QTest::addColumn<QString>("actionId");
        QTest::addColumn<bool>("closeResultDuringShow");
        QTest::newRow("index") << QStringLiteral("index") << false;
        QTest::newRow("exact") << QStringLiteral("exact") << false;
        QTest::newRow("similar") << QStringLiteral("similar") << false;
        QTest::newRow("exact-close-on-show") << QStringLiteral("exact") << true;
        QTest::newRow("similar-close-on-show") << QStringLiteral("similar") << true;
    }
    void catalogCompletionDuringShowCannotStrandDialog() {
        QFETCH(QString, actionId);
        QFETCH(bool, closeResultDuringShow);
        const QString action = actionId == QStringLiteral("index")
                                   ? QStringLiteral("Metadaten indexieren")
                               : actionId == QStringLiteral("exact")
                                   ? QStringLiteral("Identische Bilder")
                                   : QStringLiteral("Ähnliche Bilder");
        QProcess child;
        QElapsedTimer clock;
        clock.start();
        QStringList arguments{QStringLiteral("--catalog-progress-probe"), actionId,
                              QStringLiteral("-platform"), QGuiApplication::platformName()};
        if (closeResultDuringShow) arguments.append(QStringLiteral("--close-result-on-show"));
        child.start(QCoreApplication::applicationFilePath(), arguments);
        const bool finished = child.waitForFinished(15000);
        const auto elapsedMs = clock.elapsed();
        if (!finished) { child.kill(); child.waitForFinished(3000); }
        const auto childStderr = child.readAllStandardError();
        const auto childStdout = child.readAllStandardOutput();
        const auto diagnosis =
            QStringLiteral("action=%1 finished=%2 exitStatus=%3 exitCode=%4 elapsedMs=%5 stdoutBytes=%6 stderrBytes=%7\nstdout=%8\nstderr=%9")
                .arg(action)
                .arg(finished)
                .arg(child.exitStatus())
                .arg(child.exitCode())
                .arg(elapsedMs)
                .arg(childStdout.size())
                .arg(childStderr.size())
                .arg(QString::fromUtf8(childStdout))
                .arg(QString::fromUtf8(childStderr));
        QVERIFY2(finished, qPrintable(diagnosis));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, qPrintable(diagnosis));
        const auto report = QJsonDocument::fromJson(childStdout).object();
        QCOMPARE(report.value(QStringLiteral("action")).toString(), action);
        QVERIFY2(report.value(QStringLiteral("started")).toBool(), qPrintable(diagnosis));
        QVERIFY2(report.value(QStringLiteral("completed")).toBool(), qPrintable(diagnosis));
        QVERIFY2(report.value(QStringLiteral("show_injected")).toBool(), qPrintable(diagnosis));
        if (actionId != QStringLiteral("index")) {
            QCOMPARE(report.value(QStringLiteral("result_modality")).toInt(), int(Qt::ApplicationModal));
            QCOMPARE(report.value(QStringLiteral("result_type")).toInt(), int(Qt::Dialog));
            const auto title = actionId == QStringLiteral("exact")
                ? ix::localized(QStringLiteral("Identische Dateien (SHA-256)"))
                : ix::localized(QStringLiteral("Ähnliche Bilder – Prüfkandidaten"));
            QCOMPARE(report.value(QStringLiteral("result_title")).toString(), title);
        }
    }
    void cancelledLoadSkipsQueuedReadAndPreDecodeWork() {
        ix::ImageLoadCache::clear(); ix::CodecRegistry registry;
        const auto path=temporary_.filePath(QStringLiteral("cancel-before.png"));
        QImage pixels(32,24,QImage::Format_RGB32); pixels.fill(Qt::green); QVERIFY(pixels.save(path));
        std::atomic_bool cancel=true; int before=0;
        QVERIFY(!ix::loadImage(registry,{path,0,true,true,true},{},&cancel));
        QVERIFY(!ix::ImageLoadCache::read(registry,path,0,[&] { ++before; },&cancel));
        QCOMPARE(before,0); QCOMPARE(ix::ImageLoadCache::retainedBytes(),0);
        cancel=false;
        QVERIFY(!ix::ImageLoadCache::read(registry,path,0,[&] { ++before; cancel=true; },&cancel));
        QCOMPARE(before,1); QCOMPARE(ix::ImageLoadCache::retainedBytes(),0);
        cancel=false;
        const auto valid=ix::loadImage(registry,{path,0,true,false,false},{},&cancel);
        QVERIFY(valid); QCOMPARE(valid->document.pixels,pixels);
        const auto invalid=ix::loadImage(registry,{path+QStringLiteral(".missing"),0,true,false,false},{},&cancel);
        QVERIFY(invalid); QVERIFY(!invalid->error.isEmpty());
        ix::ImageLoadCache::clear();
    }
    void cancelledDecodedLoadSkipsDisplayAndKeepsBoundedOriginal_data() {
        QTest::addColumn<bool>("cache"); QTest::newRow("cache") << true; QTest::newRow("no-cache") << false;
    }
    void cancelledDecodedLoadSkipsDisplayAndKeepsBoundedOriginal() {
        QFETCH(bool,cache);
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(cache ? 16 : 0,0,true);
        const auto restore=qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192,16,true); });
        const auto path=temporary_.filePath(QStringLiteral("cancel-after.owned"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("controlled source"); file.close();
        class CancellingCodec final : public ix::ImageCodec {
          public:
            std::atomic_bool *cancel; mutable int reads=0; QImage pixels;
            bool accepts(const QString &path) const override { return path.endsWith(QStringLiteral(".owned")); }
            QList<ix::FormatCapability> capabilities() const override { return {}; }
            ix::ImageResult read(const QString &,int) const override {
                ++reads; cancel->store(true); ix::ImageDocument document;
                document.pixels=pixels; document.format="png"; return {document,{}};
            }
        };
        std::atomic_bool cancel=false; auto codec=std::make_shared<CancellingCodec>(); codec->cancel=&cancel;
        codec->pixels=QImage(1024,768,QImage::Format_RGBA64); codec->pixels.fill(QColor(30,110,220,137));
        codec->pixels.setColorSpace(QColorSpace::DisplayP3);
        ix::CodecRegistry registry; registry.add(codec);
        QVERIFY(!ix::loadImage(registry,{path,0,true,true,false},{},&cancel));
        QCOMPARE(codec->reads,1); QCOMPARE(ix::ImageLoadCache::usage().presentations,0);
        if(cache) {
            QVERIFY(ix::ImageLoadCache::usage().originals>0); cancel=false;
            const auto reused=ix::loadImage(registry,{path,0,false,false,false},{},&cancel);
            QVERIFY(reused); QCOMPARE(reused->document.pixels.cacheKey(),codec->pixels.cacheKey()); QCOMPARE(codec->reads,1);
        } else QCOMPARE(ix::ImageLoadCache::retainedBytes(),0);
    }
    void supersededQueuedOpenKeepsLatestWithoutInstallingStale() {
        ix::ImageLoadCache::clear();
        QSettings settings; const auto oldCache=settings.value(QStringLiteral("performance/imageCacheMiB"));
        settings.setValue(QStringLiteral("performance/imageCacheMiB"),32);
        const auto restore=qScopeGuard([&] { settings.setValue(QStringLiteral("performance/imageCacheMiB"),oldCache); ix::ImageLoadCache::clear(); });
        QStringList paths;
        for (int i=0;i<3;++i) {
            const auto path=temporary_.filePath(QStringLiteral("supersede-%1.png").arg(i));
            QImage image(64,48,QImage::Format_RGB32); image.fill(i==2 ? Qt::blue : Qt::red); QVERIFY(image.save(path)); paths.append(path);
        }
        ix::MainWindow window;
        QStringList installed; auto *view=window.findChild<ix::ImageView *>(); QVERIFY(view);
        connect(view,&ix::ImageView::selectionChanged,&window,[&] { if(!window.document().path.isEmpty())installed.append(window.document().path); });
        auto *pool=QThreadPool::globalInstance(); pool->waitForDone(); const int threads=pool->maxThreadCount(); pool->setMaxThreadCount(1);
        QSemaphore entered,resume; auto block=QtConcurrent::run([&] { entered.release(); resume.acquire(); });
        const auto unblock=qScopeGuard([&] { resume.release(); block.waitForFinished(); pool->waitForDone(); pool->setMaxThreadCount(threads); });
        QVERIFY(entered.tryAcquire(1,3000));
        window.openPath(paths[0]); QVERIFY(window.isBusy()); window.openPath(paths[1]); window.openPath(paths[2]);
        resume.release(); block.waitForFinished();
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path==paths[2],10000);
        QVERIFY(!installed.contains(paths[0])); QVERIFY(!installed.contains(paths[1]));
        QCOMPARE(window.document().pixels.pixelColor(0,0),QColor(Qt::blue));
        ix::CodecRegistry registry; int misses=0;
        QVERIFY(ix::ImageLoadCache::read(registry,paths[0],0,[&] { ++misses; }).ok()); QCOMPARE(misses,1);
    }
    void queuedArrowNavigationStillCompletesIntermediateLoad() {
        QStringList paths; const auto folder=temporary_.filePath(QStringLiteral("queued-arrows")); QVERIFY(QDir().mkpath(folder));
        for(int i=0;i<3;++i) {
            const auto path=folder+QStringLiteral("/%1.png").arg(i);
            QImage image(64,48,QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(path)); paths.append(path);
        }
        ix::MainWindow window; window.openPath(paths[0]);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path==paths[0],10000);
        auto *pool=QThreadPool::globalInstance(); pool->waitForDone(); const int threads=pool->maxThreadCount(); pool->setMaxThreadCount(1);
        QSemaphore entered,resume; auto block=QtConcurrent::run([&] { entered.release(); resume.acquire(); });
        const auto unblock=qScopeGuard([&] { resume.release(); block.waitForFinished(); pool->waitForDone(); pool->setMaxThreadCount(threads); });
        QVERIFY(entered.tryAcquire(1,3000));
        window.openPath(paths[1]); QVERIFY(window.isBusy()); window.dispatch(QStringLiteral("next"));
        resume.release(); block.waitForFinished();
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path==paths[2],10000);
    }
    void supersededOpenFailurePreservesVisibleDocument() {
        const auto current=temporary_.filePath(QStringLiteral("replace-current.png")),
                   stale=temporary_.filePath(QStringLiteral("replace-stale.png")),
                   missing=temporary_.filePath(QStringLiteral("replace-missing.png"));
        QImage pixels(64,48,QImage::Format_RGB32); pixels.fill(Qt::green); QVERIFY(pixels.save(current));
        pixels.fill(Qt::red); QVERIFY(pixels.save(stale));
        ix::MainWindow window; window.openPath(current);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path==current,10000);
        auto *view=window.findChild<ix::ImageView *>(); QVERIFY(view); const auto visible=view->image();
        view->setZoom(2); view->setSelection({2,3,10,9});
        auto *pool=QThreadPool::globalInstance(); pool->waitForDone(); const int threads=pool->maxThreadCount(); pool->setMaxThreadCount(1);
        QSemaphore entered,resume; auto block=QtConcurrent::run([&] { entered.release(); resume.acquire(); });
        const auto unblock=qScopeGuard([&] { resume.release(); block.waitForFinished(); pool->waitForDone(); pool->setMaxThreadCount(threads); });
        QVERIFY(entered.tryAcquire(1,3000));
        QStringList messages; QTimer accept; accept.setInterval(10);
        connect(&accept,&QTimer::timeout,&window,[&] {
            if(auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                messages.append(message->text()); message->accept();
            }
        });
        accept.start(); window.openPath(stale); window.openPath(missing); resume.release(); block.waitForFinished();
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty() && !window.isBusy(),10000); accept.stop();
        QCOMPARE(messages.size(),1); QVERIFY(messages[0].contains(missing));
        QCOMPARE(window.document().path,current); QCOMPARE(view->image().cacheKey(),visible.cacheKey());
        QCOMPARE(view->selection(),QRect(2,3,10,9)); QCOMPARE(view->zoom(),2.0);
    }
    void dirtyDocumentRetainsExistingDeferredOpenFlow() {
        QSettings settings; const auto oldAsk=settings.value(QStringLiteral("navigation/askToSave"));
        settings.setValue(QStringLiteral("navigation/askToSave"),false);
        const auto restore=qScopeGuard([&] {
            if(oldAsk.isValid()) settings.setValue(QStringLiteral("navigation/askToSave"),oldAsk);
            else settings.remove(QStringLiteral("navigation/askToSave"));
        });
        QStringList paths;
        for(int i=0;i<3;++i) {
            const auto path=temporary_.filePath(QStringLiteral("dirty-deferred-%1.png").arg(i));
            QImage pixels(64,48,QImage::Format_RGB32); pixels.fill(Qt::green); QVERIFY(pixels.save(path)); paths.append(path);
        }
        ix::MainWindow window; window.openPath(paths[0]);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path==paths[0],10000);
        window.dispatch(QStringLiteral("grayscale")); QTRY_VERIFY(!window.isBusy() && window.document().modified);
        QStringList installed; auto *view=window.findChild<ix::ImageView *>(); QVERIFY(view);
        connect(view,&ix::ImageView::selectionChanged,&window,[&] { installed.append(window.document().path); });
        auto *pool=QThreadPool::globalInstance(); pool->waitForDone(); const int threads=pool->maxThreadCount(); pool->setMaxThreadCount(1);
        QSemaphore entered,resume; auto block=QtConcurrent::run([&] { entered.release(); resume.acquire(); });
        const auto unblock=qScopeGuard([&] { resume.release(); block.waitForFinished(); pool->waitForDone(); pool->setMaxThreadCount(threads); });
        QVERIFY(entered.tryAcquire(1,3000));
        window.openPath(paths[1]); window.openPath(paths[2]); resume.release(); block.waitForFinished();
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path==paths[2],10000);
        QVERIFY(installed.contains(paths[1]));
    }
    void simultaneousPixelReadsShareOneBuffer() {
        ix::ImageLoadCache::clear();
        const auto path=temporary_.filePath(QStringLiteral("shared-active.png"));
        QImage pixels(1024,768,QImage::Format_RGB32); pixels.fill(Qt::red); QVERIFY(pixels.save(path));
        QSemaphore entered,resume; std::atomic_int calls=0;
        std::future<ix::ImageLoadCache::PreviewPixels> owner,waiting;
        const auto join=qScopeGuard([&] { resume.release(); if(owner.valid())owner.wait(); if(waiting.valid())waiting.wait(); });
        owner=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::decodePreviewPixels(path,pixels.size(),pixels.format(),"png",[&] {
                ++calls; entered.release(); resume.acquire(); return ix::ImageLoadCache::PreviewPixels{pixels,37,2};
            });
        });
        QVERIFY(entered.tryAcquire(1,3000));
        waiting=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::decodePreviewPixels(path,pixels.size(),pixels.format(),"png",[&] {
                ++calls; return ix::ImageLoadCache::PreviewPixels{pixels.copy(),99,4};
            });
        });
        QTRY_COMPARE_WITH_TIMEOUT(ix::ImageLoadCache::decodeActivity().waiting,1,3000);
        QCOMPARE(ix::ImageLoadCache::decodeActivity().active,1);
        resume.release(); const auto first=owner.get(),second=waiting.get();
        QCOMPARE(calls.load(),1); QCOMPARE(first.pixels.cacheKey(),second.pixels.cacheKey());
        QCOMPARE(second.delayMs,37); QCOMPARE(second.loopCount,2);
        QCOMPARE(ix::ImageLoadCache::decodeActivity().active,0); QCOMPARE(ix::ImageLoadCache::decodeActivity().waiting,0);
        QCOMPARE(ix::ImageLoadCache::retainedBytes(),0);
    }
    void cancelledPixelWaitDoesNotCancelOwner() {
        const auto path=temporary_.filePath(QStringLiteral("shared-cancel.png"));
        QImage pixels(1024,768,QImage::Format_RGB32); pixels.fill(Qt::green); QVERIFY(pixels.save(path));
        QSemaphore entered,resume; std::atomic_bool cancel=false; std::atomic_int calls=0;
        std::future<ix::ImageLoadCache::PreviewPixels> owner,waiting;
        const auto join=qScopeGuard([&] { cancel=true; resume.release(); if(owner.valid())owner.wait(); if(waiting.valid())waiting.wait(); });
        owner=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::decodePreviewPixels(path,pixels.size(),pixels.format(),"png",[&] {
                ++calls; entered.release(); resume.acquire(); return ix::ImageLoadCache::PreviewPixels{pixels};
            });
        });
        QVERIFY(entered.tryAcquire(1,3000));
        waiting=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::decodePreviewPixels(path,pixels.size(),pixels.format(),"png",[&] {
                ++calls; return ix::ImageLoadCache::PreviewPixels{pixels.copy()};
            },&cancel);
        });
        QTRY_COMPARE_WITH_TIMEOUT(ix::ImageLoadCache::decodeActivity().waiting,1,3000);
        cancel=true; QTRY_COMPARE_WITH_TIMEOUT(ix::ImageLoadCache::decodeActivity().waiting,0,3000);
        QVERIFY(waiting.get().pixels.isNull()); QCOMPARE(ix::ImageLoadCache::decodeActivity().active,1);
        resume.release(); QCOMPARE(owner.get().pixels,pixels); QCOMPARE(calls.load(),1);
        QCOMPARE(ix::ImageLoadCache::decodeActivity().active,0);
    }
    void sharedPixelGenerationsStayIndependent_data() {
        QTest::addColumn<int>("change");
        QTest::newRow("clear") << 0; QTest::newRow("invalidate") << 1; QTest::newRow("rewrite") << 2;
    }
    void sharedPixelGenerationsStayIndependent() {
        QFETCH(int,change);
        const auto path=temporary_.filePath(QStringLiteral("shared-generation.png"));
        QImage before(1024,768,QImage::Format_RGB32),after(before.size(),before.format());
        before.fill(Qt::red); after.fill(Qt::blue); QVERIFY(before.save(path));
        QSemaphore entered,oldResume,newEntered,newResume; std::atomic_int calls=0;
        std::future<ix::ImageLoadCache::PreviewPixels> old,fresh,waiting;
        const auto join=qScopeGuard([&] { oldResume.release(); newResume.release();
            for(auto *future : {&old,&fresh,&waiting}) if(future->valid())future->wait(); });
        old=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::decodePreviewPixels(path,before.size(),before.format(),"png",[&] {
                ++calls; entered.release(); oldResume.acquire(); return ix::ImageLoadCache::PreviewPixels{before};
            });
        });
        QVERIFY(entered.tryAcquire(1,3000));
        if(change==0) ix::ImageLoadCache::clear();
        else if(change==1) ix::ImageLoadCache::invalidate({path});
        else QVERIFY(after.save(path));
        fresh=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::decodePreviewPixels(path,after.size(),after.format(),"png",[&] {
                ++calls; newEntered.release(); newResume.acquire(); return ix::ImageLoadCache::PreviewPixels{after};
            });
        });
        QVERIFY(newEntered.tryAcquire(1,3000)); QCOMPARE(ix::ImageLoadCache::decodeActivity().active,2);
        oldResume.release(); QCOMPARE(old.get().pixels,before);
        // Finishing the old generation must not remove the fresh owner's entry.
        waiting=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::decodePreviewPixels(path,after.size(),after.format(),"png",[&] {
                ++calls; return ix::ImageLoadCache::PreviewPixels{before};
            });
        });
        QTRY_COMPARE_WITH_TIMEOUT(ix::ImageLoadCache::decodeActivity().waiting,1,3000);
        newResume.release(); const auto result=fresh.get(); QCOMPARE(waiting.get().pixels.cacheKey(),result.pixels.cacheKey());
        QCOMPARE(result.pixels,after); QCOMPARE(calls.load(),2); QCOMPARE(ix::ImageLoadCache::decodeActivity().active,0);
    }
    void failedPixelOwnerWakesWaitersAndAllowsRetry_data() {
        QTest::addColumn<bool>("exception"); QTest::newRow("null") << false; QTest::newRow("exception") << true;
    }
    void failedPixelOwnerWakesWaitersAndAllowsRetry() {
        QFETCH(bool,exception);
        const auto path=temporary_.filePath(QStringLiteral("shared-failure.png"));
        QImage pixels(1024,768,QImage::Format_RGB32); pixels.fill(Qt::blue); QVERIFY(pixels.save(path));
        QSemaphore entered,resume; std::atomic_int calls=0;
        std::future<ix::ImageLoadCache::PreviewPixels> owner,waiting;
        const auto join=qScopeGuard([&] { resume.release(); if(owner.valid())owner.wait(); if(waiting.valid())waiting.wait(); });
        owner=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::decodePreviewPixels(path,pixels.size(),pixels.format(),"png",[&]() -> ix::ImageLoadCache::PreviewPixels {
                ++calls; entered.release(); resume.acquire(); if(exception) throw std::runtime_error("test decode failure"); return {};
            });
        });
        QVERIFY(entered.tryAcquire(1,3000));
        waiting=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::decodePreviewPixels(path,pixels.size(),pixels.format(),"png",[&] {
                ++calls; return ix::ImageLoadCache::PreviewPixels{pixels};
            });
        });
        QTRY_COMPARE_WITH_TIMEOUT(ix::ImageLoadCache::decodeActivity().waiting,1,3000); resume.release();
        if(exception) QVERIFY_THROWS_EXCEPTION(std::runtime_error,owner.get()); else QVERIFY(owner.get().pixels.isNull());
        QCOMPARE(waiting.get().pixels,pixels); QCOMPARE(calls.load(),2);
        QCOMPARE(ix::ImageLoadCache::decodeActivity().active,0); QCOMPARE(ix::ImageLoadCache::decodeActivity().waiting,0);
    }
    void recursivePixelDecodeDoesNotWaitOnItself() {
        const auto path=temporary_.filePath(QStringLiteral("shared-recursive.png"));
        QImage pixels(1024,768,QImage::Format_RGB32); pixels.fill(Qt::blue); QVERIFY(pixels.save(path));
        int calls=0;
        const auto result=ix::ImageLoadCache::decodePreviewPixels(path,pixels.size(),pixels.format(),"png",[&] {
            ++calls; return ix::ImageLoadCache::decodePreviewPixels(path,pixels.size(),pixels.format(),"png",[&] {
                ++calls; return ix::ImageLoadCache::PreviewPixels{pixels};
            });
        });
        QCOMPARE(result.pixels,pixels); QCOMPARE(calls,2); QCOMPARE(ix::ImageLoadCache::decodeActivity().active,0);
    }
    void sharedPixelOwnerRestoresWorkerPriority() {
        const auto path=temporary_.filePath(QStringLiteral("shared-priority.png"));
        QImage pixels(1024,768,QImage::Format_RGB32); pixels.fill(Qt::blue); QVERIFY(pixels.save(path));
        QSemaphore entered,resume; std::atomic_int restored=-1;
        std::future<ix::ImageLoadCache::PreviewPixels> waiting;
        const auto thread=std::unique_ptr<QThread>(QThread::create([&] {
            ix::ImageLoadCache::decodePreviewPixels(path,pixels.size(),pixels.format(),"png",[&] {
                entered.release(); resume.acquire(); return ix::ImageLoadCache::PreviewPixels{pixels};
            });
            restored=int(QThread::currentThread()->priority());
        }));
        const auto join=qScopeGuard([&] { resume.release(); thread->wait(); if(waiting.valid())waiting.wait(); });
        thread->start(QThread::LowPriority); QVERIFY(entered.tryAcquire(1,3000));
        waiting=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::decodePreviewPixels(path,pixels.size(),pixels.format(),"png",[&] {
                return ix::ImageLoadCache::PreviewPixels{pixels.copy()};
            });
        });
        QTRY_COMPARE_WITH_TIMEOUT(ix::ImageLoadCache::decodeActivity().waiting,1,3000);
#ifdef Q_OS_WIN
        QCOMPARE(thread->priority(),QThread::NormalPriority);
#endif
        resume.release(); QCOMPARE(waiting.get().pixels.cacheKey(),pixels.cacheKey());
        QVERIFY(thread->wait(3000));
#ifdef Q_OS_WIN
        QCOMPARE(restored.load(),int(QThread::LowPriority));
#endif
        QCOMPARE(ix::ImageLoadCache::decodeActivity().active,0);
    }
    void decodedPreviewPreservesPixelsAndForegroundProperties_data() {
        QTest::addColumn<QByteArray>("codec"); QTest::addColumn<int>("format");
        QTest::newRow("png-rgb") << QByteArray("png") << int(QImage::Format_RGB32);
        QTest::newRow("png-rgba64") << QByteArray("png") << int(QImage::Format_RGBA64);
        QTest::newRow("png-gray16") << QByteArray("png") << int(QImage::Format_Grayscale16);
        QTest::newRow("bmp") << QByteArray("bmp") << int(QImage::Format_RGB32);
    }
    void decodedPreviewPreservesPixelsAndForegroundProperties() {
        QFETCH(QByteArray,codec); QFETCH(int,format);
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(32,1,true);
        const auto restore=qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192,16,true); });
        const auto path=temporary_.filePath(QStringLiteral("decoded-preview.")+QString::fromLatin1(codec));
        QImage source(1024,768,QImage::Format(format));
        for (int y=0;y<source.height();++y) for(int x=0;x<source.width();++x)
            source.setPixelColor(x,y,QColor((x*17+y)%256,(x+y*29)%256,(x*3+y*7)%256,format==QImage::Format_RGBA64 ? (x+y)%256 : 255));
        if (format!=QImage::Format_Grayscale16) source.setColorSpace(QColorSpace::DisplayP3);
        source.setDotsPerMeterX(4300); source.setDotsPerMeterY(4700);
        source.setText(QStringLiteral("Description"),QStringLiteral("Literal preview metadata <test>"));
        QVERIFY(source.save(path,codec.constData())); const auto originalHash=ix::sha256File(path);
        ix::CodecRegistry registry; const auto reference=registry.read(path); QVERIFY(reference.ok());
        QImageReader legacy(path); legacy.setAutoTransform(true);
        legacy.setScaledSize(legacy.size().scaled({68,50},Qt::KeepAspectRatio));
        const auto expectedThumb=legacy.read().scaled({68,50},Qt::KeepAspectRatio,Qt::SmoothTransformation);
        QCOMPARE(ix::loadPreview(path,{68,50}),expectedThumb);
        QVERIFY(ix::ImageLoadCache::usage().decodedPreviews>=reference.document.pixels.sizeInBytes());
        const auto taken=ix::ImageLoadCache::takePreviewPixels(path,source.size(),reference.document.pixels.format(),codec);
        QVERIFY(taken); QCOMPARE(taken->pixels,reference.document.pixels);
        // A second thumbnail size exercises a fresh decode rather than a cached thumbnail.
        QVERIFY(!ix::loadPreview(path,{69,51}).isNull());
        QVERIFY(ix::ImageLoadCache::usage().decodedPreviews>0);
        const auto actual=ix::ImageLoadCache::read(registry,path); QVERIFY2(actual.ok(),qPrintable(actual.error));
        const auto &a=actual.document, &b=reference.document;
        QCOMPARE(a.pixels,b.pixels); QCOMPARE(a.pixels.format(),b.pixels.format());
        QCOMPARE(a.pixels.colorSpace(),b.pixels.colorSpace()); QCOMPARE(a.text,b.text);
        QCOMPARE(a.dpiX(),b.dpiX()); QCOMPARE(a.dpiY(),b.dpiY()); QCOMPARE(a.fileBytes,b.fileBytes);
        QCOMPARE(a.pageCount,b.pageCount); QCOMPARE(a.delayMs,b.delayMs); QCOMPARE(a.loopCount,b.loopCount);
        QCOMPARE(a.sourceHasExif,b.sourceHasExif); QCOMPARE(a.metadataWarning,b.metadataWarning);
        QCOMPARE(bool(a.metadata),bool(b.metadata)); if(a.metadata) QCOMPARE(a.metadata->tags,b.metadata->tags);
        QCOMPARE(ix::ImageLoadCache::usage().decodedPreviews,0);
        QCOMPARE(ix::ImageLoadCache::read(registry,path).document.pixels.cacheKey(),a.pixels.cacheKey());
        QCOMPARE(ix::sha256File(path),originalHash);
    }
    void decodedPreviewPriorityBudgetsAndInvalidation() {
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(4,1,true);
        const auto restore=qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192,16,true); });
        const auto nearPath=temporary_.filePath(QStringLiteral("handoff-nearPath.png")), farPath=temporary_.filePath(QStringLiteral("handoff-farPath.png"));
        QImage image(1024,768,QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(nearPath)); QVERIFY(image.save(farPath));
        QVERIFY(!ix::loadPreview(nearPath,{68,50},nullptr,0).isNull());
        QVERIFY(!ix::loadPreview(farPath,{68,50},nullptr,5).isNull());
        QVERIFY(!ix::ImageLoadCache::takePreviewPixels(farPath,image.size(),image.format(),"png"));
        QVERIFY(ix::ImageLoadCache::takePreviewPixels(nearPath,image.size(),image.format(),"png"));
        QVERIFY(!ix::loadPreview(nearPath,{69,51}).isNull());
        ix::CodecRegistry registry; QVERIFY(ix::ImageLoadCache::read(registry,farPath).ok());
        const auto usage=ix::ImageLoadCache::usage(); QCOMPARE(usage.decodedPreviews,0);
        QVERIFY(usage.originals>0); QVERIFY(usage.total()<=usage.budget);
        QCOMPARE(usage.total(),ix::ImageLoadCache::retainedBytes());
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(16,1,true);
        QVERIFY(!ix::loadPreview(nearPath).isNull()); QVERIFY(!ix::loadPreview(farPath).isNull());
        ix::ImageLoadCache::invalidate({farPath}); QVERIFY(ix::ImageLoadCache::usage().decodedPreviews>0);
        image.fill(Qt::blue); QVERIFY(image.save(nearPath));
        QVERIFY(!ix::ImageLoadCache::takePreviewPixels(nearPath,image.size(),image.format(),"png"));
        QCOMPARE(ix::ImageLoadCache::usage().decodedPreviews,0);
        QCOMPARE(ix::ImageLoadCache::read(registry,nearPath).document.pixels,image);
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(16,0,true);
        QVERIFY(!ix::loadPreview(nearPath).isNull()); QCOMPARE(ix::ImageLoadCache::retainedBytes(),0);
        ix::ImageLoadCache::configure(16,1,true);
        std::atomic_bool cancel=true; QVERIFY(ix::loadPreview(nearPath,{68,50},&cancel).isNull());
        QCOMPARE(ix::ImageLoadCache::retainedBytes(),0);
        QVERIFY(image.save(temporary_.filePath(QStringLiteral("reduced.jpg"))));
        QVERIFY(!ix::loadPreview(temporary_.filePath(QStringLiteral("reduced.jpg"))).isNull());
        QCOMPARE(ix::ImageLoadCache::usage().decodedPreviews,0);
        const auto animated=temporary_.filePath(QStringLiteral("handoff-animated.png"));
        QFile animation(animated); QVERIFY(animation.open(QIODevice::WriteOnly));
        const auto bytes=ix::test::apngFixture(2,{1024,768}); QCOMPARE(animation.write(bytes),bytes.size()); animation.close();
        QVERIFY(!ix::loadPreview(animated).isNull()); QCOMPARE(ix::ImageLoadCache::usage().decodedPreviews,0);
        QVERIFY(!ix::loadPreview(nearPath,{70,52}).isNull());
        QVERIFY(!ix::ImageLoadCache::takePreviewPixels(nearPath,{10,10},image.format(),"png"));
        QCOMPARE(ix::ImageLoadCache::usage().decodedPreviews,0);
        QVERIFY(!ix::loadPreview(nearPath,{71,53}).isNull());
        QVERIFY(!ix::ImageLoadCache::takePreviewPixels(nearPath,image.size(),image.format(),"bmp"));
        QCOMPARE(ix::ImageLoadCache::usage().decodedPreviews,0);
        ix::ImageLoadCache::configure(0,1,true);
        QVERIFY(!ix::loadPreview(nearPath,{72,54}).isNull());
        QCOMPARE(ix::ImageLoadCache::usage().decodedPreviews,0);
    }
    void presentationsReuseExactPixelsAndRespectPolicy() {
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(16,1,true);
        const auto restore=qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192,16,true); });
        const auto path=temporary_.filePath(QStringLiteral("presentation.png"));
        QImage source(128,96,QImage::Format_RGBA64); source.fill(QColor(25,110,210,157));
        for (int y=0;y<source.height();++y) {
            auto *row=reinterpret_cast<QRgba64 *>(source.scanLine(y));
            for (int x=0;x<source.width();++x)
                row[x]=QRgba64::fromRgba64(quint16(x*1193+y*7),quint16(y*1709+x*13),quint16(x*419+y*811),quint16(30000+(x+y)*71));
        }
        source.setColorSpace(QColorSpace::DisplayP3); source.setDotsPerMeterX(4300);
        QVERIFY(source.save(path)); const auto hash=ix::sha256File(path);
        ix::CodecRegistry registry;
        const auto first=ix::loadImage(registry,{path,0,true,false,false}); QVERIFY(first.error.isEmpty());
        const auto expected=ix::displayImage(first.document.pixels,true,false);
        QCOMPARE(first.display,expected);
        const auto second=ix::loadImage(registry,{path,0,true,false,false});
        QCOMPARE(second.document.pixels.cacheKey(),first.document.pixels.cacheKey());
        QCOMPARE(second.display.cacheKey(),first.display.cacheKey());
        QCOMPARE(second.document.pixels.colorSpace(),QColorSpace(QColorSpace::DisplayP3));
        QVERIFY(ix::ImageLoadCache::usage().presentations>0);
        const auto plain=ix::loadImage(registry,{path,0,false,false,false});
        QCOMPARE(plain.display.cacheKey(),plain.document.pixels.cacheKey());
        const auto sharpened=ix::loadImage(registry,{path,0,true,true,false});
        QCOMPARE(sharpened.display,ix::displayImage(first.document.pixels,true,true));
        QVERIFY(sharpened.display!=first.display);
        const auto again=ix::loadImage(registry,{path,0,true,false,false});
        QCOMPARE(again.display,expected); QCOMPARE(again.display.dotsPerMeterX(),4300);
        ix::ImageLoadCache::configure(16,1,true);
        QCOMPARE(ix::loadImage(registry,{path,0,true,false,false}).display.cacheKey(),again.display.cacheKey());
        QCOMPARE(ix::sha256File(path),hash);
        const auto usage=ix::ImageLoadCache::usage();
        QVERIFY(usage.originals+usage.presentations<=16*ix::MiB);
        QCOMPARE(usage.total(),ix::ImageLoadCache::retainedBytes());
    }
    void originalCacheTakesPriorityOverPreparedDisplays() {
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(4,1,true);
        const auto restore=qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192,16,true); });
        QImage source(512,384,QImage::Format_RGBA64); source.fill(Qt::red); source.setColorSpace(QColorSpace::DisplayP3);
        const auto first=temporary_.filePath(QStringLiteral("priority-1.png")), second=temporary_.filePath(QStringLiteral("priority-2.png"));
        QVERIFY(source.save(first)); QVERIFY(source.save(second));
        ix::CodecRegistry registry;
        QVERIFY(ix::loadImage(registry,{first,0,true,false,false}).error.isEmpty());
        QVERIFY(ix::ImageLoadCache::usage().presentations>0);
        QVERIFY(ix::loadImage(registry,{second,0,true,false,false}).error.isEmpty());
        int decodes=0;
        QVERIFY(ix::ImageLoadCache::read(registry,first,0,[&] { ++decodes; }).ok());
        QVERIFY(ix::ImageLoadCache::read(registry,second,0,[&] { ++decodes; }).ok());
        QCOMPARE(decodes,0);
        const auto usage=ix::ImageLoadCache::usage();
        QVERIFY(usage.originals+usage.presentations<=4*ix::MiB); QVERIFY(usage.total()<=5*ix::MiB);
        QCOMPARE(usage.presentations,0);
    }
    void clearDuringPresentationCannotRepopulateCache() {
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(16,1,true);
        const auto restore=qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192,16,true); });
        const auto path=temporary_.filePath(QStringLiteral("inflight-display.png"));
        QImage image(64,48,QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(path));
        ix::CodecRegistry registry; const auto read=ix::ImageLoadCache::read(registry,path); QVERIFY(read.ok());
        QSemaphore started,resume;
        auto work=std::async(std::launch::async,[&] {
            return ix::ImageLoadCache::prepareDisplay(path,0,read.document,true,false,[&] {
                started.release(); resume.tryAcquire(1,3000); return read.document.pixels.copy();
            });
        });
        const auto join=qScopeGuard([&] { resume.release(); if (work.valid()) work.wait(); });
        QVERIFY(started.tryAcquire(1,2000)); ix::ImageLoadCache::clear(); resume.release();
        QVERIFY(!work.get().isNull()); QCOMPARE(ix::ImageLoadCache::usage().total(),0);
        int decodes=0; QVERIFY(ix::ImageLoadCache::read(registry,path,0,[&] { ++decodes; }).ok()); QCOMPARE(decodes,1);
    }
    void preparedDisplayInvalidationAndRewriteKeepNeighbors() {
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(16,1,true);
        const auto restore=qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192,16,true); });
        QImage image(64,48,QImage::Format_RGBA64); image.fill(Qt::red); image.setColorSpace(QColorSpace::DisplayP3);
        const auto path=temporary_.filePath(QStringLiteral("changed-display.png")), neighbor=temporary_.filePath(QStringLiteral("retained-display.png"));
        QVERIFY(image.save(path)); QVERIFY(image.save(neighbor)); ix::CodecRegistry registry;
        auto before=ix::loadImage(registry,{path,0,true,false,false});
        const auto nearby=ix::loadImage(registry,{neighbor,0,true,false,false});
        image.fill(Qt::blue); QVERIFY(image.save(path));
        const auto after=ix::loadImage(registry,{path,0,true,false,false});
        QVERIFY(after.document.sourceRevision!=before.document.sourceRevision);
        QVERIFY(after.display!=before.display);
        ix::ImageLoadCache::invalidate({path});
        QCOMPARE(ix::loadImage(registry,{neighbor,0,true,false,false}).display.cacheKey(),nearby.display.cacheKey());
        ix::ImageLoadCache::clear(); QCOMPARE(ix::ImageLoadCache::retainedBytes(),0);
    }
    void smallMemoryBudgetCoordinatesAllImageCaches() {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),{QStringLiteral("--small-cache-budget"),QStringLiteral("-platform"),QStringLiteral("offscreen")});
        QVERIFY(child.waitForFinished(20000));
        QCOMPARE(child.exitStatus(),QProcess::NormalExit);
        const auto output=child.readAllStandardOutput(), errors=child.readAllStandardError();
        QVERIFY2(child.exitCode()==0,(errors+output).constData());
        const auto report=QJsonDocument::fromJson(output).object();
        QVERIFY(!report.isEmpty()); QCOMPARE(report.value(QStringLiteral("budget")).toInteger(),8*ix::MiB);
    }
    void batchAdmissionObservesRetainedCacheChanges() {
        std::atomic<qint64> retained=50;
        ix::BatchMemoryGate gate(100,[&] { return retained.load(); });
        std::atomic_bool cancel=false;
        auto first=gate.acquire(40,cancel); QVERIFY(first);
        QSemaphore entered,acquired,resume;
        std::thread worker([&] { entered.release(); auto second=gate.acquire(40,cancel); if(second) acquired.release(); resume.tryAcquire(1,3000); });
        const auto join=qScopeGuard([&] { cancel=true; resume.release(); worker.join(); });
        QVERIFY(entered.tryAcquire(1,1000)); QVERIFY(!acquired.tryAcquire(1,50));
        retained=0;
        QVERIFY(acquired.tryAcquire(1,2000));
    }
    void batchMemoryEstimatesCoverWidePixelsAndUnknownHeaders() {
        ix::PipelineProfile profile;
        profile.steps = {{QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        const auto limit = 512 * ix::MiB;
        QVERIFY(ix::estimatedPipelineMemory({64,48}, QImage::Format_ARGB32, profile, limit) < limit / 4);
        const auto narrow = ix::estimatedPipelineMemory({3000,2000}, QImage::Format_ARGB32, profile, limit);
        const auto wide = ix::estimatedPipelineMemory({3000,2000}, QImage::Format_RGBA64, profile, limit);
        QVERIFY(wide > narrow);
        QCOMPARE(ix::estimatedPipelineMemory({}, QImage::Format_ARGB32, profile, limit), limit);
        QCOMPARE(ix::estimatedPipelineMemory({1,1}, QImage::Format_Invalid, profile, limit), limit);
        QCOMPARE(ix::estimatedPipelineMemory({ix::ImageAxisLimit,ix::ImageAxisLimit}, QImage::Format_RGBA32FPx4, profile, limit), limit);
        QCOMPARE(ix::estimatedPipelineMemory({ix::ImageAxisLimit+1,1}, QImage::Format_RGB32, profile, limit), limit);
        profile.steps.prepend({QStringLiteral("resize"), {{QStringLiteral("width"),100000},{QStringLiteral("height"),100000}}});
        QCOMPARE(ix::estimatedPipelineMemory({64,48}, QImage::Format_RGBA64, profile, limit), limit);
        profile.steps[0].parameters = {{QStringLiteral("width"),4096},{QStringLiteral("height"),3072},
            {QStringLiteral("proportional"),false},{QStringLiteral("filter"),5}};
        const auto roomy = 2048 * ix::MiB;
        const auto normalizedWide = ix::estimatedPipelineMemory({64,48}, QImage::Format_RGBA64, profile, roomy);
        // 16-bit gray and RGB30 use 64-bit normalized resize output too.
        QCOMPARE(ix::estimatedPipelineMemory({64,48}, QImage::Format_Grayscale16, profile, roomy), normalizedWide);
        QCOMPARE(ix::estimatedPipelineMemory({64,48}, QImage::Format_RGB30, profile, roomy), normalizedWide);
        QVERIFY(ix::estimatedPipelineMemory({64,48}, QImage::Format_ARGB32, profile, roomy) < normalizedWide);
        profile.steps[0].parameters.insert(QStringLiteral("filter"),6);
        const auto floating = ix::estimatedPipelineMemory({64,48}, QImage::Format_RGBA32FPx4, profile, roomy);
        profile.steps.prepend({QStringLiteral("watermark"), {{QStringLiteral("text"),QStringLiteral("mark")}}});
        QCOMPARE(ix::estimatedPipelineMemory({64,48}, QImage::Format_RGBA32FPx4, profile, roomy), floating);
    }
    void batchMemoryAdmissionCancelsAndReleasesMovedLeases() {
        ix::BatchMemoryGate budget(100);
        std::atomic_bool cancel = false, waiterCancel = false, finished = false, admitted = false;
        QSemaphore entered;
        {
            auto original = budget.acquire(60, cancel);
            auto moved = std::move(original);
            QVERIFY(!original); QVERIFY(moved);
            auto remainder = budget.acquire(40, cancel);
            QVERIFY(remainder);
            std::thread waiter([&] {
                entered.release();
                auto lease = budget.acquire(1, waiterCancel);
                admitted = bool(lease);
                finished = true;
            });
            const auto join = qScopeGuard([&] { waiterCancel = true; waiter.join(); });
            QVERIFY(entered.tryAcquire(1, 1000));
            QVERIFY(!finished.load());
            waiterCancel = true;
            QTRY_VERIFY_WITH_TIMEOUT(finished.load(), 1000);
            QVERIFY(!admitted.load());
        }
        // The canceled queue entry and both moved/remaining leases must be gone.
        try {
            auto oversized = budget.acquire(1000, cancel);
            QVERIFY(oversized);
            throw std::runtime_error("simulated decoder failure");
        } catch (const std::runtime_error &) {}
        auto all = budget.acquire(100, cancel);
        QVERIFY(all);
        QVERIFY(!budget.acquire(1, waiterCancel));
    }
    void batchMemoryAdmissionBoundsConcurrentEstimatedBytes() {
        ix::BatchMemoryGate budget(96);
        std::atomic_bool cancel = false;
        std::atomic_int used = 0, peak = 0, completed = 0;
        std::vector<std::thread> threads;
        const auto join = qScopeGuard([&] { for (auto &thread : threads) if (thread.joinable()) thread.join(); });
        for (int worker = 0; worker < 8; ++worker) threads.emplace_back([&, worker] {
            for (int i = 0; i < 50; ++i) {
                const int cost = 1 + (worker * 13 + i * 7) % 70;
                auto lease = budget.acquire(cost, cancel);
                const int active = used.fetch_add(cost) + cost;
                auto maximum = peak.load();
                while (active > maximum && !peak.compare_exchange_weak(maximum, active)) {}
                std::this_thread::yield();
                used -= cost;
                ++completed;
            }
        });
        for (auto &thread : threads) thread.join();
        QCOMPARE(completed.load(), 400); QCOMPARE(used.load(), 0);
        QVERIFY(peak.load() <= 96); QVERIFY(peak.load() > 0);
    }
    void waitingBatchDoesNotDecodeCanceledOrChangedSources_data() {
        QTest::addColumn<bool>("cancelWaiting");
        QTest::newRow("cancel") << true;
        QTest::newRow("source-rewritten") << false;
    }
    void waitingBatchDoesNotDecodeCanceledOrChangedSources() {
        QFETCH(bool, cancelWaiting);
        class HoldingCodec final : public ix::ImageCodec {
          public:
            mutable QSemaphore entered, resume;
            mutable std::atomic_int reads = 0;
            bool accepts(const QString &) const override { return true; }
            QList<ix::FormatCapability> capabilities() const override { return {}; }
            ix::ImageResult read(const QString &, int) const override {
                ++reads; entered.release();
                if (!resume.tryAcquire(1, 5000)) return {{}, QStringLiteral("test decoder deadline")};
                ix::ImageDocument document;
                document.pixels = QImage(8, 6, QImage::Format_RGBA64); document.pixels.fill(Qt::green);
                return {document, {}};
            }
        };
        auto codec = std::make_shared<HoldingCodec>();
        ix::CodecRegistry registry; registry.add(codec);
        ix::PipelineJob job;
        job.profile.steps = {{QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        QList<ix::PipelineFile> plan;
        for (int i = 0; i < 2; ++i) {
            ix::PipelineFile file;
            file.input = temporary_.filePath(QStringLiteral("queued-%1-%2.owned").arg(cancelWaiting).arg(i));
            file.output = temporary_.filePath(QStringLiteral("queued-%1-%2.png").arg(cancelWaiting).arg(i));
            QFile input(file.input); QVERIFY(input.open(QIODevice::WriteOnly)); input.write("owned"); input.close();
            file.inputStamp = ix::imageFileKey(file.input); plan.append(file);
        }
        std::atomic_bool firstCancel = false, secondCancel = false;
        auto first = std::async(std::launch::async, [&] { return ix::BatchPipeline::run(job, {plan[0]}, firstCancel, {}, &registry); });
        const auto release = qScopeGuard([&] { codec->resume.release(4); if (first.valid()) first.wait(); });
        QVERIFY(codec->entered.tryAcquire(1, 2000));
        auto second = std::async(std::launch::async, [&] { return ix::BatchPipeline::run(job, {plan[1]}, secondCancel, {}, &registry); });
        const auto cancelSecond = qScopeGuard([&] { secondCancel = true; if (second.valid()) second.wait(); });
        QVERIFY(!codec->entered.tryAcquire(1, 100));
        if (cancelWaiting) secondCancel = true;
        else {
            QFile changed(plan[1].input); QVERIFY(changed.open(QIODevice::WriteOnly));
            changed.write("externally changed while queued"); changed.close();
            codec->resume.release();
        }
        QTRY_VERIFY_WITH_TIMEOUT(second.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 2000);
        QVERIFY(!second.get()[0].error.isEmpty());
        QCOMPARE(codec->reads.load(), 1);
        QVERIFY(!QFileInfo::exists(plan[1].output));
        codec->resume.release();
        const auto result = first.get();
        QVERIFY2(result[0].error.isEmpty(), qPrintable(result[0].error));
        const QImage output(plan[0].output);
        QCOMPARE(output.size(), QSize(8,6)); QCOMPARE(output.pixelColor(0,0), QColor(Qt::green));
    }
    void init() {
        ix::configureImageStorage({false});
        QSettings().setValue(QStringLiteral("performance/swapEnabled"), false);
    }
    void initTestCase() {
        ix::initializeImageMemoryLimit(512);
        ix::configureImageStorage({false});
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewMaintenanceTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
        QSettings().setValue(QStringLiteral("performance/swapEnabled"), false);
        QSettings().setValue(QStringLiteral("performance/imageCacheMiB"), 0);
        QSettings().setValue(QStringLiteral("performance/previewCacheMiB"), 0);
    }
    void batchReleasesResizedSourceBeforePublishingOutput() {
        const auto input=temporary_.filePath(QStringLiteral("memory.owned"));
        const auto output=temporary_.filePath(QStringLiteral("memory-result.png"));
        QFile file(input); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        bool released=false, releasedBeforeOutput=false;
        class OwnedCodec final : public ix::ImageCodec {
          public:
            QString output; bool *released, *before;
            bool accepts(const QString &path) const override { return path.endsWith(QStringLiteral(".owned")); }
            QList<ix::FormatCapability> capabilities() const override { return {}; }
            ix::ImageResult read(const QString &, int) const override {
                struct Owner { uchar *bytes; QString output; bool *released, *before; };
                auto *owner=new Owner{new uchar[1024*768*4],output,released,before};
                ix::ImageDocument document;
                document.pixels=QImage(owner->bytes,1024,768,1024*4,QImage::Format_RGB32,[](void *info) {
                    auto *owner=static_cast<Owner *>(info);
                    *owner->released=true; *owner->before=!QFileInfo::exists(owner->output);
                    delete[] owner->bytes; delete owner;
                },owner);
                document.pixels.fill(Qt::blue);
                return {document,{}};
            }
        };
        auto codec=std::make_shared<OwnedCodec>();
        codec->output=output; codec->released=&released; codec->before=&releasedBeforeOutput;
        ix::CodecRegistry registry; registry.add(codec);
        ix::PipelineJob job;
        job.profile.steps={{QStringLiteral("resize"),{{QStringLiteral("width"),8},{QStringLiteral("height"),6},{QStringLiteral("filter"),0}}},
                           {QStringLiteral("export"),{{QStringLiteral("format"),QStringLiteral("png")}}}};
        ix::PipelineFile entry; entry.input=input; entry.output=output; entry.inputStamp=ix::imageFileKey(input);
        std::atomic_bool cancel=false;
        const auto result=ix::BatchPipeline::run(job,{entry},cancel,{},&registry);
        QCOMPARE(result.size(),1); QVERIFY2(result[0].error.isEmpty(),qPrintable(result[0].error));
        QVERIFY(released); QVERIFY(releasedBeforeOutput);
        QImage saved(output); QCOMPARE(saved.size(),QSize(8,6)); QCOMPARE(saved.pixelColor(0,0),QColor(Qt::blue));
    }
    void completedLoadReleasesPixelsAfterDocumentReplacement() {
        const auto path = temporary_.filePath(QStringLiteral("source.png"));
        QImage image(96, 64, QImage::Format_RGB32); image.fill(Qt::green);
        QVERIFY(image.save(path)); image = {};
        ix::MainWindow window;
        window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && !window.document().empty(), 15000);
        QImage previous = window.document().pixels;
        QVERIFY(!previous.isDetached());
        QImage replacement(3, 2, QImage::Format_RGB32); replacement.fill(Qt::red);
        QApplication::clipboard()->setImage(replacement);
        window.dispatch(QStringLiteral("paste"));
        QCOMPARE(window.document().pixels.size(), replacement.size());
        ix::ImageLoadCache::clear();
        // Only this test should still own the old pixels. A finished QFuture
        // must not keep an orphan document alive until the next worker starts.
        QVERIFY(previous.isDetached());
        QApplication::clipboard()->clear();
    }
    void cacheInvalidationRetainsUnrelatedImagesAndPreviews() {
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(32, 8, true);
        auto restore = qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192, 16, true); });
        const auto source = temporary_.filePath(QStringLiteral("invalidate-source.png"));
#ifdef Q_OS_UNIX
        const auto neighbor = source + QStringLiteral("|neighbor.png");
#else
        const auto neighbor = source + QStringLiteral(".neighbor.png");
#endif
        QImage pixels(96, 64, QImage::Format_RGB32); pixels.fill(Qt::green);
        QVERIFY(pixels.save(source)); QVERIFY(pixels.save(neighbor));
        ix::CodecRegistry registry;
        QVERIFY(ix::ImageLoadCache::read(registry, source).ok());
        QVERIFY(ix::ImageLoadCache::read(registry, neighbor).ok());
        const auto originalPreview = ix::loadPreview(source, {32, 32});
        const auto neighborPreview = ix::loadPreview(neighbor, {32, 32});
        QVERIFY(!originalPreview.isNull() && !neighborPreview.isNull());
        ix::ImageLoadCache::invalidate({QString()});
        int decodes = 0;
        QVERIFY(ix::ImageLoadCache::read(registry, source, 0, [&] { ++decodes; }).ok());
        QCOMPARE(decodes, 0);
        QStringList affected;
        for (int i=0;i<10000;++i) affected.append(temporary_.filePath(QStringLiteral("absent-%1.png").arg(i)));
        affected.append(QString());
#ifdef Q_OS_WIN
        affected.append(source.toUpper());
#else
        affected.append(source);
#endif
        affected.append(source);
        ix::ImageLoadCache::invalidate(affected);
        QVERIFY(ix::ImageLoadCache::read(registry, neighbor, 0, [&] { ++decodes; }).ok());
        QCOMPARE(decodes, 0);
        QCOMPARE(ix::loadPreview(neighbor, {32, 32}).cacheKey(), neighborPreview.cacheKey());
        QVERIFY(ix::ImageLoadCache::read(registry, source, 0, [&] { ++decodes; }).ok());
        QCOMPARE(decodes, 1);
        QVERIFY(ix::loadPreview(source, {32, 32}).cacheKey() != originalPreview.cacheKey());
    }
    void invalidatedInFlightReadCannotRepopulateCache() {
        ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(32, 8, true);
        auto restore = qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192, 16, true); });
        const auto path = temporary_.filePath(QStringLiteral("invalidate-in-flight.png"));
        QImage pixels(96, 64, QImage::Format_RGB32); pixels.fill(Qt::blue); QVERIFY(pixels.save(path));
        ix::CodecRegistry registry;
        QSemaphore entered, resume;
        auto pending = QtConcurrent::run([&] { return ix::ImageLoadCache::read(registry, path, 0, [&] {
            entered.release(); resume.acquire();
        }); });
        auto finish = qScopeGuard([&] { resume.release(); pending.waitForFinished(); });
        QVERIFY(entered.tryAcquire(1, 5000));
        ix::ImageLoadCache::invalidate({path});
        resume.release(); pending.waitForFinished(); QVERIFY(pending.result().ok());
        int decodes = 0;
        QVERIFY(ix::ImageLoadCache::read(registry, path, 0, [&] { ++decodes; }).ok());
        QCOMPARE(decodes, 1);
    }
    void historyPreservesPixelsSelectionAndSourceAcrossUndoRedo() {
        using Direction = ix::DocumentHistory::Direction;
        ix::ImageDocument first;
        first.pixels = QImage(19, 13, QImage::Format_RGBA64);
        first.pixels.fill(QColor::fromRgba64(1, 37001, 65535, 23123));
        first.pixels.setColorSpace(QColorSpace::DisplayP3);
        first.pixels.setDotsPerMeterX(11811);
        first.path = QStringLiteral("source.tif"); first.sourceRevision = QStringLiteral("revision");
        first.metadata = std::make_shared<ix::Metadata>();
        first.text.insert(QStringLiteral("Comment"), QStringLiteral("precision"));
        ix::DocumentHistory history;
        const QRect selection(2, 3, 5, 7);
        history.recordEdit(first, selection);
        const auto entry = *history.target(Direction::Undo);
        auto second = first; second.pixels = first.pixels.copy(); second.modified = true;
        second.pixels.setPixelColor(0, 0, Qt::red);
        QVERIFY(history.commit(Direction::Undo, second, {}));
        QVERIFY(!history.canUndo()); QVERIFY(history.canRedo());
        QCOMPARE(entry.pixels, first.pixels);
        QCOMPARE(entry.pixels.colorSpace(), first.pixels.colorSpace());
        QCOMPARE(entry.pixels.dotsPerMeterX(), first.pixels.dotsPerMeterX());
        QCOMPARE(entry.metadata, first.metadata); QCOMPARE(entry.selection, selection);
        QCOMPARE(entry.sourceRevision, first.sourceRevision); QCOMPARE(entry.text, first.text);
        const auto redo = *history.target(Direction::Redo);
        QCOMPARE(redo.pixels, second.pixels); QVERIFY(redo.modified);
        QVERIFY(history.commit(Direction::Redo, first, selection));
        QVERIFY(history.canUndo()); QVERIFY(!history.canRedo());
        history.updateSources([](ix::ImageDocument &d) { d.path = QStringLiteral("renamed.tif"); d.modified = true; });
        QCOMPARE(history.target(Direction::Undo)->path, QStringLiteral("renamed.tif"));
        QCOMPARE(history.target(Direction::Undo)->pixels, first.pixels);
        history.clear(); QCOMPARE(history.size(), 0);
        QVERIFY(!history.commit(Direction::Undo, first, {}));
    }
    void historyBoundsRedoAndCountsSharedMetadataOnce() {
        using Direction = ix::DocumentHistory::Direction;
        ix::ImageDocument small, large, larger;
        small.pixels = QImage(8, 8, QImage::Format_RGB32); small.pixels.fill(Qt::red);
        large.pixels = QImage(128, 128, QImage::Format_RGBA64); large.pixels.fill(Qt::blue);
        larger.pixels = QImage(128, 128, QImage::Format_RGBA64); larger.pixels.fill(Qt::green);
        ix::DocumentHistory history(140000);
        history.recordEdit(small, {}); history.recordEdit(large, {});
        QCOMPARE(history.size(), 2);
        QVERIFY(history.commit(Direction::Undo, larger, {}));
        QVERIFY(history.commit(Direction::Undo, large, {}));
        QVERIFY(history.retainedBytes() <= 140000);
        QCOMPARE(history.size(), 1);
        QCOMPARE(history.target(Direction::Redo)->pixels.cacheKey(), large.pixels.cacheKey());
        auto metadata = std::make_shared<ix::Metadata>(); metadata->nativeBytes = 1000000;
        small.metadata = metadata; large.metadata = metadata;
        ix::DocumentHistory shared(1500000);
        shared.recordEdit(small, {}); shared.recordEdit(large, {});
        QCOMPARE(shared.size(), 2); QVERIFY(shared.retainedBytes() < 1500000);
        auto unique = std::make_shared<ix::Metadata>(); unique->nativeBytes = 1000000;
        larger.metadata = unique;
        shared.recordEdit(larger, {});
        QCOMPARE(shared.size(), 1); QVERIFY(shared.retainedBytes() < 1500000);
    }
    void historyBoundsPropertiesAndKeepsOneOversizedInverse() {
        ix::ImageDocument original;
        original.pixels = QImage(8, 8, QImage::Format_RGB32); original.pixels.fill(Qt::green);
        original.pixels.setText(QStringLiteral("Comment"), QString(40000, QLatin1Char('x')));
        ix::DocumentHistory history(100000, 3);
        history.recordEdit(original, {});
        auto second = original; second.pixels = original.pixels.copy();
        history.recordEdit(second, {});
        QCOMPARE(history.size(), 1);
        ix::DocumentHistory oversized(1);
        oversized.recordEdit(original, {});
        QCOMPARE(oversized.size(), 1); QVERIFY(oversized.retainedBytes() > 1);
        QVERIFY(oversized.commit(ix::DocumentHistory::Direction::Undo, second, {}));
        QCOMPARE(oversized.size(), 1); QVERIFY(oversized.canRedo());
        history.clear();
        for (int i = 0; i < 30; ++i) history.recordEdit(ix::ImageDocument{}, {});
        QCOMPARE(history.size(), 3);
        original.metadata = std::make_shared<ix::Metadata>();
        auto mutableMetadata = std::const_pointer_cast<ix::Metadata>(original.metadata);
        mutableMetadata->nativeBytes = std::numeric_limits<qint64>::max();
        QCOMPARE(ix::documentMemoryCost(original), ix::ImageCostLimit);
    }
    void losslessJpegRetainsConflictingSourceAndDestination_data() {
        QTest::addColumn<QString>("change");
        for (const auto *name : {"created-target", "rewritten-target", "rewritten-source", "success"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void losslessJpegRetainsConflictingSourceAndDestination() {
        QFETCH(QString, change);
        QTemporaryDir files; QVERIFY(files.isValid());
        const auto source = files.filePath(QStringLiteral("source.jpg"));
        const auto target = files.filePath(QStringLiteral("target.jpg"));
        QImage image(64, 48, QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(source));
        auto sourceRevision = ix::imageFileKey(source);
        QString targetRevision;
        if (change == QStringLiteral("rewritten-target")) {
            QFile file(target); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
            targetRevision = ix::imageFileKey(target);
        }
        if (change != QStringLiteral("success")) {
            QFile file(change == QStringLiteral("rewritten-source") ? source : target);
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write("intervening external content"); file.close();
        }
        QFile before(target); QByteArray expected;
        if (before.open(QIODevice::ReadOnly)) { expected = before.readAll(); before.close(); }
        const auto error = ix::transformJpeg(source, target, ix::JpegTransform::Right, {}, targetRevision, sourceRevision);
        if (change == QStringLiteral("success")) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(QImage(target).size(), QSize(48, 64));
        } else {
            QVERIFY(!error.isEmpty());
            if (expected.isEmpty()) QVERIFY(!QFileInfo::exists(target));
            else { QVERIFY(before.open(QIODevice::ReadOnly)); QCOMPARE(before.readAll(), expected); }
        }
    }
#ifdef IX_TEST_ARCHIVE
    void archiveBoundsImplicitParentDirectoriesBeforeParsing() {
        QTemporaryDir files; QVERIFY(files.isValid());
        const auto path = files.filePath(QStringLiteral("many-parents.cbz"));
        QByteArray png; QBuffer pixels(&png); pixels.open(QIODevice::WriteOnly);
        QImage image(8, 8, QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(&pixels, "PNG"));
        KZip archive(path); QVERIFY(archive.open(QIODevice::WriteOnly));
        for (int file = 0; file < 400; ++file) {
            QString name;
            for (int depth = 0; depth < 30; ++depth) name += QStringLiteral("d%1_%2/").arg(file).arg(depth);
            name += QStringLiteral("image.png");
            QVERIFY(archive.writeFile(name, png));
        }
        QVERIFY(archive.close());
        // 400 files, each below existing depth/entry/byte limits, imply 12,000
        // distinct parents. They must be rejected before constructing the tree.
        QVERIFY(!ix::CodecRegistry().read(path).ok());
        QCOMPARE(QDir(files.path()).entryList(QDir::Files), QStringList{QStringLiteral("many-parents.cbz")});
    }
#endif
    void destroyingWindowJoinsItsQueuedImageWork() {
        auto *pool = QThreadPool::globalInstance();
        pool->waitForDone();
        const int threads = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        QSemaphore entered, release, destroyed;
        auto blocker = QtConcurrent::run([&] { entered.release(); release.acquire(); });
        QVERIFY(entered.tryAcquire(1, 15000));
        std::atomic_bool freed = false;
        auto *window = new ix::MainWindow;
        // Controlled external pixels let us observe whether queued work still
        // owns an image after the owning window's destructor returns.
        auto *pixels = new uchar[8 * 8 * 4];
        QImage source(pixels, 8, 8, 8 * 4, QImage::Format_RGB32,
            [](void *state) { static_cast<std::atomic_bool *>(state)->store(true); }, &freed);
        source.fill(Qt::blue);
        const_cast<ix::ImageDocument &>(window->document()).pixels = std::move(source);
        window->dispatch(QStringLiteral("grayscale"));
        const bool queued = window->isBusy();
        // On the fixed path destruction waits for the queue. On the old path
        // it returns immediately. The timeout only releases the test barrier;
        // no product delay or minimum-duration assertion is involved.
        std::thread unblock([&] { destroyed.tryAcquire(1, 200); release.release(); });
        delete window;
        const bool releasedBeforeReturn = freed.load();
        destroyed.release(); unblock.join(); blocker.waitForFinished(); pool->waitForDone();
        pool->setMaxThreadCount(threads);
        delete[] pixels;
        QVERIFY(queued);
        QVERIFY(releasedBeforeReturn);
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("IxViewMaintenanceTests"));
    QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
    const auto catalogProbe = app.arguments().indexOf(QStringLiteral("--catalog-progress-probe"));
    if (catalogProbe >= 0) {
        // ASCII ids keep umlauts off the command line; map back to the label.
        const auto id = app.arguments().value(catalogProbe + 1);
        if (id != QStringLiteral("index") && id != QStringLiteral("exact") && id != QStringLiteral("similar"))
            return 3;
        const auto action = id == QStringLiteral("index")   ? QStringLiteral("Metadaten indexieren")
                            : id == QStringLiteral("exact") ? QStringLiteral("Identische Bilder")
                                                            : QStringLiteral("Ähnliche Bilder");
        const auto report = checkCatalogProgress(action, app.arguments().contains(QStringLiteral("--close-result-on-show")));
        const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Compact);
        QFile output;
        if (!output.open(stdout, QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) return 2;
        return report.contains(QStringLiteral("error")) ? 1 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--small-cache-budget"))) {
        const auto report=checkSmallCacheBudget();
        const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Compact);
        QFile output;
        if (!output.open(stdout,QIODevice::WriteOnly) || output.write(bytes)!=bytes.size()) return 2;
        output.close();
        return report.contains(QStringLiteral("error")) ? 1 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-memory"))) {
        const auto result = profileDocumentRelease();
        QFile output(app.arguments().value(1));
        if (!output.open(QIODevice::WriteOnly)) return 2;
        output.write(QJsonDocument(result).toJson());
        return result.contains(QStringLiteral("error")) ? 3 : 0;
    }
    MaintenanceTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "maintenance_ixview.moc"
