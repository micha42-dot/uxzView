// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/codec_registry.h"
#include "core/image_loading.h"
#include "core/preview_service.h"
#include "ui/main_window.h"
#include "ui/preview_strip.h"
#include "ui/slideshow_controller.h"
#include <QColorSpace>
#include <QImageReader>
#include <QImageWriter>
#include <QScopeGuard>
#include <QtTest>
#include <QtWidgets>

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
