// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/preview_service.h"
#include "core/document_history.h"
#include "core/image_memory.h"
#include "core/jpeg_transform.h"
#include "core/batch_pipeline.h"
#include <QColorSpace>
#include "ui/main_window.h"
#include <QClipboard>
#include <QSemaphore>
#include <QSettings>
#include <QtConcurrent>
#include <QtTest>
#include <QtWidgets>
#include <thread>
#include <limits>
#ifdef IX_TEST_ARCHIVE
#include <KZip>
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif

namespace {
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
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewMaintenanceTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
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
        QCOMPARE(ix::documentMemoryCost(original), ix::ImageMemoryLimit + 1);
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
