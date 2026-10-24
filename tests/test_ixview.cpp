// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "animation_fixture.h"
#include "core/app_storage.h"
#include "core/apng_codec.h"
#include "core/batch_pipeline.h"
#include "core/batch_processor.h"
#include "core/codec_registry.h"
#include "core/directory_index.h"
#include "core/file_journal.h"
#include "core/image_catalog.h"
#include "core/image_loading.h"
#include "core/jpeg_transform.h"
#include "core/localization.h"
#include "core/metadata.h"
#include <QPaintEvent>
#include "core/plugin_store.h"
#include "core/preview_service.h"
#include "core/processing.h"
#include "core/resize_model.h"
#include "core/rotation.h"
#include "core/selection.h"
#include "ui/action_search.h"
#include "ui/application.h"
#include "ui/crop_ratios.h"
#include "ui/dialogs.h"
#include "ui/icons.h"
#include "ui/library_dialog.h"
#include "ui/main_window.h"
#include "ui/pipeline_dialog.h"
#include "ui/platform_support.h"
#include "ui/preview_strip.h"
#include "ui/resize_dialog.h"
#include "ui/rotation_dialog.h"
#include "ui/settings_dialog.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCollator>
#include <QCryptographicHash>
#include <QFileOpenEvent>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QScopeGuard>
#include <QSysInfo>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif
#include <QtEndian>
#include <QtTest>
#include <QtWidgets>
#include <cmath>
#include <limits>
#include <numbers>
#include <tiffio.h>
#ifdef IX_TEST_EXIV2
#include <exiv2/exiv2.hpp>
#endif
namespace {
#ifdef Q_OS_WIN
HWND transitionCoverHandle(const QObject *cover) {
    return cover ? reinterpret_cast<HWND>(cover->property("nativeHandle").value<quintptr>()) : nullptr;
}
#endif
QImage pattern(int w = 64, int h = 48) {
    QImage image(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixelColor(
                x, y, QColor((x * 31 + y * 3) % 256, (y * 47 + x * 7) % 256, (x * 19 + y * 11) % 256, 255));
    image.setColorSpace(QColorSpace::SRgb);
    image.setDotsPerMeterX(3780);
    image.setDotsPerMeterY(3780);
    return image;
}
QByteArray gif() {
    return QByteArray::fromHex(
        "47494638396101000100800000000000ff000021ff0b4e45545343415045322e30030100000021f904000a0000002c000000"
        "000100010000020244010021f904000a0000002c00000000010001000002024c01003b");
}
class PaintRegionProbe : public QObject {
  public:
    QRegion painted;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Paint)
            painted |= static_cast<QPaintEvent *>(event)->region();
        return QObject::eventFilter(object, event);
    }
};
bool reviewedOverwrite(ix::MainWindow &window, const QString &input, const QString &target) {
    bool completed = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &window, [] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    watchdog.start(15000);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<ix::PipelineDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto close = qScopeGuard([&] { static_cast<QDialog *>(dialog)->reject(); });
        auto *inputs = dialog->findChild<QListWidget *>(QStringLiteral("pipelineInputs"));
        inputs->clear();
        inputs->addItem(input);
        auto *chain = dialog->findChild<QListWidget *>(QStringLiteral("pipelineChain"));
        chain->clear();
        auto *step = new QListWidgetItem(QStringLiteral("PNG"), chain);
        step->setData(Qt::UserRole, QJsonObject{{QStringLiteral("type"), QStringLiteral("export")},
            {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("format"), QStringLiteral("png")}}}});
        dialog->findChild<QLineEdit *>(QStringLiteral("pipelineOutput"))->setText(QFileInfo(target).absolutePath());
        dialog->findChild<QLineEdit *>(QStringLiteral("pipelinePattern"))->setText(QFileInfo(target).completeBaseName());
        dialog->findChild<QCheckBox *>(QStringLiteral("pipelineOverwrite"))->setChecked(true);
        auto *start = dialog->findChild<QPushButton *>(QStringLiteral("pipelineStart"));
        auto *preview = dialog->findChild<QPushButton *>(QStringLiteral("pipelinePreview"));
        preview->click();
        QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled() && start->isEnabled(), 10000);
        start->click();
        QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(), 10000);
        completed = QImage(target) == QImage(input);
    });
    window.dispatch(QStringLiteral("batch"));
    return completed;
}
} // namespace
class IxTests : public QObject {
    Q_OBJECT
  private:
    QTemporaryDir temporary_{QStringLiteral(IX_TEST_OUTPUT "/run-XXXXXX")};
    QString dir(QString name) const {
        return temporary_.filePath(name);
    }
  private slots:
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
        QApplication::clipboard()->clear();
    }
    void fileOpenEventRouting() {
        auto *app = dynamic_cast<ix::Application *>(qApp);
        QVERIFY(app);
        QString received;
        auto previous = app->fileOpened;
        app->fileOpened = [&received](const QString &path) { received = path; };
        QString path = dir(QString::fromUtf8("Finder-\xc3\xa4\xe6\x97\xa5.png"));
        QFileOpenEvent event(path);
        QVERIFY(QCoreApplication::sendEvent(app, &event));
        QCOMPARE(received, path);
        app->fileOpened = std::move(previous);
        QVERIFY(ix::referenceFont(8).pointSize() == 8);
    }

    void directoryIndexNaturalAndReverse() {
        auto folder = dir(QStringLiteral("indexed"));
        QDir().mkpath(folder);
        for (auto name : {"image10.png", "image2.png", "image1.png"}) {
            QFile file(folder + QLatin1Char('/') + QString::fromLatin1(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("data");
        }
        auto result = ix::scanDirectory(folder, {QStringLiteral("png")}, 0, false);
        QCOMPARE(result.files.size(), 3);
        QVERIFY(result.files[0].endsWith(QStringLiteral("image1.png")));
        QVERIFY(result.files[1].endsWith(QStringLiteral("image2.png")));
        for (int i = 0; i < 3; ++i)
            QCOMPARE(result.positions.value(result.files[i]), i);
        auto reverse = ix::scanDirectory(folder, {QStringLiteral("png")}, 0, true);
        QCOMPARE(reverse.files.first(), result.files.last());
        std::atomic_bool cancel = true;
        QVERIFY(ix::scanDirectory(folder, {QStringLiteral("png")}, 0, false, &cancel).files.isEmpty());
    }
    void directoryStreamingMatchesSortingFiltersAndLatest() {
        const auto folder = dir(QStringLiteral("stream-index"));
        QVERIFY(QDir().mkpath(folder + QStringLiteral("/subdirectory.png")));
        const QStringList names{QStringLiteral("image10.PNG"), QStringLiteral("image2.png"),
                                QStringLiteral("Image2.png"), QStringLiteral("ä 日本 3.png"),
                                QStringLiteral("not-an-image.txt"), QStringLiteral(".hidden.png")};
        const auto epoch = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC);
        for (int i = 0; i < names.size(); ++i) {
            QFile file(folder + QLatin1Char('/') + names[i]);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray(i + 1, 'x')), qint64(i + 1));
            QVERIFY(file.setFileTime(epoch.addSecs(i * 10), QFileDevice::FileModificationTime));
        }
        QLocale locale;
        if (locale.language() == QLocale::C)
            locale = QLocale(QLocale::German, QLocale::Germany);
        for (int sorting = 0; sorting < 4; ++sorting)
            for (bool reverse : {false, true}) {
                auto entries = QDir(folder).entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::NoSort);
                QCollator collator(locale);
                collator.setNumericMode(sorting == 0);
                collator.setCaseSensitivity(Qt::CaseInsensitive);
                std::sort(entries.begin(), entries.end(), [&](const auto &a, const auto &b) {
                    int order = 0;
                    if (sorting == 2)
                        order = a.lastModified() < b.lastModified() ? -1 : a.lastModified() > b.lastModified() ? 1 : 0;
                    else if (sorting == 3)
                        order = a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
                    if (!order)
                        order = collator.compare(a.fileName(), b.fileName());
                    if (!order)
                        order = QString::compare(a.fileName(), b.fileName(), Qt::CaseSensitive);
                    return reverse ? order > 0 : order < 0;
                });
                QStringList expected;
                for (const auto &entry : entries)
                    expected.append(entry.absoluteFilePath());
                const auto snapshot = ix::scanDirectory(folder, {QStringLiteral("png")}, sorting, reverse);
                QCOMPARE(snapshot.files, expected);
                for (int i = 0; i < expected.size(); ++i)
                    QCOMPARE(snapshot.positions.value(expected[i], -1), i);
                const auto newest = *std::max_element(entries.cbegin(), entries.cend(), [](const auto &a, const auto &b) {
                    return a.lastModified() < b.lastModified();
                });
                QCOMPARE(snapshot.latest, newest.absoluteFilePath());
            }
        const auto noLatest = ix::scanDirectory(folder, {QStringLiteral("png")}, 0, false, nullptr, false);
        QVERIFY(noLatest.latest.isEmpty());
        std::atomic_bool cancelled = true;
        QVERIFY(ix::scanDirectory(folder, {QStringLiteral("png")}, 0, false, &cancelled).files.isEmpty());
        QVERIFY(ix::scanDirectory(folder + QStringLiteral("/missing"), {QStringLiteral("png")}, 0, false).files.isEmpty());
#ifndef Q_OS_WIN
        const auto link = folder + QStringLiteral("/linked.png");
        QVERIFY(QFile::link(folder + QStringLiteral("/image2.png"), link));
        QVERIFY(ix::scanDirectory(folder, {QStringLiteral("png")}, 0, false).files.contains(link));
#endif
    }
    void neighborPreviewsReuseWidgetsAndRefreshChangedFiles() {
        QSettings settings;
        QVariantMap saved;
        for (const auto &key : settings.allKeys())
            saved.insert(key, settings.value(key));
        auto restore = qScopeGuard([saved] {
            QSettings s;
            s.clear();
            for (auto it = saved.begin(); it != saved.end(); ++it)
                s.setValue(it.key(), it.value());
        });
        settings.setValue(QStringLiteral("preview/previous"), 5);
        settings.setValue(QStringLiteral("preview/next"), 5);
        const auto folder = dir(QStringLiteral("reuse-thumbnails"));
        QVERIFY(QDir().mkpath(folder));
        QStringList files;
        for (int i = 0; i < 13; ++i) {
            files.append(folder + QStringLiteral("/reuse-thumb-%1.png").arg(i));
            QImage image(320, 240, QImage::Format_RGB32);
            image.fill(Qt::red);
            QVERIFY(image.save(files.last()));
        }
        ix::PreviewStrip strip;
        strip.resize(1200, 104);
        strip.show();
        strip.setFiles(files, 6);
        auto buttons = [&] {
            QHash<QString, QToolButton *> result;
            for (auto *button : strip.findChildren<QToolButton *>(QStringLiteral("neighborPreview")))
                result.insert(button->property("previewPath").toString(), button);
            return result;
        };
        QTRY_VERIFY(buttons().value(files.at(3))->property("previewImageKey").isValid());
        auto *retained = buttons().value(files.at(3));
        const auto iconKey = retained->icon().cacheKey();
        QPointer<QToolButton> removed = buttons().value(files.at(1));
        strip.setFiles(files, 7);
        QCOMPARE(buttons().value(files.at(3)), retained);
        QCOMPARE(retained->icon().cacheKey(), iconKey);
        QVERIFY(removed.isNull());
        QCOMPARE(buttons().size(), 10);
        QTest::qWait(100);
        QCOMPARE(retained->icon().cacheKey(), iconKey);
        QSignalSpy open(&strip, &ix::PreviewStrip::openRequested);
        retained->click();
        QCOMPARE(open.count(), 1);
        QCOMPARE(open[0][0].toString(), files.at(3));
        QImage replacement(320, 240, QImage::Format_RGB32);
        replacement.fill(Qt::green);
        QVERIFY(replacement.save(files.at(3)));
        strip.setFiles(files, 6);
        QCOMPARE(buttons().value(files.at(3)), retained);
        QTRY_COMPARE(retained->icon().pixmap(68, 50).toImage().pixelColor(34, 25), QColor(Qt::green));
        QVERIFY(QFile::remove(files.at(3)));
        strip.setFiles(files, 7);
        QTRY_VERIFY(!retained->property("previewImageKey").isValid());
        QVERIFY(retained->toolTip().contains(QStringLiteral("Keine schnelle Vorschau")));
        auto newFiles = files;
        newFiles.detach();
        QPointer<QToolButton> beforeReset = retained;
        strip.setFiles(newFiles, 7);
        QVERIFY(beforeReset.isNull());
        strip.setFiles({}, -1);
        QVERIFY(buttons().isEmpty());
        // The main window must keep the shared list during navigation as well.
        settings.setValue(QStringLiteral("view/previewStrip"), true);
        settings.setValue(QStringLiteral("navigation/sort"), 0);
        settings.setValue(QStringLiteral("navigation/reverse"), false);
        QStringList unexpectedDialogs;
        QTimer modalGuard;
        connect(&modalGuard, &QTimer::timeout, qApp, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                unexpectedDialogs.append(box->text());
                qWarning().noquote() << box->text();
                box->done(QMessageBox::Cancel);
            }
        });
        modalGuard.start(20);
        ix::MainWindow window;
        window.show();
        window.openPath(files.at(6));
        QTRY_COMPARE(window.document().path, files.at(6));
        auto windowButton = [&]() -> QToolButton * {
            for (auto *button : window.findChildren<QToolButton *>(QStringLiteral("neighborPreview")))
                if (button->property("previewPath").toString() == files.at(4))
                    return button;
            return nullptr;
        };
        QTRY_VERIFY(windowButton());
        QPointer<QToolButton> sharedButton = windowButton();
        window.dispatch(QStringLiteral("next"));
        QTRY_COMPARE(window.document().path, files.at(7));
        QVERIFY(sharedButton);
        QCOMPARE(windowButton(), sharedButton.data());
        QVERIFY2(unexpectedDialogs.isEmpty(), qPrintable(unexpectedDialogs.join(QLatin1Char('\n'))));
        window.close();
    }
    void displayCachePreservesPixelsAndInvalidates() {
        ix::ImageView view;
        view.resize(172, 128);
        view.show();
        QCoreApplication::processEvents();
        auto render = [&] {
            const auto ratio = view.viewport()->devicePixelRatioF();
            QImage result(QSize(qCeil(view.viewport()->width() * ratio), qCeil(view.viewport()->height() * ratio)), QImage::Format_RGB32);
            result.setDevicePixelRatio(ratio);
            result.fill(Qt::magenta);
            view.viewport()->render(&result);
            return result;
        };
        for (auto format : {QImage::Format_RGB32, QImage::Format_ARGB32, QImage::Format_RGBA64,
                             QImage::Format_Grayscale16, QImage::Format_RGBA16FPx4}) {
            auto original = pattern(640, 480);
            for (int y = 0; y < original.height(); ++y) {
                auto *row = reinterpret_cast<QRgb *>(original.scanLine(y));
                for (int x = 0; x < original.width(); ++x)
                    row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), (x * 13 + y * 7) % 256);
            }
            auto source = original.convertToFormat(format);
            if (format == QImage::Format_RGBA64)
                for (int y = 0; y < source.height(); ++y) {
                    auto *row = reinterpret_cast<QRgba64 *>(source.scanLine(y));
                    for (int x = 0; x < source.width(); ++x)
                        row[x] = QRgba64::fromRgba64((x * 101 + y * 79) % 65536, (x * 97 + y * 61) % 65536,
                                                    (x * 53 + y * 151) % 65536, (x * 257 + y * 137) % 65536);
                }
            const auto untouched = source.copy();
            for (bool smooth : {false, true}) {
                view.setImage(source);
                view.setSmooth(smooth);
                view.setFit(ix::ImageView::Fit::Window);
                for (const auto background : {QColor(Qt::black), QColor(71, 119, 207)}) {
                    view.background = background;
                    view.setDisplayCacheEnabled(true);
                    const auto first = render();
                    QCOMPARE(render(), first);
                    view.setSelection(QRect(21, 47, 204, 127));
                    view.grid = true;
                    view.setInformationOverlay(QStringLiteral("640 × 480"), Qt::BottomRightCorner);
                    const auto overlays = render();
                    view.setDisplayCacheEnabled(false);
                    QCOMPARE(render(), overlays);
                    view.grid = false;
                    view.setInformationOverlay({}, Qt::BottomRightCorner);
                    view.setSelection({});
                    QCOMPARE(render(), first);
                    // Source edits and a changed zoom/centering invalidate the base.
                    auto changed = source; changed.fill(QColor(219, 31, 113, 127));
                    view.setImage(changed);
                    view.setZoom(.21);
                    view.setCentered(false);
                    view.setDisplayCacheEnabled(true);
                    const auto update = render();
                    QCOMPARE(render(), update);
                    view.setDisplayCacheEnabled(false);
                    QCOMPARE(render(), update);
                    view.resize(196, 143);
                    view.setDisplayCacheEnabled(true);
                    const auto resized = render();
                    QCOMPARE(render(), resized);
                    view.setDisplayCacheEnabled(false);
                    QCOMPARE(render(), resized);
                    view.resize(172, 128);
                    view.setImage(source);
                    view.setCentered(true);
                    view.setFit(ix::ImageView::Fit::Window);
                }
                QCOMPARE(source, untouched);
            }
        }
        view.setImage(pattern(640, 480));
        view.setFit(ix::ImageView::Fit::Window);
        const auto redirected = [&](const QTransform &transform) {
            QImage output(400, 300, QImage::Format_RGB32);
            output.fill(Qt::magenta);
            QPainter painter(&output);
            painter.setTransform(transform);
            view.viewport()->render(&painter, QPoint(7, 11));
            return output;
        };
        for (const auto &transform : {QTransform::fromScale(1.5, 1.5), QTransform::fromTranslate(.25, .5),
                                      QTransform(1, 0, .0005, 0, 1, .0003, 0, 0, 1)}) {
            view.setDisplayCacheEnabled(false);
            const auto expected = redirected(transform);
            view.setDisplayCacheEnabled(true);
            render(); render();
            QCOMPARE(redirected(transform), expected);
        }
    }
    void imageLoadingSeparatesOriginalAndDisplay() {
        const auto path = dir(QStringLiteral("load-service-ä-日本.png"));
        auto original = pattern().convertToFormat(QImage::Format_RGBA64);
        original.setColorSpace(QColorSpace::DisplayP3);
        QVERIFY(original.save(path));
        ix::ImageLoadCache::clear();
        ix::CodecRegistry registry;
        int previews = 0;
        ix::ImageLoadRequest request{path};
        const auto loaded = ix::loadImage(registry, request, [&](QImage) { ++previews; });
        QVERIFY2(loaded.error.isEmpty(), qPrintable(loaded.error));
        QCOMPARE(loaded.document.pixels.convertToFormat(QImage::Format_RGBA64), original);
        QCOMPARE(loaded.document.pixels.colorSpace(), QColorSpace(QColorSpace::DisplayP3));
        QCOMPARE(loaded.display.colorSpace(), QColorSpace(QColorSpace::SRgb));
        QCOMPARE(loaded.document.sourceRevision, ix::imageFileKey(path));
        QCOMPARE(previews, 0);
        request.manageColor = false;
        auto unmanaged = ix::loadImage(registry, request);
        QCOMPARE(unmanaged.display, unmanaged.document.pixels);
        request.sharpenDisplay = true;
        const auto sharpened = ix::loadImage(registry, request);
        QVERIFY(sharpened.error.isEmpty());
        QCOMPARE(sharpened.document.pixels, unmanaged.document.pixels);
        unmanaged.document.pixels.fill(Qt::red);
        QCOMPARE(ix::loadImage(registry, request).document.pixels.convertToFormat(QImage::Format_RGBA64), original);
        request.page = 999;
        const auto missingPage = ix::loadImage(registry, request);
        QVERIFY(!missingPage.error.isEmpty());
        QVERIFY(missingPage.document.empty() && missingPage.display.isNull());
        request.path += QStringLiteral(".missing");
        const auto missingFile = ix::loadImage(registry, request);
        QVERIFY(!missingFile.error.isEmpty());
        QVERIFY(missingFile.document.empty() && missingFile.display.isNull());
    }
    void previewsAndCacheIsolation() {
        auto path = dir(QStringLiteral("preview-cache.png"));
        QVERIFY(pattern(400, 300).save(path));
        auto preview = ix::loadPreview(path, {80, 80});
        QCOMPARE(preview.size(), QSize(80, 60));
        std::atomic_bool cancel = true;
        QVERIFY(ix::loadPreview(path, {80, 80}, &cancel).isNull());
        ix::CodecRegistry registry;
        auto first = ix::ImageLoadCache::read(registry, path);
        QVERIFY(first.ok());
        auto original = first.document.pixels.pixel(0, 0);
        first.document.pixels.fill(Qt::red);
        auto second = ix::ImageLoadCache::read(registry, path);
        QCOMPARE(second.document.pixels.pixel(0, 0), original);
        ix::PreviewService service;
        QSignalSpy ready(&service, &ix::PreviewService::ready);
        service.request(path, {80, 80}, 7);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready[0][2].toInt(), 7);
        auto raw = dir(QStringLiteral("invalid.nef"));
        QFile file(raw);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("invalid");
        file.close();
        QVERIFY(ix::loadPreview(raw).isNull());
    }
    void previewCancellationDropsQueuedWork() {
        const auto path = dir(QStringLiteral("preview-cancel.png"));
        QVERIFY(pattern(400, 300).save(path));
        ix::PreviewService service;
        QSignalSpy ready(&service, &ix::PreviewService::ready);
        for (int generation = 0; generation < 200; ++generation) {
            service.cancel();
            for (int n = 0; n < 10; ++n)
                service.request(path, {80, 80}, generation);
        }
        // No event-loop yield: even completed workers must not leave thousands
        // of future watchers and completion events behind a navigation burst.
        QVERIFY(service.findChildren<QFutureWatcherBase *>().size() <= 2);
        service.cancel();
        service.request(path, {80, 80}, 999);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(2).toInt(), 999);
        QCOMPARE(qvariant_cast<QImage>(ready.first().at(1)).size(), QSize(80, 60));
        // A ready callback may cancel/requeue the next generation.
        connect(&service, &ix::PreviewService::ready, &service, [&](const QString &, const QImage &, int generation) {
            if (generation == 1000) {
                service.cancel();
                service.request(path, {40, 40}, 1001);
            }
        });
        service.request(path, {80, 80}, 1000);
        QTRY_COMPARE(ready.size(), 3);
        QCOMPARE(ready.last().at(2).toInt(), 1001);
    }
    void rawPreviewFormatsNeverFallBackToFullDecode() {
        const auto extensions = QByteArray("3fr arw bay bmq cap cine cr2 cr3 crw cs1 dc2 dcr dng drf eip erf fff ia "
            "iiq k25 kc2 kdc mdc mef mos mrw nef nrw obm orf pef ptx pxn qtk raf raw rdc rw2 rwl rwz sr2 srf srw sti x3f").split(' ');
        auto restore = qScopeGuard([] { ix::ImageLoadCache::configure(192, 16, true); });
        for (const auto &extension : extensions) {
            const auto path = dir(QStringLiteral("preview-format.") + QString::fromLatin1(extension.toUpper()));
            QVERIFY(ix::isRawImage(path));
            // A PNG disguised as RAW catches accidental QImageReader fallback.
            QVERIFY(pattern().save(path, "PNG"));
            for (bool enabled : {false, true}) {
                ix::ImageLoadCache::configure(192, 16, enabled);
                QVERIFY2(ix::loadPreview(path).isNull(), qPrintable(path));
            }
        }
        QVERIFY(!ix::isRawFormat("png"));
        QVERIFY(!ix::isRawFormat("jpeg"));
    }
    void optionalWidgetsAndNeighborNavigation() {
        QSettings().setValue(QStringLiteral("view/previewStrip"), false);
        QSettings().setValue(QStringLiteral("view/actionSearch"), false);
        ix::MainWindow window;
        window.show();
        QVERIFY(!window.findChild<ix::PreviewStrip *>());
        QVERIFY(!window.findChild<ix::ActionSearch *>());
        auto *toggle = window.findChild<QAction *>(QStringLiteral("previewStripToggle"));
        QVERIFY(toggle);
        toggle->trigger();
        auto *strip = window.findChild<ix::PreviewStrip *>();
        QVERIFY(strip);
        QVERIFY(!strip->isHidden());
        QStringList files;
        auto folder = dir(QStringLiteral("neighbors"));
        QDir().mkpath(folder);
        for (int i = 0; i < 13; ++i) {
            auto path = folder + QStringLiteral("/image%1.png").arg(i);
            QVERIFY(pattern().save(path));
            files.append(path);
        }
        QSettings().setValue(QStringLiteral("preview/previous"), 5);
        QSettings().setValue(QStringLiteral("preview/next"), 5);
        strip->applySettings();
        QVERIFY(strip->findChildren<QSpinBox *>().isEmpty());
        strip->setFiles(files, 6);
        QCOMPARE(strip->findChildren<QToolButton *>(QStringLiteral("neighborPreview")).size(), 10);
        QSignalSpy open(strip, &ix::PreviewStrip::openRequested);
        strip->findChild<QToolButton *>(QStringLiteral("neighborPreview"))->click();
        QCOMPARE(open.count(), 1);
        QCOMPARE(open[0][0].toString(), files[1]);
        QTRY_COMPARE(window.document().path, files[1]);
        toggle->trigger();
        QVERIFY(strip->isHidden());
        auto *searchToggle = window.findChild<QAction *>(QStringLiteral("actionSearchToggle"));
        searchToggle->trigger();
        auto *search = window.findChild<ix::ActionSearch *>();
        auto *toolbar = window.findChild<QToolBar *>(QStringLiteral("mainToolbar"));
        auto *spacer = window.findChild<QWidget *>(QStringLiteral("actionSearchSpacer"));
        QVERIFY(search);
        QVERIFY(toolbar);
        QVERIFY(spacer);
        for (int width : {980, 1280}) {
            window.resize(width, 662);
            QTRY_VERIFY(search->isVisible());
            QTRY_VERIFY(search->geometry().right() >= toolbar->width() - 12);
            QVERIFY(search->width() >= search->minimumWidth());
            QVERIFY(spacer->geometry().right() < search->geometry().left());
        }
        searchToggle->trigger();
        QTRY_VERIFY(search->isHidden());
        QVERIFY(spacer->isHidden());
        window.openPath(files[6]);
        QTRY_COMPARE(window.document().path, files[6]);
        QTest::qWait(120);
        toggle->trigger();
        searchToggle->trigger();
        QTest::qWait(150);
        QVERIFY(window.grab().save(QStringLiteral(IX_TEST_OUTPUT "/optional-widgets.png")));
        const auto originalPalette = window.palette();
        QPalette dark = originalPalette;
        dark.setColor(QPalette::Window, QColor(QStringLiteral("#191919")));
        dark.setColor(QPalette::WindowText, QColor(QStringLiteral("#d4d4d4")));
        dark.setColor(QPalette::Base, QColor(QStringLiteral("#222222")));
        dark.setColor(QPalette::Text, QColor(QStringLiteral("#e0e0e0")));
        dark.setColor(QPalette::Button, QColor(QStringLiteral("#191919")));
        dark.setColor(QPalette::ButtonText, QColor(QStringLiteral("#d4d4d4")));
        dark.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#737373")));
        dark.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#737373")));
        window.setPalette(dark);
        QCoreApplication::processEvents();
        const auto darkToolbar = toolbar->grab();
        QCOMPARE(darkToolbar.toImage().pixelColor(1, 1), dark.color(QPalette::Window));
        QCOMPARE(search->grab().toImage().pixelColor(search->width() - 20, search->height() - 8),
                 dark.color(QPalette::Base));
        QVERIFY(darkToolbar.save(QStringLiteral(IX_TEST_OUTPUT "/toolbar-dark.png")));
        window.setPalette(originalPalette);
        QCoreApplication::processEvents();
        QVERIFY(toolbar->grab().save(QStringLiteral(IX_TEST_OUTPUT "/toolbar-light.png")));
        toggle->trigger();
        searchToggle->trigger();
        window.close();
    }

    void centeredNeighborGroupAndSettingsCounts() {
        QSettings settings;
        auto before = settings.value(QStringLiteral("preview/previous"), 2),
             after = settings.value(QStringLiteral("preview/next"), 2);
        auto restore = qScopeGuard([before, after] {
            QSettings().setValue(QStringLiteral("preview/previous"), before);
            QSettings().setValue(QStringLiteral("preview/next"), after);
        });
        settings.setValue(QStringLiteral("preview/previous"), 2);
        settings.setValue(QStringLiteral("preview/next"), 2);
        QStringList files;
        for (int i = 0; i < 7; ++i)
            files.append(dir(QStringLiteral("center%1.png").arg(i)));
        ix::PreviewStrip strip;
        strip.resize(1000, 104);
        strip.show();
        strip.applySettings();
        strip.setFiles(files, 3);
        QCoreApplication::processEvents();
        auto bounds = [&] {
            QRect result;
            for (auto *button : strip.findChildren<QToolButton *>(QStringLiteral("neighborPreview")))
                result = result.united(QRect(button->mapTo(&strip, QPoint(0, 0)), button->size()));
            auto *marker = strip.findChild<QLabel *>(QStringLiteral("currentNeighborMarker"));
            if (marker)
                result = result.united(QRect(marker->mapTo(&strip, QPoint(0, 0)), marker->size()));
            return result;
        };
        QCOMPARE(strip.findChildren<QToolButton *>(QStringLiteral("neighborPreview")).size(), 4);
        QVERIFY(qAbs(bounds().center().x() - strip.rect().center().x()) <= 2);
        settings.setValue(QStringLiteral("preview/previous"), 1);
        settings.setValue(QStringLiteral("preview/next"), 3);
        strip.applySettings();
        QCoreApplication::processEvents();
        QCOMPARE(strip.findChildren<QToolButton *>(QStringLiteral("neighborPreview")).size(), 4);
        QVERIFY(qAbs(bounds().center().x() - strip.rect().center().x()) <= 2);
        strip.resize(240, 104);
        QCoreApplication::processEvents();
        auto *scroll = strip.findChild<QScrollArea *>(QStringLiteral("neighborScroll"));
        QVERIFY(scroll->horizontalScrollBar()->maximum() > 0);
        strip.resize(1000, 104);
        strip.setFiles(files, 0);
        QCoreApplication::processEvents();
        QVERIFY(qAbs(bounds().center().x() - strip.rect().center().x()) <= 2);
    }
    void settingsRoundTripAndCancel() {
        QSettings settings;
        QVariantMap saved;
        for (const auto &key : settings.allKeys())
            saved.insert(key, settings.value(key));
        auto restore = qScopeGuard([saved] {
            QSettings s;
            s.clear();
            for (auto it = saved.begin(); it != saved.end(); ++it)
                s.setValue(it.key(), it.value());
        });
        settings.setValue(QStringLiteral("preview/previous"), 2);
        settings.setValue(QStringLiteral("view/previewStrip"), false);
        settings.remove(QStringLiteral("navigation/askToSave"));
        settings.setValue(QStringLiteral("preview/background"), QStringLiteral("theme"));
        {
            ix::SettingsDialog dialog;
            auto *ask = dialog.findChild<QCheckBox *>(QStringLiteral("navigation/askToSave"));
            QVERIFY(ask);
            QVERIFY(!ask->isChecked());
            ask->setChecked(true);
            dialog.findChild<QComboBox *>(QStringLiteral("preview/background"))->setCurrentIndex(2);
            dialog.findChild<QSpinBox *>(QStringLiteral("preview/previous"))->setValue(4);
            dialog.findChild<QCheckBox *>(QStringLiteral("view/previewStrip"))->setChecked(true);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            QCOMPARE(settings.value(QStringLiteral("preview/previous")).toInt(), 2);
            QVERIFY(!settings.value(QStringLiteral("view/previewStrip")).toBool());
            QVERIFY(!settings.value(QStringLiteral("navigation/askToSave"), false).toBool());
            QCOMPARE(settings.value(QStringLiteral("preview/background")).toString(), QStringLiteral("theme"));
        }
        {
            ix::SettingsDialog dialog;
            dialog.show();
            auto *tabs = dialog.findChild<QStackedWidget *>(QStringLiteral("settingsPages"));
            auto *categories = dialog.findChild<QListWidget *>(QStringLiteral("settingsCategories"));
            QCOMPARE(tabs->count(), 10);
            dialog.findChild<QComboBox *>(QStringLiteral("preview/background"))->setCurrentIndex(1);
            dialog.findChild<QCheckBox *>(QStringLiteral("view/previewStrip"))->setChecked(true);
            dialog.findChild<QCheckBox *>(QStringLiteral("navigation/askToSave"))->setChecked(true);
            dialog.findChild<QSpinBox *>(QStringLiteral("preview/previous"))->setValue(4);
            dialog.findChild<QSpinBox *>(QStringLiteral("preview/next"))->setValue(1);
            dialog.findChild<QCheckBox *>(QStringLiteral("view/actionSearch"))->setChecked(true);
            dialog.findChild<QCheckBox *>(QStringLiteral("toolbar/labels"))->setChecked(true);
            dialog.findChild<QComboBox *>(QStringLiteral("toolbar/iconSize"))->setCurrentIndex(3);
            dialog.findChild<QSpinBox *>(QStringLiteral("batch/parallelism"))->setValue(3);
            dialog.findChild<QSpinBox *>(QStringLiteral("performance/imageCacheMiB"))->setValue(64);
            dialog.findChild<QCheckBox *>(QStringLiteral("performance/rawPreview"))->setChecked(false);
            for (int i = 0; i < tabs->count(); ++i) {
                categories->setCurrentRow(i);
                QCoreApplication::processEvents();
                QVERIFY(dialog.grab().save(QStringLiteral(IX_TEST_OUTPUT "/settings-%1.png").arg(i)));
            }
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        }
        QCOMPARE(settings.value(QStringLiteral("preview/previous")).toInt(), 4);
        QCOMPARE(settings.value(QStringLiteral("preview/next")).toInt(), 1);
        QVERIFY(settings.value(QStringLiteral("view/previewStrip")).toBool());
        QCOMPARE(settings.value(QStringLiteral("toolbar/iconSize")).toInt(), 32);
        QCOMPARE(settings.value(QStringLiteral("batch/parallelism")).toInt(), 3);
        QVERIFY(!settings.value(QStringLiteral("performance/rawPreview")).toBool());
        QVERIFY(settings.value(QStringLiteral("navigation/askToSave")).toBool());
        QCOMPARE(settings.value(QStringLiteral("preview/background")).toString(), QStringLiteral("image"));
        ix::SettingsDialog reopened;
        QCOMPARE(reopened.findChild<QSpinBox *>(QStringLiteral("preview/previous"))->value(), 4);
        QCOMPARE(reopened.findChild<QSpinBox *>(QStringLiteral("performance/imageCacheMiB"))->value(), 64);
        QVERIFY(reopened.findChild<QCheckBox *>(QStringLiteral("navigation/askToSave"))->isChecked());
    }
    void settingsApplyImmediatelyToMainWindow() {
        QSettings settings;
        QVariantMap saved;
        for (const auto &key : settings.allKeys())
            saved.insert(key, settings.value(key));
        auto restore = qScopeGuard([saved] {
            QSettings s;
            s.clear();
            for (auto it = saved.begin(); it != saved.end(); ++it)
                s.setValue(it.key(), it.value());
            ix::ImageLoadCache::configure(192, 16, true);
        });
        settings.setValue(QStringLiteral("view/previewStrip"), false);
        settings.setValue(QStringLiteral("view/actionSearch"), false);
        settings.remove(QStringLiteral("window/geometry"));
        ix::MainWindow window;
        window.show();
        bool edited = false;
        QTimer::singleShot(50, qApp, [&] {
            auto *dialog = qobject_cast<ix::SettingsDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            edited = true;
            dialog->findChild<QCheckBox *>(QStringLiteral("view/previewStrip"))->setChecked(true);
            dialog->findChild<QCheckBox *>(QStringLiteral("view/actionSearch"))->setChecked(true);
            dialog->findChild<QSpinBox *>(QStringLiteral("preview/previous"))->setValue(1);
            dialog->findChild<QSpinBox *>(QStringLiteral("preview/next"))->setValue(3);
            dialog->findChild<QCheckBox *>(QStringLiteral("toolbar/labels"))->setChecked(true);
            dialog->findChild<QCheckBox *>(QStringLiteral("window/rememberGeometry"))->setChecked(false);
            dialog->findChild<QComboBox *>(QStringLiteral("toolbar/iconSize"))->setCurrentIndex(2);
            dialog->findChild<QComboBox *>(QStringLiteral("view/fit"))->setCurrentIndex(3);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        window.dispatch(QStringLiteral("settings"));
        QVERIFY(edited);
        auto *strip = window.findChild<ix::PreviewStrip *>();
        QVERIFY(strip && !strip->isHidden());
        QVERIFY(window.findChild<ix::ActionSearch *>());
        QVERIFY(window.findChild<QAction *>(QStringLiteral("previewStripToggle"))->isChecked());
        auto *toolbar = window.findChild<QToolBar *>(QStringLiteral("mainToolbar"));
        QCOMPARE(toolbar->iconSize(), QSize(28, 28));
        QCOMPARE(toolbar->toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
        QCOMPARE(window.findChild<ix::ImageView *>()->fit(), ix::ImageView::Fit::Width);
        window.close();
        QVERIFY(!settings.contains(QStringLiteral("window/geometry")));
    }
    void outlineIconsAndZoomSemantics() {
        QPalette light;
        light.setColor(QPalette::WindowText, QColor(QStringLiteral("#20242b")));
        QPalette dark = light;
        dark.setColor(QPalette::WindowText, QColor(QStringLiteral("#edf2f7")));
        auto names = ix::uiIconNames();
        QVERIFY(names.size() >= 19);
        QImage sheet(760, ((names.size() + 5) / 6) * 132, QImage::Format_ARGB32_Premultiplied);
        sheet.fill(Qt::white);
        QPainter painter(&sheet);
        int i = 0;
        for (const auto &name : names) {
            auto icon = ix::uiIcon(name, light);
            QVERIFY(!icon.isNull());
            auto pix = icon.pixmap({32, 32});
            QVERIFY(!pix.isNull());
            QVERIFY(pix.toImage() != ix::uiIcon(name, dark).pixmap({32, 32}).toImage());
            auto disabled = icon.pixmap({24, 24}, QIcon::Disabled);
            QVERIFY(!disabled.isNull());
            for (qreal scale : {1.0, 1.5, 2.0}) {
                auto scaled = icon.pixmap(QSize(24, 24), scale);
                QVERIFY(!scaled.isNull());
                QCOMPARE(scaled.devicePixelRatio(), scale);
                QCOMPARE(scaled.size(), QSize(qRound(24 * scale), qRound(24 * scale)));
            }
            int x = (i % 6) * 126, y = (i / 6) * 132;
            icon.paint(&painter, {x + 20, y + 10, 32, 32});
            painter.fillRect(QRect(x + 64, y + 4, 48, 48), QColor(QStringLiteral("#1d222b")));
            ix::uiIcon(name, dark).paint(&painter, {x + 72, y + 12, 32, 32});
            painter.setPen(Qt::black);
            painter.drawText(QRect(x, y + 58, 126, 60), Qt::AlignHCenter | Qt::TextWordWrap, name);
            ++i;
        }
        painter.end();
        QVERIFY(sheet.save(QStringLiteral(IX_TEST_OUTPUT "/icon-set.png")));
        ix::MainWindow window;
        auto *zoomIn = window.findChild<QAction *>(QStringLiteral("zoomIn")),
             *zoomOut = window.findChild<QAction *>(QStringLiteral("zoomOut"));
        QCOMPARE(zoomIn->property("ixIcon").toString(), QStringLiteral("zoom-in"));
        QCOMPARE(zoomOut->property("ixIcon").toString(), QStringLiteral("zoom-out"));
        QVERIFY(zoomIn->toolTip().contains(QStringLiteral("Vergrößern")));
        QVERIFY(zoomOut->toolTip().contains(QStringLiteral("Verkleinern")));
        QVERIFY(zoomIn->icon().pixmap({24, 24}).toImage() != zoomOut->icon().pixmap({24, 24}).toImage());
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("previousPage"))->isVisible());
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("nextPage"))->isVisible());
    }
    void toolbarStatesStayStableWhileBrowsing() {
        QSettings settings;
        const auto ask = settings.value(QStringLiteral("navigation/askToSave"));
        auto restore = qScopeGuard([ask] { QSettings().setValue(QStringLiteral("navigation/askToSave"), ask); });
        settings.setValue(QStringLiteral("navigation/askToSave"), false);
        const auto first = dir(QStringLiteral("stable-toolbar-1.png"));
        const auto second = dir(QStringLiteral("stable-toolbar-2.png"));
        QVERIFY(pattern(100, 80).save(first));
        const auto next = pattern(120, 90);
        QVERIFY(next.save(second));
        ix::MainWindow window;
        window.show();
        window.openPath(first);
        QTRY_VERIFY(window.document().path == first && !window.isBusy());
        auto *toolbar = window.findChild<QToolBar *>(QStringLiteral("mainToolbar"));
        int transitions = 0;
        for (auto *action : toolbar->actions()) {
            const bool enabled = action->isEnabled();
            connect(action, &QAction::changed, &window, [&, action, enabled] {
                if (action->isEnabled() != enabled)
                    ++transitions;
            });
        }
        for (const auto &path : {second, first, second}) {
            window.openPath(path);
            QVERIFY(window.isBusy());
            // An enabled-looking command cannot mutate the outgoing document.
            window.dispatch(QStringLiteral("negativeAll"));
            auto *sharpen = window.findChild<QAction *>(QStringLiteral("sharpenDisplay"));
            const bool checked = sharpen->isChecked();
            sharpen->trigger();
            QCOMPARE(sharpen->isChecked(), checked);
            QTRY_VERIFY(window.document().path == path && !window.isBusy());
            QVERIFY(!window.document().modified);
        }
        QCOMPARE(transitions, 0);
        QCOMPARE(window.document().pixels, next);
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("previousPage"))->isVisible());
    }
    void settingsCategoriesRemainVisible() {
        ix::SettingsDialog dialog;
        dialog.resize(860, 580);
        dialog.show();
        auto *categories = dialog.findChild<QListWidget *>(QStringLiteral("settingsCategories"));
        auto *pages = dialog.findChild<QStackedWidget *>(QStringLiteral("settingsPages"));
        QVERIFY(categories && pages);
        auto *path = dialog.findChild<QLineEdit *>(QStringLiteral("storage/dataRoot"));
        QVERIFY(path && path->isReadOnly());
        QCOMPARE(QDir::fromNativeSeparators(path->text()), ix::AppStorage::dataRoot());
        QCOMPARE(categories->count(), 10);
        QVERIFY(dialog.findChildren<QTabWidget *>().isEmpty());
        QTest::qWait(30);
        QCOMPARE(categories->horizontalScrollBar()->maximum(), 0);
        QCOMPARE(categories->verticalScrollBar()->maximum(), 0);
        for (int i = 0; i < categories->count(); ++i) {
            QVERIFY(categories->viewport()->rect().contains(categories->visualItemRect(categories->item(i))));
            categories->setCurrentRow(i);
            QCOMPARE(pages->currentIndex(), i);
            QVERIFY(pages->currentWidget()->isVisible());
        }
        categories->setCurrentRow(0);
        categories->setFocus();
        QTest::keyClick(categories, Qt::Key_End);
        QCOMPARE(pages->currentIndex(), 9);
        dialog.selectPage(QStringLiteral("cropSettingsPage"));
        QVERIFY(dialog.findChild<QWidget *>(QStringLiteral("cropSettingsPage"))->isVisible());
        QVERIFY(dialog.grab().save(QStringLiteral(IX_TEST_OUTPUT "/settings-sidebar.png")));
    }
    void previewBackgroundModesPreserveThumbnails() {
        QSettings settings;
        const auto savedMode = settings.value(QStringLiteral("preview/background"));
        const auto savedColor = settings.value(QStringLiteral("view/background"));
        auto restore = qScopeGuard([savedMode, savedColor] {
            QSettings().setValue(QStringLiteral("preview/background"), savedMode);
            QSettings().setValue(QStringLiteral("view/background"), savedColor);
        });
        QWidget parent;
        auto colors = parent.palette();
        const QColor inherited(QStringLiteral("#315579"));
        colors.setColor(QPalette::Window, inherited);
        parent.setPalette(colors);
        parent.setAutoFillBackground(true);
        auto *layout = new QVBoxLayout(&parent);
        auto *strip = new ix::PreviewStrip;
        layout->addWidget(strip);
        parent.resize(650, 110);
        parent.show();
        const auto path = dir(QStringLiteral("preview-background.png"));
        QVERIFY(pattern().save(path));
        strip->setFiles({path, dir(QStringLiteral("current.png"))}, 1);
        auto *button = strip->findChild<QToolButton *>(QStringLiteral("neighborPreview"));
        QVERIFY(button);
        QTRY_VERIFY(button->property("previewImageKey").isValid());
        const auto iconKey = button->icon().cacheKey();
        auto *scroll = strip->findChild<QScrollArea *>(QStringLiteral("neighborScroll"));
        for (const auto &background : {QColor(Qt::black), QColor(QStringLiteral("#f2eddf"))}) {
            settings.setValue(QStringLiteral("view/background"), background);
            settings.setValue(QStringLiteral("preview/background"), QStringLiteral("image"));
            strip->applySettings();
            QCOMPARE(strip->grab().toImage().pixelColor(1, 1), background);
            QCOMPARE(scroll->viewport()->grab().toImage().pixelColor(1, 1), background);
            QVERIFY(button->autoRaise());
            QCOMPARE(button->icon().cacheKey(), iconKey);
            QCOMPARE(strip->palette().color(QPalette::Window), background);
        }
        settings.setValue(QStringLiteral("preview/background"), QStringLiteral("transparent"));
        strip->applySettings();
        QVERIFY(!strip->autoFillBackground());
        QVERIFY(!scroll->viewport()->autoFillBackground());
        QVERIFY(!scroll->widget()->autoFillBackground());
        QCOMPARE(strip->grab().toImage().pixelColor(1, 1), inherited);
        QCOMPARE(button->icon().cacheKey(), iconKey);
        settings.setValue(QStringLiteral("preview/background"), QStringLiteral("theme"));
        strip->applySettings();
        QVERIFY(!button->autoRaise());
        QVERIFY(scroll->viewport()->autoFillBackground());
        QCOMPARE(button->icon().cacheKey(), iconKey);
    }
    void portableModeSettingsPreserveCancelAndImport() {
        QSettings settings;
        QVariantMap saved;
        for (const auto &key : settings.allKeys())
            saved.insert(key, settings.value(key));
        const auto previousRoot = qApp->property("ixStorageData");
        auto restore = qScopeGuard([saved, previousRoot] {
            QSettings s;
            s.clear();
            for (auto it = saved.begin(); it != saved.end(); ++it)
                s.setValue(it.key(), it.value());
            qApp->setProperty("ixStorageData", previousRoot);
        });
        const auto source = dir(QStringLiteral("portable-ui-source"));
        const auto home = dir(QStringLiteral("portable-ui-app"));
        QVERIFY(QDir().mkpath(source));
        QFile catalog(source + QStringLiteral("/catalog.json"));
        QVERIFY(catalog.open(QIODevice::WriteOnly));
        catalog.write("invalid json");
        catalog.close();
        qApp->setProperty("ixStorageData", source);
        settings.setValue(QStringLiteral("preview/previous"), 2);
        const auto originalFile = QSettings().fileName();
        {
            ix::SettingsDialog dialog(nullptr, home);
            dialog.findChild<QCheckBox *>(QStringLiteral("storage/portable"))->setChecked(true);
            dialog.findChild<QSpinBox *>(QStringLiteral("preview/previous"))->setValue(4);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            QCOMPARE(settings.value(QStringLiteral("preview/previous")).toInt(), 2);
            QVERIFY(!ix::AppStorage::portableRequested(home));
        }
        int messages = 0;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, qApp, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++messages;
                box->accept();
            }
        });
        dismiss.start(20);
        {
            ix::SettingsDialog dialog(nullptr, home);
            dialog.show();
            dialog.findChild<QCheckBox *>(QStringLiteral("storage/portable"))->setChecked(true);
            dialog.findChild<QSpinBox *>(QStringLiteral("preview/previous"))->setValue(4);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(messages, 1);
            QVERIFY(dialog.isVisible());
            QCOMPARE(settings.value(QStringLiteral("preview/previous")).toInt(), 2);
            QVERIFY(!ix::AppStorage::portableRequested(home));
            dialog.reject();
        }
        QVERIFY(catalog.open(QIODevice::WriteOnly));
        catalog.write("{}");
        catalog.close();
        {
            ix::SettingsDialog dialog(nullptr, home);
            dialog.findChild<QCheckBox *>(QStringLiteral("storage/portable"))->setChecked(true);
            dialog.findChild<QSpinBox *>(QStringLiteral("preview/previous"))->setValue(4);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        QCOMPARE(messages, 2);
        QVERIFY(ix::AppStorage::portableRequested(home));
        QCOMPARE(settings.value(QStringLiteral("preview/previous")).toInt(), 4);
        QCOMPARE(QSettings().fileName(), originalFile);
        QCOMPARE(ix::AppStorage::dataRoot(), source);
        QSettings imported(home + QStringLiteral("/IxView-data/settings/IxView/IxView.ini"), QSettings::IniFormat);
        QCOMPARE(imported.value(QStringLiteral("preview/previous")).toInt(), 4);
        {
            ix::SettingsDialog dialog(nullptr, home);
            auto *portable = dialog.findChild<QCheckBox *>(QStringLiteral("storage/portable"));
            QVERIFY(portable->isChecked());
            portable->setChecked(false);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        QCOMPARE(messages, 3);
        QVERIFY(!ix::AppStorage::portableRequested(home));
        QVERIFY(QFileInfo::exists(home + QStringLiteral("/IxView-data/catalog.json")));
        QVERIFY(QFileInfo::exists(source + QStringLiteral("/catalog.json")));
    }
    void fullscreenTransitionsRetainVisibleWindow() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            // Read actual screen pixels without processing Qt events after the
            // exit. A visible Qt window / successful DwmFlush alone used to
            // pass while the cover had not received even its first paint.
            QWidget solid;
            solid.setAutoFillBackground(true);
            auto palette = solid.palette();
            const QColor green(31, 173, 79);
            palette.setColor(QPalette::Window, green);
            solid.setPalette(palette);
            solid.resize(320, 240);
            solid.showFullScreen();
            QVERIFY(QTest::qWaitForWindowExposed(&solid));
            {
                ix::WindowTransition transition(&solid);
                palette.setColor(QPalette::Window, Qt::blue);
                solid.setPalette(palette);
                solid.showNormal();
            }
            auto *cover = solid.findChild<QObject *>(QStringLiteral("fullscreenTransitionCover"));
            QVERIFY(cover);
            const auto handle = transitionCoverHandle(cover);
            RECT rect{};
            QVERIFY(GetWindowRect(handle, &rect));
            HDC screen = GetDC(nullptr);
            QVERIFY(screen);
            const auto actual = GetPixel(screen, (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
            const auto desktopPixel = GetPixel(screen, 0, 0);
            ReleaseDC(nullptr, screen);
            if (actual == CLR_INVALID && desktopPixel == CLR_INVALID)
                qWarning("Desktop pixel readback unavailable; cover colour not verified in this session.");
            else
                QCOMPARE(actual, RGB(green.red(), green.green(), green.blue()));
            QTRY_VERIFY(!solid.findChild<QObject *>(QStringLiteral("fullscreenTransitionCover")));
            QVERIFY(!IsWindow(handle));
        }
#endif
        class VisibilityProbe : public QObject {
          public:
            int hides = 0;
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::Hide)
                    ++hides;
                return false;
            }
        } probe;
        const auto folder = dir(QStringLiteral("fullscreen-transitions"));
        QVERIFY(QDir().mkpath(folder));
        const auto path = folder + QStringLiteral("/b.png");
        QVERIFY(pattern(800, 600).save(path));
        const auto neighbor = folder + QStringLiteral("/a.png");
        QVERIFY(pattern().save(neighbor));
        QVERIFY(pattern().save(folder + QStringLiteral("/c.png")));
        class TransitionWindow : public ix::MainWindow {
          public:
            bool changing = false;
            int nativeResizes = 0, unbatchedResizes = 0;
            bool leaving = false;
            int coveredResizes = 0, uncoveredResizes = 0, unpresentedResizes = 0, misplacedResizes = 0;
#ifdef Q_OS_WIN
            RECT fullscreenRect{};
          protected:
            bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override {
                const auto *event = static_cast<MSG *>(message);
                if (changing && event->message == WM_WINDOWPOSCHANGED) {
                    const auto *position = reinterpret_cast<WINDOWPOS *>(event->lParam);
                    if (!(position->flags & SWP_NOSIZE)) {
                        ++nativeResizes;
                        auto *cover = findChild<QObject *>(QStringLiteral("fullscreenTransitionCover"));
                        const bool shielded = leaving && cover && IsWindowVisible(transitionCoverHandle(cover));
                        if (!(position->flags & SWP_NOREDRAW) && !shielded)
                            ++unbatchedResizes;
                        if (leaving) {
                            if (shielded)
                                ++coveredResizes;
                            else
                                ++uncoveredResizes;
                            if (shielded && !cover->property("compositorPresented").toBool())
                                ++unpresentedResizes;
                            RECT coverRect{};
                            if (shielded && (!GetWindowRect(transitionCoverHandle(cover), &coverRect) ||
                                             !EqualRect(&coverRect, &fullscreenRect)))
                                ++misplacedResizes;
                        }
                    }
                }
                return ix::MainWindow::nativeEvent(type, message, result);
            }
#endif
        } window;
        window.resize(640, 480);
        window.show();
        window.activateWindow();
        window.openPath(path);
        QTRY_VERIFY(window.document().path == path && !window.isBusy());
        auto *view = window.findChild<ix::ImageView *>();
        auto *fullscreen = window.findChild<QAction *>(QStringLiteral("fullscreen"));
        QVERIFY(!fullscreen->autoRepeat());
        QVERIFY(view->viewport()->testAttribute(Qt::WA_OpaquePaintEvent));
        auto *previewToggle = window.findChild<QAction *>(QStringLiteral("previewStripToggle"));
        previewToggle->setChecked(true);
        auto *strip = window.findChild<ix::PreviewStrip *>();
        QVERIFY(strip);
        QTRY_COMPARE(strip->findChildren<QToolButton *>().size(), 2);
        QTRY_VERIFY(strip->findChildren<QToolButton *>().first()->property("previewImageKey").isValid());
        auto *toolbarAction = window.findChild<QToolBar *>()->actions().first();
        const auto iconKey = toolbarAction->icon().cacheKey();
        const auto id = window.winId();
        window.installEventFilter(&probe);
        for (bool maximized : {false, true}) {
            if (maximized) {
                const auto normalSize = window.size();
                window.showMaximized();
                QTRY_VERIFY(window.size() != normalSize);
            }
            QTRY_COMPARE(window.isMaximized(), maximized);
            view->setZoom(2);
            for (bool previews : {false, true}) {
                previewToggle->setChecked(previews);
                QTRY_COMPARE(strip->isVisible(), previews);
                const auto size = window.size();
                const auto previewButtons = strip->findChildren<QToolButton *>();
                const auto previewIcon = previewButtons.first()->icon().cacheKey();
                window.changing = true;
                fullscreen->trigger();
                window.changing = false;
                QTRY_VERIFY(window.isFullScreen());
                QVERIFY(strip->isHidden());
                QVERIFY(window.isVisible());
                QCOMPARE(window.winId(), id);
                window.changing = true;
                window.leaving = true;
#ifdef Q_OS_WIN
                if (QGuiApplication::platformName() == QStringLiteral("windows"))
                    QVERIFY(GetWindowRect(reinterpret_cast<HWND>(window.winId()), &window.fullscreenRect));
#endif
                fullscreen->trigger();
                window.leaving = false;
                window.changing = false;
                QTRY_VERIFY(!window.isFullScreen());
#ifdef Q_OS_WIN
                if (QGuiApplication::platformName() == QStringLiteral("windows")) {
                    auto *cover = window.findChild<QObject *>(QStringLiteral("fullscreenTransitionCover"));
                    QVERIFY(cover); // Qt's queued paints have not presented yet.
                    QVERIFY(IsWindowVisible(transitionCoverHandle(cover)));
                    QVERIFY(cover->property("compositorPresented").toBool());
                    const auto coverHandle = transitionCoverHandle(cover);
                    const auto flags = GetWindowLongPtrW(coverHandle, GWL_EXSTYLE);
                    QVERIFY(flags & WS_EX_LAYERED);
                    QVERIFY(flags & WS_EX_TRANSPARENT);
                    QVERIFY(flags & WS_EX_NOACTIVATE);
                    QVERIFY(window.findChild<QObject *>(QStringLiteral("pendingFullscreenTransition")));
                }
#endif
                QTRY_VERIFY(!window.findChild<QObject *>(QStringLiteral("fullscreenTransitionCover")));
                QTRY_COMPARE(window.isMaximized(), maximized);
                QTRY_COMPARE(window.size(), size);
                QCOMPARE(strip->isVisible(), previews);
                QCOMPARE(strip->findChildren<QToolButton *>(), previewButtons);
                QCOMPARE(previewButtons.first()->icon().cacheKey(), previewIcon);
                QCOMPARE(toolbarAction->icon().cacheKey(), iconKey);
                QVERIFY(view->viewport()->updatesEnabled());
                QVERIFY(strip->updatesEnabled());
                if (previews)
                    QVERIFY(view->geometry().bottom() < strip->geometry().top());
                QVERIFY(window.isVisible());
                QVERIFY(!window.isMinimized());
                QVERIFY(window.updatesEnabled());
                QCOMPARE(window.winId(), id);
                QCOMPARE(view->zoom(), 2.0);
            }
        }
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            QVERIFY(window.nativeResizes > 0);
            QCOMPARE(window.unbatchedResizes, 0);
            QVERIFY(window.coveredResizes > 0);
            QCOMPARE(window.uncoveredResizes, 0);
            QCOMPARE(window.unpresentedResizes, 0);
            QCOMPARE(window.misplacedResizes, 0);
        }
#endif
        fullscreen->trigger();
        window.openPath(neighbor);
        QTRY_VERIFY(window.document().path == neighbor && !window.isBusy());
        fullscreen->trigger();
        QTRY_VERIFY(!window.isFullScreen());
        QCOMPARE(strip->findChild<QLabel *>(QStringLiteral("currentNeighborMarker"))->toolTip(), neighbor);
        QCOMPARE(probe.hides, 0);
        window.removeEventFilter(&probe);
        // Successive Enter presses must replace pending covers, not stack them
        // or let an old callback reset a newer transition's DWM state.
        for (int i = 0; i < 4; ++i)
            fullscreen->trigger();
        QVERIFY(!window.isFullScreen());
        QVERIFY(window.findChildren<QObject *>(QStringLiteral("fullscreenTransitionCover")).size() <= 1);
        QTRY_VERIFY(!window.findChild<QObject *>(QStringLiteral("fullscreenTransitionCover")));
        QTRY_VERIFY(!window.findChild<QObject *>(QStringLiteral("pendingFullscreenTransition")));
        auto closing = std::make_unique<ix::MainWindow>();
        closing->show();
        closing->openPath(path);
        QTRY_VERIFY(!closing->isBusy() && closing->document().path == path);
        closing->dispatch(QStringLiteral("fullscreen"));
        closing->dispatch(QStringLiteral("fullscreen"));
        QPointer<QObject> pendingCover = closing->findChild<QObject *>(QStringLiteral("fullscreenTransitionCover"));
        closing.reset();
        QVERIFY(pendingCover.isNull());
    }
    void fullscreenInformationOptionsAndMenu() {
        ix::MainWindow window;
        QCOMPARE(window.windowTitle(), QStringLiteral("üxzView"));
        auto *viewMenu = window.menuBar()->findChild<QMenu *>(QStringLiteral("ansicht"));
        auto *display = viewMenu->findChild<QMenu *>(QStringLiteral("anzeige-optionen"));
        auto *action = viewMenu->findChild<QAction *>(QStringLiteral("fullscreenInfoOptions"));
        QVERIFY(display && action);
        QCOMPARE(viewMenu->actions().indexOf(action), viewMenu->actions().indexOf(display->menuAction()) + 1);
        QVERIFY(!ix::fullscreenInfoOptions().enabled);
        auto edit = [&](bool save) {
            QTimer::singleShot(0, [] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                QCOMPARE(dialog->objectName(), QStringLiteral("fullscreenInfoDialog"));
                dialog->findChild<QCheckBox *>(QStringLiteral("fullscreen/info/enabled"))->setChecked(true);
                auto *corner = dialog->findChild<QComboBox *>(QStringLiteral("fullscreen/info/corner"));
                corner->setCurrentIndex(corner->findData(int(Qt::BottomRightCorner)));
                dialog->findChild<QCheckBox *>(QStringLiteral("fullscreen/info/field/camera"))->setChecked(true);
                dialog->findChild<QCheckBox *>(QStringLiteral("fullscreen/info/field/dpi"))->setChecked(false);
            });
            QTimer::singleShot(20, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                dialog->findChild<QDialogButtonBox *>()->button(save ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
            });
            action->trigger();
        };
        edit(false);
        QVERIFY(!ix::fullscreenInfoOptions().enabled);
        edit(true);
        const auto options = ix::fullscreenInfoOptions();
        QVERIFY(options.enabled);
        QCOMPARE(options.corner, Qt::BottomRightCorner);
        QVERIFY(options.fields.contains(QStringLiteral("camera")));
        QVERIFY(!options.fields.contains(QStringLiteral("dpi")));
        {
            ix::SettingsDialog dialog;
            dialog.findChild<QCheckBox *>(QStringLiteral("fullscreen/info/enabled"))->setChecked(false);
            dialog.reject();
            QVERIFY(ix::fullscreenInfoOptions().enabled);
        }
        {
            ix::SettingsDialog dialog;
            dialog.findChild<QCheckBox *>(QStringLiteral("fullscreen/info/enabled"))->setChecked(false);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QVERIFY(!ix::fullscreenInfoOptions().enabled);
        }
    }
    void fullscreenInformationTracksImagesWithoutChangingPixels() {
        ix::ImageDocument document;
        document.pixels = pattern(640, 480);
        document.path = QStringLiteral("photo.png");
        document.fileBytes = 123456;
        document.format = "png";
        auto metadata = std::make_shared<ix::Metadata>();
        metadata->tags.insert(QStringLiteral("Exif.Image.Model"), QStringLiteral("Test Camera\nModel"));
        metadata->tags.insert(QStringLiteral("Exif.Photo.ISOSpeedRatings"), QStringLiteral("800"));
        document.metadata = metadata;
        ix::FullscreenInfoOptions options;
        options.enabled = true;
        options.fields = {QStringLiteral("filename"), QStringLiteral("resolution"), QStringLiteral("dpi"),
                          QStringLiteral("fileSize"), QStringLiteral("camera"), QStringLiteral("iso"),
                          QStringLiteral("lens"), QStringLiteral("unknown")};
        const auto text = ix::fullscreenInfoText(document, options, 1);
        QVERIFY(text.contains(QStringLiteral("640 × 480 px")));
        QVERIFY(text.contains(QStringLiteral("DPI: 96.0 × 96.0")));
        QVERIFY(text.contains(QStringLiteral("Kamera: Test Camera Model")));
        QVERIFY(text.contains(QStringLiteral("ISO: 800")));
        QVERIFY(!text.contains(QStringLiteral("Objektiv:")));
        QVERIFY(!text.contains(QStringLiteral("unknown")));
        QVERIFY(ix::fullscreenInfoText({}, options, 1).isEmpty());
        const auto path = dir(QStringLiteral("overlay-a.png")), next = dir(QStringLiteral("overlay-b.png"));
        QVERIFY(pattern(640, 480).save(path));
        QVERIFY(pattern(320, 240).save(next));
        QSettings().setValue(QStringLiteral("fullscreen/info/enabled"), true);
        QSettings().setValue(QStringLiteral("fullscreen/info/fields"),
                             QStringList{QStringLiteral("filename"), QStringLiteral("resolution")});
        ix::MainWindow window;
        window.show();
        window.openPath(path);
        QTRY_VERIFY(window.document().path == path && !window.isBusy());
        auto *view = window.findChild<ix::ImageView *>();
        auto *fullscreen = window.findChild<QAction *>(QStringLiteral("fullscreen"));
        QVERIFY(view->informationOverlay().isEmpty());
        const auto pixels = window.document().pixels;
        fullscreen->trigger();
        QTRY_VERIFY(window.isFullScreen());
        QVERIFY(view->informationOverlay().contains(QStringLiteral("overlay-a.png")));
        const auto fullText = view->informationOverlay();
        for (const auto corner : {Qt::TopLeftCorner, Qt::TopRightCorner, Qt::BottomLeftCorner, Qt::BottomRightCorner}) {
            view->setInformationOverlay({}, corner);
            const auto before = view->screenshot();
            view->setInformationOverlay(fullText, corner);
            const auto box = view->informationOverlayRect();
            QVERIFY(view->viewport()->rect().contains(box));
            if (corner == Qt::TopLeftCorner || corner == Qt::BottomLeftCorner)
                QCOMPARE(box.left(), 12);
            else
                QCOMPARE(view->viewport()->width() - box.right() - 1, 12);
            const auto after = view->screenshot();
            QVERIFY(before != after);
            QCOMPARE(window.document().pixels, pixels);
            QVERIFY(!window.document().modified);
        }
        QVERIFY(view->screenshot().save(QStringLiteral(IX_TEST_OUTPUT "/fullscreen-information.png")));
        window.openPath(next);
        QTRY_VERIFY(window.document().path == next && !window.isBusy());
        QVERIFY(view->informationOverlay().contains(QStringLiteral("overlay-b.png")));
        QVERIFY(view->informationOverlay().contains(QStringLiteral("320 × 240 px")));
        fullscreen->trigger();
        QTRY_VERIFY(!window.isFullScreen());
        QVERIFY(view->informationOverlay().isEmpty());
        view->resize(48, 48);
        view->setInformationOverlay(QString(5000, QLatin1Char('W')), Qt::BottomRightCorner);
        QVERIFY(view->informationOverlayRect().isEmpty() || view->viewport()->rect().contains(view->informationOverlayRect()));
    }
    void configurableImageCache() {
        struct CountingCodec : ix::ImageCodec {
            mutable int reads = 0;
            bool accepts(const QString &) const override {
                return true;
            }
            ix::ImageResult read(const QString &path, int) const override {
                ++reads;
                ix::ImageDocument document;
                document.pixels = pattern();
                document.path = path;
                document.format = "png";
                return {document, {}};
            }
            QList<ix::FormatCapability> capabilities() const override {
                return {};
            }
        };
        auto restore = qScopeGuard([] {
            ix::ImageLoadCache::configure(192, 16, true);
            ix::ImageLoadCache::clear();
        });
        auto codec = std::make_shared<CountingCodec>();
        ix::CodecRegistry registry;
        registry.add(codec);
        auto path = dir(QStringLiteral("cache-config.png"));
        QVERIFY(pattern().save(path));
        ix::ImageLoadCache::configure(0, 0);
        QVERIFY(ix::ImageLoadCache::read(registry, path).ok());
        QVERIFY(ix::ImageLoadCache::read(registry, path).ok());
        QCOMPARE(codec->reads, 2);
        ix::ImageLoadCache::configure(8, 4);
        QVERIFY(ix::ImageLoadCache::read(registry, path).ok());
        QVERIFY(ix::ImageLoadCache::read(registry, path).ok());
        QCOMPARE(codec->reads, 3);
        ix::ImageLoadCache::clear();
        QVERIFY(ix::ImageLoadCache::read(registry, path).ok());
        QCOMPARE(codec->reads, 4);
    }
    void actionSearchTriggersActualCommand() {
        QAction action(QStringLiteral("Größe ändern"), this);
        action.setData(QStringLiteral("resize"));
        ix::ActionSearch search({&action});
        QSignalSpy fired(&action, &QAction::triggered);
        search.show();
        search.setFocus();
        QTest::keyClicks(&search, QStringLiteral("dpi"));
        QTest::keyClick(&search, Qt::Key_Return);
        QTRY_COMPARE(fired.count(), 1);
        QVERIFY(search.text().isEmpty());
        action.setEnabled(false);
        QTest::keyClicks(&search, QStringLiteral("dpi"));
        QTest::keyClick(&search, Qt::Key_Return);
        QTest::qWait(10);
        QCOMPARE(fired.count(), 1);
    }
    void journalCopyMoveTrashUndo() {
        auto source = dir(QStringLiteral("journal-source.txt")),
             target = dir(QStringLiteral("journal-target.txt"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("original");
        file.close();
        ix::FileJournal journal(dir(QStringLiteral("journal-basic")));
        QVERIFY(journal.perform(QStringLiteral("copy"), source, target).isEmpty());
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(QFileInfo::exists(target));
        QVERIFY(journal.undo().isEmpty());
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(journal.perform(QStringLiteral("move"), source, target).isEmpty());
        QVERIFY(!QFileInfo::exists(source));
        QVERIFY(journal.undo().isEmpty());
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(journal.perform(QStringLiteral("delete"), source).isEmpty());
        QVERIFY(!QFileInfo::exists(source));
        auto entry = journal.entries().last().toObject();
        QVERIFY(QFileInfo::exists(entry.value(QStringLiteral("target")).toString()));
        QVERIFY(journal.undo().isEmpty());
        QCOMPARE(ix::sha256File(source), entry.value(QStringLiteral("hash")).toString());
    }
    void journalMigratesPastOldLimitAndWritesOnlyChangedRecord() {
        const auto source = dir(QStringLiteral("journal-v2-source.png"));
        QVERIFY(pattern().save(source));
        ix::FileJournal journal(dir(QStringLiteral("journal-v2")));
        QVERIFY(QDir().mkpath(journal.root()));
        const auto oldTarget = dir(QStringLiteral("journal-v2-old.png"));
        QVERIFY(QFile::copy(source, oldTarget));
        QJsonObject old{{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                        {QStringLiteral("kind"), QStringLiteral("copy")},
                        {QStringLiteral("source"), source},
                        {QStringLiteral("target"), oldTarget},
                        {QStringLiteral("hash"), ix::sha256File(source)},
                        {QStringLiteral("state"), QStringLiteral("committed")},
                        {QStringLiteral("padding"), QString(32 * 1024 * 1024, QLatin1Char('x'))}};
        QFile legacy(journal.root() + QStringLiteral("/operations.json"));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(QJsonDocument(QJsonArray{old}).toJson(QJsonDocument::Compact));
        legacy.close();
        QString issue;
        QCOMPARE(journal.entries(&issue).size(), 1);
        QVERIFY(issue.isEmpty());
        const auto target = dir(QStringLiteral("journal-v2-new.png"));
        auto error = journal.perform(QStringLiteral("copy"), source, target);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!legacy.exists());
        QCOMPARE(journal.entries(&issue).size(), 2);
        QVERIFY(issue.isEmpty());
        const QDir records(journal.root() + QStringLiteral("/entries"));
        const auto first =
            records.filePath(records.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name).first());
        const auto hash = ix::sha256File(first);
        const auto stamp = QFileInfo(first).lastModified();
        QVERIFY(journal.undo().isEmpty());
        QVERIFY(!QFileInfo::exists(target));
        QCOMPARE(ix::sha256File(first), hash);
        QCOMPARE(QFileInfo(first).lastModified(), stamp);
        QVERIFY(journal.undo().isEmpty());
        QVERIFY(!QFileInfo::exists(oldTarget));
    }
    void batchPinsWatermarkAndRejectsResourceOutputs() {
        const auto input = dir(QStringLiteral("watermark-input.png"));
        const auto mark = dir(QStringLiteral("watermark-asset.png"));
        QVERIFY(pattern().save(input));
        QImage image(12, 12, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(mark));
        ix::PipelineJob job;
        job.inputs = {input};
        job.outputDirectory = dir(QStringLiteral("watermark-results"));
        job.journalRoot = dir(QStringLiteral("watermark-journal"));
        QVERIFY(QDir().mkpath(job.outputDirectory));
        job.profile.steps = {{QStringLiteral("watermark"),
                              {{QStringLiteral("image"), mark},
                               {QStringLiteral("opacity"), 1.0},
                               {QStringLiteral("margin"), 0}}},
                             {QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        auto plan = ix::BatchPipeline::plan(job);
        std::atomic_bool cancel = false;
        auto preview = ix::BatchPipeline::preview(job, input, cancel, plan);
        QVERIFY2(preview.error.isEmpty(), qPrintable(preview.error));
        image.fill(Qt::yellow);
        QVERIFY(image.save(mark));
        auto result = ix::BatchPipeline::run(job, plan, cancel);
        QVERIFY(!result.first().error.isEmpty());
        QVERIFY(!QFileInfo::exists(plan.first().output));
        QVERIFY(!ix::BatchPipeline::preview(job, input, cancel, plan).error.isEmpty());
        plan = ix::BatchPipeline::plan(job);
        result = ix::BatchPipeline::run(job, plan, cancel);
        QVERIFY2(result.first().error.isEmpty(), qPrintable(result.first().error));
        job.outputDirectory = QFileInfo(mark).absolutePath();
        job.namePattern = QFileInfo(mark).completeBaseName();
        job.overwrite = true;
        QVERIFY(!ix::BatchPipeline::plan(job).first().error.isEmpty());
    }
    void libraryLoadsThumbnailsAfterTwoHundredItems() {
        const auto folder = dir(QStringLiteral("library-many"));
        QVERIFY(QDir().mkpath(folder));
        QStringList files;
        for (int i = 0; i < 500; ++i) {
            const auto path = folder + QStringLiteral("/%1.png").arg(i, 4, 10, QLatin1Char('0'));
            QVERIFY(pattern(8, 8).save(path));
            files.append(path);
        }
        bool loaded = false;
        QTimer::singleShot(150, qApp, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *list = dialog->findChild<QListWidget *>(QStringLiteral("catalogImages"));
            if (!list) {
                dialog->reject();
                return;
            }
            const auto before = list->item(499)->icon().cacheKey();
            list->scrollToBottom();
            auto *poll = new QTimer(dialog);
            auto elapsed = std::make_shared<QElapsedTimer>();
            elapsed->start();
            connect(poll, &QTimer::timeout, dialog, [&, dialog, list, before, elapsed, poll] {
                loaded = list->item(499)->icon().cacheKey() != before;
                if (loaded || elapsed->elapsed() >= 5000) {
                    if (!loaded)
                        qWarning() << "Last thumbnail not loaded; item rect:" << list->visualItemRect(list->item(499))
                                   << "viewport:" << list->viewport()->rect() << "scroll:"
                                   << list->verticalScrollBar()->value() << list->verticalScrollBar()->maximum();
                    poll->stop();
                    dialog->reject();
                }
            });
            poll->start(20);
        });
        ix::libraryDialog(nullptr, files, [](QString) {});
        QVERIFY(loaded);
    }
    void saveDialogPausesSlideshowAndRetainsChosenImage() {
        QSettings settings;
        QVariantMap old;
        for (const auto &key : settings.allKeys())
            old.insert(key, settings.value(key));
        auto restore = qScopeGuard([old] {
            QSettings s;
            s.clear();
            for (auto it = old.begin(); it != old.end(); ++it)
                s.setValue(it.key(), it.value());
        });
        settings.setValue(QStringLiteral("slideshow/seconds"), .1);
        settings.setValue(QStringLiteral("slideshow/random"), false);
        settings.setValue(QStringLiteral("slideshow/repeat"), true);
        settings.setValue(QStringLiteral("slideshow/fullscreen"), false);
        const auto folder = dir(QStringLiteral("save-slides"));
        QVERIFY(QDir().mkpath(folder));
        const auto first = folder + QStringLiteral("/a.png"), second = folder + QStringLiteral("/b.png");
        QImage image(20, 12, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(first));
        image.fill(Qt::blue);
        QVERIFY(image.save(second));
        ix::MainWindow window;
        window.show();
        window.openPath(first);
        QTRY_COMPARE(window.document().path, first);
        window.dispatch(QStringLiteral("slideshow"));
        const auto output = dir(QStringLiteral("save-slide-copy.png"));
        bool stayed = false, handled = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dialog || handled)
                return;
            handled = true;
            QTimer::singleShot(400, dialog, [&, dialog] {
                stayed = window.document().path == first;
                dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(output);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            });
        });
        const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        auto nativeRestore =
            qScopeGuard([native] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native); });
        timer.start(20);
        window.dispatch(QStringLiteral("saveAs"));
        timer.stop();
        window.close();
        QVERIFY(stayed);
        QCOMPARE(window.document().path, output);
        QCOMPARE(QImage(output).pixelColor(0, 0), QColor(Qt::red));
    }
    void exportedAnimationBecomesSinglePageDocument() {
        const auto input = dir(QStringLiteral("export-frames.gif")),
                   output = dir(QStringLiteral("export-frame.png"));
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(gif());
        file.close();
        const auto previousAsk = QSettings().value(QStringLiteral("navigation/askToSave"));
        auto restoreAsk = qScopeGuard([previousAsk] {
            QSettings().setValue(QStringLiteral("navigation/askToSave"), previousAsk);
        });
        QSettings().setValue(QStringLiteral("navigation/askToSave"), true);
        ix::MainWindow window;
        window.openPath(input, 1);
        QTRY_COMPARE(window.document().path, input);
        QVERIFY(window.document().animated);
        QCOMPARE(window.document().pageCount, 2);
        window.dispatch(QStringLiteral("right"));
        QTRY_VERIFY(window.document().modified);
        const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        auto restore =
            qScopeGuard([native] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native); });
        QTimer timer;
        bool handled = false;
        connect(&timer, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (dialog && !handled) {
                handled = true;
                dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(output);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            } else if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *save = message->button(QMessageBox::Save))
                    save->click();
                else if (auto *yes = message->button(QMessageBox::Yes))
                    yes->click();
            }
        });
        timer.start(20);
        window.dispatch(QStringLiteral("animation"));
        timer.stop();
        QCOMPARE(window.document().path, output);
        QCOMPARE(window.document().page, 0);
        QCOMPARE(window.document().pageCount, 1);
        QVERIFY(!window.document().animated);
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("nextPage"))->isEnabled());
        QVERIFY(ix::CodecRegistry().read(output).ok());
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().path, output);
        QCOMPARE(window.document().page, 0);
        QCOMPARE(window.document().pageCount, 1);
        QVERIFY(!window.document().animated);
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("nextPage"))->isEnabled());
    }
    void fileOpenDuringSaveIsDeferredUntilTransactionEnds() {
        const auto first = dir(QStringLiteral("deferred-first.png")),
                   second = dir(QStringLiteral("deferred-second.png")),
                   output = dir(QStringLiteral("deferred-saved.png"));
        QImage image(20, 12, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(first));
        image.fill(Qt::blue);
        QVERIFY(image.save(second));
        ix::MainWindow window;
        window.openPath(first);
        QTRY_COMPARE(window.document().path, first);
        const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        auto restore =
            qScopeGuard([native] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native); });
        QTimer timer;
        bool handled = false, stayed = false;
        connect(&timer, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dialog || handled)
                return;
            handled = true;
            window.openPath(second);
            window.dispatch(QStringLiteral("grayscale"));
            QTimer::singleShot(200, dialog, [&, dialog] {
                stayed = window.document().path == first && !window.document().modified;
                dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(output);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            });
        });
        timer.start(20);
        window.dispatch(QStringLiteral("saveAs"));
        timer.stop();
        QVERIFY(stayed);
        QCOMPARE(QImage(output).pixelColor(0, 0), QColor(Qt::red));
        QTRY_COMPARE(window.document().path, second);
        QCOMPARE(window.document().pixels.pixelColor(0, 0), QColor(Qt::blue));
    }
    void journalRefusesChangedTargetsAndCorruption() {
        auto source = dir(QStringLiteral("guard-source.txt")),
             target = dir(QStringLiteral("guard-target.txt"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("source");
        file.close();
        ix::FileJournal journal(dir(QStringLiteral("journal-guard")));
        QVERIFY(journal.perform(QStringLiteral("copy"), source, target).isEmpty());
        QFile changed(target);
        QVERIFY(changed.open(QIODevice::WriteOnly));
        changed.write("changed");
        changed.close();
        QVERIFY(!journal.undo().isEmpty());
        QVERIFY(QFileInfo::exists(target));
        QFile corrupt(journal.root() + QStringLiteral("/operations.json"));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("broken");
        corrupt.close();
        QVERIFY(!journal.perform(QStringLiteral("delete"), source).isEmpty());
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(corrupt.open(QIODevice::ReadOnly));
        QCOMPARE(corrupt.readAll(), QByteArray("broken"));
    }
    void journalReplacementAndBackupGuard() {
        auto target = dir(QStringLiteral("replace.txt")), stage = dir(QStringLiteral("stage.txt"));
        auto write = [](const QString &path, QByteArray data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(target, "old"));
        QVERIFY(write(stage, "new"));
        auto oldHash = ix::sha256File(target);
        ix::FileJournal journal(dir(QStringLiteral("journal-replace")));
        QVERIFY(journal.installOutput(stage, target, true, ix::imageFileKey(target)).isEmpty());
        QVERIFY(journal.undo().isEmpty());
        QCOMPARE(ix::sha256File(target), oldHash);
        QVERIFY(write(stage, "new2"));
        QVERIFY(journal.installOutput(stage, target, true, ix::imageFileKey(target)).isEmpty());
        auto backup = journal.entries().last().toObject().value(QStringLiteral("backup")).toString();
        QVERIFY(write(backup, "corrupted backup"));
        QVERIFY(!journal.undo().isEmpty());
        QVERIFY(!QFileInfo::exists(stage));
    }
    void pipelineOrderProfilesPreviewAndUndo() {
        ix::PipelineJob job;
        auto input = dir(QStringLiteral("pipeline.png"));
        QVERIFY(pattern(120, 80).save(input));
        job.inputs = {input};
        job.outputDirectory = dir(QStringLiteral("pipeline-output"));
        job.journalRoot = dir(QStringLiteral("pipeline-journal"));
        job.allowLoss = true;
        job.profile.steps = {
            {QStringLiteral("resize"), {{QStringLiteral("width"), 60}, {QStringLiteral("height"), 60}}},
            {QStringLiteral("sharpen"), {{QStringLiteral("amount"), 20}}},
            {QStringLiteral("watermark"),
             {{QStringLiteral("text"), QStringLiteral("IxView")},
              {QStringLiteral("fontSize"), 12},
              {QStringLiteral("margin"), 2}}},
            {QStringLiteral("export"),
             {{QStringLiteral("format"), QStringLiteral("webp")}, {QStringLiteral("quality"), 70}}}};
        QVERIFY(job.profile.validate().isEmpty());
        QString error;
        auto restored = ix::PipelineProfile::fromJson(job.profile.toJson(), error);
        QVERIFY(error.isEmpty());
        QCOMPARE(restored.toJson(), job.profile.toJson());
        std::atomic_bool cancel = false;
        auto plan = ix::BatchPipeline::plan(job);
        QCOMPARE(plan.size(), 1);
        QVERIFY(plan[0].error.isEmpty());
        auto preview = ix::BatchPipeline::preview(job, input, cancel);
        QVERIFY2(preview.error.isEmpty(), qPrintable(preview.error));
        QCOMPARE(preview.resultSize, QSize(60, 40));
        QVERIFY(!preview.after.isNull());
        QVERIFY(!QFileInfo::exists(plan[0].output));
        auto result = ix::BatchPipeline::run(job, plan, cancel);
        QVERIFY2(result[0].error.isEmpty(), qPrintable(result[0].error));
        QImage output(result[0].output);
        QCOMPARE(output.size(), QSize(60, 40));
        QCOMPARE(output.scaled(500, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation), preview.after);
        const auto undoError = ix::FileJournal(job.journalRoot).undo();
        if (!undoError.isEmpty())
            qWarning().noquote() << undoError << "target hash:" << ix::sha256File(result[0].output)
                                 << "journal:" << QJsonDocument(ix::FileJournal(job.journalRoot).entries())
                                                         .toJson(QJsonDocument::Compact);
        QVERIFY2(undoError.isEmpty(), qPrintable(undoError));
        QVERIFY(!QFileInfo::exists(result[0].output));
        std::swap(job.profile.steps[0], job.profile.steps.last());
        QVERIFY(!job.profile.validate().isEmpty());
    }
    void pipelineFreshnessCollisionAndCancellation() {
        ix::PipelineJob job;
        auto source = dir(QStringLiteral("pipeline-guard.png"));
        QVERIFY(pattern().save(source));
        job.inputs = {source};
        job.profile.steps = {{QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        job.outputDirectory = QFileInfo(source).absolutePath();
        job.namePattern = QStringLiteral("{name}");
        QVERIFY(!ix::BatchPipeline::plan(job)[0].error.isEmpty());
        job.outputDirectory = dir(QStringLiteral("pipeline-guard-output"));
        job.journalRoot = dir(QStringLiteral("pipeline-guard-journal"));
        auto plan = ix::BatchPipeline::plan(job);
        QVERIFY(plan[0].error.isEmpty());
        QVERIFY(pattern(65, 49).save(source));
        std::atomic_bool cancel = false;
        auto result = ix::BatchPipeline::run(job, plan, cancel);
        QVERIFY(!result[0].error.isEmpty());
        QVERIFY(!QFileInfo::exists(plan[0].output));
        plan = ix::BatchPipeline::plan(job);
        cancel = true;
        result = ix::BatchPipeline::run(job, plan, cancel);
        QVERIFY(!result[0].error.isEmpty());
        QVERIFY(!QFileInfo::exists(plan[0].output));
        job.inputs.append(source);
        job.namePattern = QStringLiteral("same");
        QVERIFY(!ix::BatchPipeline::plan(job)[1].error.isEmpty());
    }
    void pipelineChangedOverwritePlanAndParallelCommit() {
        ix::PipelineJob job;
        job.outputDirectory = dir(QStringLiteral("parallel-output"));
        job.journalRoot = dir(QStringLiteral("parallel-journal"));
        job.namePattern = QStringLiteral("{name}");
        job.allowLoss = true;
        job.parallelism = 4;
        job.profile.steps = {{QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        for (int i = 0; i < 4; ++i) {
            auto path = dir(QStringLiteral("parallel%1.png").arg(i));
            QVERIFY(pattern(70 + i, 50).save(path));
            job.inputs.append(path);
        }
        std::atomic_bool cancel = false;
        auto result = ix::BatchPipeline::run(job, ix::BatchPipeline::plan(job), cancel);
        for (const auto &entry : result)
            QVERIFY2(entry.error.isEmpty(), qPrintable(entry.error));
        QCOMPARE(ix::FileJournal(job.journalRoot).entries().size(), 4);
        job.overwrite = true;
        auto plan = ix::BatchPipeline::plan(job);
        QVERIFY(plan[0].replaces);
        QVERIFY(pattern(120, 60).save(plan[0].output));
        auto changedHash = ix::sha256File(plan[0].output);
        result = ix::BatchPipeline::run(job, plan, cancel);
        QVERIFY(!result[0].error.isEmpty());
        QCOMPARE(ix::sha256File(plan[0].output), changedHash);
        for (int i = 1; i < 4; ++i)
            QVERIFY(result[i].error.isEmpty());
    }
    void pipelineDialogRealPreviewAndRun() {
        auto source = dir(QStringLiteral("pipeline-ui.png"));
        QVERIFY(pattern().save(source));
        ix::PipelineDialog dialog({source}, nullptr, dir(QStringLiteral("pipeline-ui-journal")));
        dialog.show();
        dialog.findChild<QCheckBox *>(QStringLiteral("pipelineAllowLoss"))->setChecked(true);
        auto *start = dialog.findChild<QPushButton *>(QStringLiteral("pipelineStart"));
        QVERIFY(!start->isEnabled());
        auto *output = dialog.findChild<QLineEdit *>(QStringLiteral("pipelineOutput"));
        output->setText(dir(QStringLiteral("pipeline-ui-out")));
        auto *chain = dialog.findChild<QListWidget *>(QStringLiteral("pipelineChain"));
        delete chain->takeItem(0);
        dialog.findChild<QPushButton *>(QStringLiteral("pipelinePreview"))->click();
        QTRY_VERIFY_WITH_TIMEOUT(
            dialog.findChild<QPushButton *>(QStringLiteral("pipelinePreview"))->isEnabled() &&
                start->isEnabled(),
            10000);
        QVERIFY(!QFileInfo::exists(output->text() + QStringLiteral("/pipeline-ui_0001.webp")));
        dialog.grab().save(QStringLiteral(IX_TEST_OUTPUT "/pipeline.png"));
        start->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output->text() + QStringLiteral("/pipeline-ui_0001.webp")),
                                 10000);
        QTRY_VERIFY_WITH_TIMEOUT(
            dialog.findChild<QPushButton *>(QStringLiteral("pipelinePreview"))->isEnabled(), 10000);
        QCOMPARE(ix::FileJournal(dir(QStringLiteral("pipeline-ui-journal"))).entries().size(), 1);
    }
    void catalogAnnotationsIndexSearchAndCorruption() {
        auto source = dir(QStringLiteral("catalog-landscape.png"));
        QVERIFY(pattern().save(source));
        ix::ImageCatalog catalog(dir(QStringLiteral("catalog.json")));
        QVERIFY(catalog.setAnnotation(source, 4, QStringLiteral("blue")).isEmpty());
        std::atomic_bool cancel = false;
        QVERIFY(catalog.index({source}, cancel).isEmpty());
        QCOMPARE(catalog.search({source}, QStringLiteral("landscape 64x48"), 4, QStringLiteral("blue")),
                 QStringList{source});
        QVERIFY(catalog.search({source}, {}, 5).isEmpty());
        QCOMPARE(catalog.records().size(), 1);
        QCOMPARE(catalog.record(source).value(QStringLiteral("rating")).toInt(), 4);
        QFile file(catalog.path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("broken");
        file.close();
        QVERIFY(!catalog.setAnnotation(source, 1, {}).isEmpty());
        QVERIFY(!catalog.index({source}, cancel).isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("broken"));
    }
    void exactAndSimilarDuplicates() {
        auto a = dir(QStringLiteral("dupe-a.png")), b = dir(QStringLiteral("dupe-b.png")),
             c = dir(QStringLiteral("dupe-c.jpg")), red = dir(QStringLiteral("red.png")),
             white = dir(QStringLiteral("white.png"));
        QVERIFY(pattern().save(a));
        QVERIFY(QFile::copy(a, b));
        QVERIFY(pattern().save(c, "JPEG", 90));
        QImage flat(64, 48, QImage::Format_RGB32);
        flat.fill(Qt::red);
        QVERIFY(flat.save(red));
        flat.fill(Qt::white);
        QVERIFY(flat.save(white));
        std::atomic_bool cancel = false;
        auto exact = ix::ImageCatalog::duplicates({a, b, c, red, white}, false, 0, cancel);
        QCOMPARE(exact.pairs.size(), 1);
        QVERIFY(exact.pairs[0].identical);
        auto similar = ix::ImageCatalog::duplicates({a, c}, true, 16, cancel);
        QCOMPARE(similar.pairs.size(), 1);
        QVERIFY(!similar.pairs[0].identical);
        QVERIFY(ix::ImageCatalog::duplicates({red, white}, true, 6, cancel).pairs.isEmpty());
    }
    void pluginManifestArchitectureAndHashGuards() {
        QJsonObject manifest{{QStringLiteral("schema"), 1},
                             {QStringLiteral("kind"), QStringLiteral("qt-imageformats")},
                             {QStringLiteral("id"), QStringLiteral("example.codec")},
                             {QStringLiteral("version"), QStringLiteral("1.0")},
                             {QStringLiteral("os"), ix::PluginStore::platform()},
                             {QStringLiteral("arch"), QSysInfo::currentCpuArchitecture()},
                             {QStringLiteral("qtMajor"), QT_VERSION_MAJOR},
                             {QStringLiteral("qtMinor"), QT_VERSION_MINOR}};
        QVERIFY(ix::PluginStore::compatibility(manifest).isEmpty());
        auto bad = manifest;
        bad.insert(QStringLiteral("arch"), QStringLiteral("x86"));
        QVERIFY(!ix::PluginStore::compatibility(bad).isEmpty());
        bad = manifest;
        bad.insert(QStringLiteral("id"), QStringLiteral("../outside"));
        QVERIFY(!ix::PluginStore::compatibility(bad).isEmpty());
        ix::PluginStore store(dir(QStringLiteral("plugin-store")));
        QVERIFY(!store.install(QJsonDocument(manifest).toJson(), QString(64, QLatin1Char('0'))).isEmpty());
        auto builtins = ix::PluginStore::builtins();
        QVERIFY(!builtins.isEmpty());
        auto plugin = builtins.first().toObject();
        QFile file(plugin.value(QStringLiteral("path")).toString());
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto bytes = file.readAll();
        auto sha = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        manifest.insert(
            QStringLiteral("files"),
            QJsonArray{QJsonObject{{QStringLiteral("name"), file.fileName().section(QLatin1Char('/'), -1)},
                                   {QStringLiteral("role"), QStringLiteral("plugin")},
                                   {QStringLiteral("sha256"), sha},
                                   {QStringLiteral("data"), QString::fromLatin1(bytes.toBase64())}}});
        auto error = store.install(QJsonDocument(manifest).toJson());
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(store.installed().size(), 1);
        QVERIFY(store.setEnabled(QStringLiteral("example.codec"), false).isEmpty());
        QVERIFY(!store.installed()[0].toObject().value(QStringLiteral("enabled")).toBool());
        QVERIFY(store.setBuiltinEnabled({QStringLiteral("jxl")}, false).isEmpty());
        QVERIFY(store.disabledFormats().contains(QStringLiteral("jxl")));
    }
    void lazyExtensionManagerLoadsAndOpens() {
#ifdef IX_TEST_EXTENSIONS_MODULE
#ifdef Q_OS_WIN
        QVERIFY(GetModuleHandleW(L"Qt6Network.dll") == nullptr);
#endif
        QLibrary library(ix::optionalModulePath(QStringLiteral(IX_TEST_EXTENSIONS_MODULE)));
        library.setLoadHints(QLibrary::PreventUnloadHint);
        using Open = void (*)(QWidget *);
        auto open = reinterpret_cast<Open>(library.resolve("ixOpenExtensionManager"));
        QVERIFY2(open, qPrintable(library.errorString()));
        bool shown = false, https = false;
        QTimer::singleShot(150, qApp, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog && dialog->objectName() == QStringLiteral("extensionManagerDialog")) {
                https = dialog->property("httpsAvailable").toBool();
                shown = true;
                dialog->grab().save(QStringLiteral(IX_TEST_OUTPUT "/extensions.png"));
                dialog->reject();
            }
        });
        open(nullptr);
        QVERIFY(shown);
        QVERIFY(https);
#endif
    }
    void libraryAndJournalDialogs() {
        auto source = dir(QStringLiteral("library-ui.png"));
        QVERIFY(pattern().save(source));
        bool shown = false;
        QTimer::singleShot(150, qApp, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog && dialog->objectName() == QStringLiteral("libraryDialog")) {
                shown = true;
                dialog->grab().save(QStringLiteral(IX_TEST_OUTPUT "/library.png"));
                dialog->reject();
            }
        });
        ix::libraryDialog(nullptr, {source}, [](QString) {});
        QVERIFY(shown);
        QTimer::singleShot(30, qApp, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        ix::journalDialog(nullptr);
    }
    void duplicateResultOpensWithoutManualDialogClosing() {
        const auto first = dir(QStringLiteral("duplicate-open-a.png")),
                   second = dir(QStringLiteral("duplicate-open-b.png"));
        QVERIFY(pattern().save(first));
        QVERIFY(QFile::copy(first, second));
        ix::MainWindow window;
        window.openPath(first);
        QTRY_COMPARE(window.document().path, first);
        bool searched = false, opened = false, closedOnOpen = false;
        QPointer<QDialog> library;
        QTimer driver, watchdog;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (!searched && dialog->objectName() == QStringLiteral("libraryDialog")) {
                library = dialog;
                for (auto *button : dialog->findChildren<QPushButton *>())
                    if (button->text() == ix::localized(QStringLiteral("Identische Bilder"))) {
                        searched = true;
                        QTimer::singleShot(0, button, &QPushButton::click);
                        break;
                    }
            } else if (!opened && dialog->objectName() == QStringLiteral("duplicateDialog")) {
                auto *table = dialog->findChild<QTableWidget *>();
                if (!table || table->rowCount() != 1)
                    return;
                opened = true;
                QMetaObject::invokeMethod(table, "cellDoubleClicked", Qt::DirectConnection,
                                          Q_ARG(int, 0), Q_ARG(int, 1));
                closedOnOpen = !dialog->isVisible() && library && !library->isVisible();
                if (dialog->isVisible())
                    dialog->reject();
                if (library && library->isVisible())
                    library->reject();
            }
        });
        connect(&watchdog, &QTimer::timeout, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        driver.start(20);
        watchdog.start(5000);
        ix::libraryDialog(&window, {first, second}, [&](const QString &path) { window.openPath(path); });
        driver.stop();
        watchdog.stop();
        QVERIFY(searched);
        QVERIFY(opened);
        QTRY_COMPARE(window.document().path, second);
        QVERIFY2(closedOnOpen, "Opening a duplicate left the modal library/results blocking the image");
    }
    void fullscreenCloseKeepsWindowFitPreference() {
        QSettings settings;
        const auto old = settings.value(QStringLiteral("view/fit"));
        const auto geometry = settings.value(QStringLiteral("window/geometry"));
        const auto remember = settings.value(QStringLiteral("window/rememberGeometry"));
        auto restore = qScopeGuard([&] {
            settings.setValue(QStringLiteral("view/fit"), old);
            settings.setValue(QStringLiteral("window/geometry"), geometry);
            settings.setValue(QStringLiteral("window/rememberGeometry"), remember);
        });
        settings.remove(QStringLiteral("window/geometry"));
        settings.setValue(QStringLiteral("window/rememberGeometry"), true);
        const auto path = dir(QStringLiteral("fullscreen-close.png"));
        QVERIFY(pattern().save(path));
        ix::MainWindow window;
        window.show();
        window.openPath(path);
        QTRY_COMPARE(window.document().path, path);
        window.findChild<ix::ImageView *>()->setFit(ix::ImageView::Fit::Original);
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(window.isFullScreen());
        QVERIFY(window.close());
        QCOMPARE(settings.value(QStringLiteral("view/fit")).toInt(), int(ix::ImageView::Fit::Original));
        ix::MainWindow reopened;
        reopened.show();
        reopened.openPath(path);
        QTRY_COMPARE(reopened.document().path, path);
        QVERIFY(!reopened.isFullScreen());
        QCOMPARE(reopened.findChild<ix::ImageView *>()->browsingFit(), ix::ImageView::Fit::Original);
        reopened.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(reopened.isFullScreen());
        settings.setValue(QStringLiteral("window/geometry"), reopened.saveGeometry());
        ix::MainWindow legacySettings;
        legacySettings.show();
        QVERIFY(!legacySettings.isFullScreen());
        legacySettings.close();
        reopened.activateWindow();
        auto *restoredLayout = reopened.centralWidget()->layout();
        const auto margins = restoredLayout->contentsMargins();
        bool lateLayout = false;
        reopened.dispatch(QStringLiteral("fullscreen"));
        // Layout changes queued behind the first restored paint still need a
        // protected presentation; cleanup must follow state, not a fixed delay.
        QTimer::singleShot(0, &reopened, [&] {
            restoredLayout->setContentsMargins(margins.left() + 1, margins.top(), margins.right(), margins.bottom());
            QTimer::singleShot(0, &reopened, [&] {
#ifdef Q_OS_WIN
                if (QGuiApplication::platformName() == QStringLiteral("windows")) {
                    auto *cover = reopened.findChild<QObject *>(QStringLiteral("fullscreenTransitionCover"));
                    QVERIFY(cover && IsWindowVisible(transitionCoverHandle(cover)));
                }
#endif
                restoredLayout->setContentsMargins(margins);
                lateLayout = true;
            });
        });
        QTRY_VERIFY(lateLayout);
        QTRY_VERIFY(!reopened.findChild<QObject *>(QStringLiteral("pendingFullscreenTransition")));
        QCOMPARE(restoredLayout->contentsMargins(), margins);
        QVERIFY(!reopened.isFullScreen());
    }
    void saveReadOnlyFormatOffersSaveAs() {
        const auto path = dir(QStringLiteral("read-only-save.svg"));
        QFile source(path);
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"20\">"
                     "<rect width=\"40\" height=\"20\" fill=\"red\"/></svg>");
        source.close();
        const auto hash = ix::sha256File(path);
        ix::MainWindow window;
        window.openPath(path);
        QTRY_COMPARE(window.document().path, path);
        window.dispatch(QStringLiteral("right"));
        QTRY_VERIFY(window.document().modified);
        const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        auto restore = qScopeGuard([native] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native);
        });
        bool offered = false;
        QString suggested;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                offered = true;
                suggested = dialog->selectedFiles().value(0);
                dialog->reject();
            } else if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                message->accept();
        });
        timer.start(20);
        window.dispatch(QStringLiteral("save"));
        timer.stop();
        QCOMPARE(ix::sha256File(path), hash);
        QVERIFY(window.document().modified);
        QVERIFY2(offered, "Ctrl+S attempted to overwrite a decode-only format without offering Save as");
        QVERIFY(suggested.endsWith(QStringLiteral(".png")));
    }
    void uppercaseJpegSaveRetainsLosslessCommands() {
        const auto source = dir(QStringLiteral("uppercase-source.png")),
                   target = dir(QStringLiteral("uppercase-target.JPG"));
        QVERIFY(pattern().save(source));
        ix::MainWindow window;
        window.openPath(source);
        QTRY_COMPARE(window.document().path, source);
        const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        auto restore = qScopeGuard([native] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native);
        });
        QTimer timer;
        bool selected = false;
        connect(&timer, &QTimer::timeout, &window, [&] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); dialog && !selected) {
                selected = true;
                dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(target);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            } else if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *yes = message->button(QMessageBox::Yes))
                    yes->click();
            }
        });
        timer.start(20);
        window.dispatch(QStringLiteral("saveAs"));
        timer.stop();
        QCOMPARE(window.document().path, target);
        QCOMPARE(window.document().format, QByteArray("jpg"));
        QVERIFY(window.findChild<QAction *>(QStringLiteral("jpegTransform"))->isEnabled());
    }
    void watermarkConvertsTaggedColorSpace() {
        QImage mark(8, 8, QImage::Format_ARGB32);
        mark.fill(QColor(180, 120, 60));
        mark.setColorSpace(QColorSpace(QColorSpace::AdobeRgb));
        const auto path = dir(QStringLiteral("wide-gamut-watermark.png"));
        QVERIFY(mark.save(path));
        ix::ImageDocument source;
        source.pixels = QImage(64, 64, QImage::Format_ARGB32);
        source.pixels.fill(Qt::black);
        source.pixels.setColorSpace(QColorSpace(QColorSpace::SRgb));
        ix::PipelineProfile profile;
        profile.steps = {
            {QStringLiteral("watermark"), {{QStringLiteral("image"), path},
                {QStringLiteral("opacity"), 1.0}, {QStringLiteral("margin"), 0},
                {QStringLiteral("anchor"), QStringLiteral("topLeft")}}},
            {QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        std::atomic_bool cancel = false;
        const auto result = ix::BatchPipeline::transform(source, profile, cancel);
        const auto expected = mark.convertedToColorSpace(source.pixels.colorSpace()).pixelColor(0, 0);
        QCOMPARE(result.pixels.pixelColor(2, 2), expected);
        QCOMPARE(result.pixels.colorSpace(), source.pixels.colorSpace());
    }
    void fittedImageHasNoPhantomScrollbarsAndWheelNavigates() {
        ix::ImageView view;
        view.show();
        QSignalSpy navigation(&view, &ix::ImageView::navigate);
        for (const auto canvas : {QSize(642, 482), QSize(482, 642), QSize(983, 617)}) {
            view.resize(canvas);
            for (const auto image : {QSize(1281, 961), QSize(961, 1281), QSize(3973, 2235),
                                     QSize(1050, 700), QSize(700, 1050), QSize(1086, 724), QSize(724, 1086)}) {
                view.setImage(pattern(image.width(), image.height()));
                for (const auto fit : {ix::ImageView::Fit::Window, ix::ImageView::Fit::LargeOnly}) {
                    view.setZoom(2);
                    QCoreApplication::processEvents();
                    view.setFit(fit);
                    QTRY_COMPARE_WITH_TIMEOUT(view.horizontalScrollBar()->maximum(), 0, 1000);
                    QTRY_COMPARE_WITH_TIMEOUT(view.verticalScrollBar()->maximum(), 0, 1000);
                    QTRY_VERIFY_WITH_TIMEOUT(image.width() * view.zoom() <= view.viewport()->width() + 1e-7,
                                             1000);
                    QTRY_VERIFY_WITH_TIMEOUT(image.height() * view.zoom() <= view.viewport()->height() + 1e-7,
                                             1000);
                    const auto point = QPointF(view.viewport()->rect().center());
                    QWheelEvent wheel(point, view.viewport()->mapToGlobal(point.toPoint()), {},
                                      QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                                      Qt::NoScrollPhase, false);
                    QCoreApplication::sendEvent(view.viewport(), &wheel);
                    QCOMPARE(navigation.size(), 1);
                    QCOMPARE(navigation.takeFirst().at(0).toInt(), 1);
                }
            }
        }
        view.setZoom(2);
        QCoreApplication::processEvents();
        view.verticalScrollBar()->setValue(100);
        const auto before = view.verticalScrollBar()->value();
        const auto point = QPointF(view.viewport()->rect().center());
        QWheelEvent wheel(point, view.viewport()->mapToGlobal(point.toPoint()), {}, QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view.viewport(), &wheel);
        QVERIFY(view.verticalScrollBar()->value() > before);
        QVERIFY(navigation.isEmpty());
        QSettings settings;
        const QStringList keys{QStringLiteral("view/fit"), QStringLiteral("view/previewStrip"),
                               QStringLiteral("navigation/wrap"), QStringLiteral("view/lockZoom"),
                               QStringLiteral("view/lockScroll")};
        QVariantMap previous;
        for (const auto &key : keys) previous.insert(key, settings.value(key));
        const auto restore = qScopeGuard([&] {
            for (const auto &key : keys)
                if (previous[key].isValid()) settings.setValue(key, previous[key]);
                else settings.remove(key);
        });
        settings.setValue(QStringLiteral("navigation/wrap"), true);
        settings.setValue(QStringLiteral("view/lockZoom"), false);
        settings.setValue(QStringLiteral("view/lockScroll"), false);
        const auto largeFolder = dir(QStringLiteral("large-fitted-panoramas"));
        QVERIFY(QDir().mkpath(largeFolder));
        const auto largePath = largeFolder + QStringLiteral("/1.png");
        const auto nextPath = largeFolder + QStringLiteral("/2.png");
        QImage panorama(20003, 1003, QImage::Format_RGB32);
        panorama.fill(QColor(31, 113, 179));
        QVERIFY(panorama.save(largePath));
        QVERIFY(QFile::copy(largePath, nextPath));
        QSettings().setValue(QStringLiteral("view/fit"), int(ix::ImageView::Fit::LargeOnly));
        ix::MainWindow window;
        window.resize(997, 713);
        window.show();
        window.openPath(largePath);
        QTRY_VERIFY(window.document().path == largePath && !window.isBusy());
        auto *largeView = window.findChild<ix::ImageView *>();
        auto *zoom = window.findChild<QComboBox *>(QStringLiteral("zoom"));
        QVERIFY(zoom);
        // Reading/confirming the displayed rounded percentage must retain fit.
        zoom->lineEdit()->setFocus();
        QTest::keyClick(zoom->lineEdit(), Qt::Key_Return);
        QCOMPARE(largeView->fit(), ix::ImageView::Fit::LargeOnly);
        zoom->lineEdit()->setFocus();
        zoom->lineEdit()->selectAll();
        QTest::keyClicks(zoom->lineEdit(), QStringLiteral("42"));
        QTest::keyClick(zoom->lineEdit(), Qt::Key_Escape);
        QCOMPARE(largeView->fit(), ix::ImageView::Fit::LargeOnly);
        for (bool previews : {false, true}) {
            window.findChild<QAction *>(QStringLiteral("previewStripToggle"))->setChecked(previews);
            for (const auto size : {QSize(997, 713), QSize(801, 603)}) {
                window.resize(size);
                window.dispatch(QStringLiteral("fullscreen"));
                QTRY_VERIFY(window.isFullScreen());
                window.dispatch(QStringLiteral("fullscreen"));
                QTRY_VERIFY(!window.isFullScreen());
                QTRY_VERIFY(!window.findChild<QObject *>(QStringLiteral("fullscreenTransitionCover")));
                QCOMPARE(largeView->fit(), ix::ImageView::Fit::LargeOnly);
                QTRY_COMPARE(largeView->visibleImageRect(), panorama.rect());
                QCOMPARE(largeView->horizontalScrollBar()->maximum(), 0);
                QCOMPARE(largeView->verticalScrollBar()->maximum(), 0);
                QVERIFY(panorama.width() * largeView->zoom() <= largeView->viewport()->width() + 1e-7);
                QVERIFY(panorama.height() * largeView->zoom() <= largeView->viewport()->height() + 1e-7);
                QSignalSpy folderNavigation(largeView, &ix::ImageView::navigate);
                QTest::keyClick(largeView, Qt::Key_Right);
                QCOMPARE(folderNavigation.size(), 1);
                QTRY_VERIFY(window.document().path == nextPath && !window.isBusy());
                QCOMPARE(largeView->fit(), ix::ImageView::Fit::LargeOnly);
                const auto center = QPointF(largeView->viewport()->rect().center());
                QWheelEvent next(center, largeView->viewport()->mapToGlobal(center.toPoint()), {},
                    QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(largeView->viewport(), &next);
                QCOMPARE(folderNavigation.size(), 2);
                QTRY_VERIFY(window.document().path == largePath && !window.isBusy());
            }
        }
        // An actual user edit still selects a manual zoom on focus loss.
        zoom->lineEdit()->setFocus();
        zoom->lineEdit()->selectAll();
        QTest::keyClicks(zoom->lineEdit(), QStringLiteral("25"));
        largeView->setFocus();
        QTRY_COMPARE(largeView->fit(), ix::ImageView::Fit::Original);
        QCOMPARE(largeView->zoom(), .25);
    }
    void imageChangesDiscardEditsByDefaultButClosingStillAsks() {
        QSettings settings;
        const auto old = settings.value(QStringLiteral("navigation/askToSave"));
        settings.remove(QStringLiteral("navigation/askToSave"));
        auto restore = qScopeGuard([&] { settings.setValue(QStringLiteral("navigation/askToSave"), old); });
        const auto folder = dir(QStringLiteral("discard-on-navigation"));
        QVERIFY(QDir().mkpath(folder));
        const auto first = folder + QStringLiteral("/1.png"), second = folder + QStringLiteral("/2.png");
        const auto original = pattern(640, 480);
        QVERIFY(original.save(first));
        QVERIFY(pattern(640, 481).save(second));
        const auto hash = ix::sha256File(first);
        ix::MainWindow window;
        window.show();
        window.openPath(first);
        QTRY_COMPARE(window.document().path, first);
        auto *view = window.findChild<ix::ImageView *>();
        view->setFit(ix::ImageView::Fit::LargeOnly);
        view->setSelection({10, 10, 120, 90});
        window.dispatch(QStringLiteral("crop"));
        QTRY_VERIFY(window.document().modified);
        QCOMPARE(window.document().pixels.size(), QSize(120, 90));
        int questions = 0;
        bool closing = false, errorShown = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (message->standardButtons().testFlag(QMessageBox::Save)) {
                    ++questions;
                    message->button(closing ? QMessageBox::Cancel : QMessageBox::Discard)->click();
                } else {
                    errorShown = true;
                    message->accept();
                }
            }
        });
        timer.start(20);
        QTest::keyClick(view, Qt::Key_Right);
        QTRY_COMPARE(window.document().path, second);
        QCOMPARE(questions, 0);
        QVERIFY(!window.document().modified);
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
        QCOMPARE(ix::sha256File(first), hash);
        window.openPath(first);
        QTRY_COMPARE(window.document().path, first);
        QCOMPARE(window.document().pixels, original);
        window.dispatch(QStringLiteral("grayscale"));
        QTRY_VERIFY(window.document().modified);
        const auto edited = window.document().pixels;
        window.openPath(folder + QStringLiteral("/missing.png"));
        QTRY_VERIFY(errorShown);
        QCOMPARE(questions, 0);
        QCOMPARE(window.document().path, first);
        QVERIFY(window.document().modified);
        QCOMPARE(window.document().pixels, edited);
        const auto point = QPointF(view->viewport()->rect().center());
        QWheelEvent wheel(point, view->viewport()->mapToGlobal(point.toPoint()), {}, QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view->viewport(), &wheel);
        QTRY_COMPARE(window.document().path, second);
        QCOMPARE(questions, 0);
        window.dispatch(QStringLiteral("grayscale"));
        QTRY_VERIFY(window.document().modified);
        closing = true;
        QVERIFY(!window.close());
        QCOMPARE(questions, 1);
        QVERIFY(window.document().modified);
        QCOMPARE(ix::sha256File(first), hash);
        closing = false;
        const auto frames = folder + QStringLiteral("/frames.gif");
        QFile file(frames);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(gif());
        file.close();
        const auto frameHash = ix::sha256File(frames);
        window.openPath(frames);
        QTRY_COMPARE(window.document().pageCount, 2);
        window.dispatch(QStringLiteral("animation"));
        window.dispatch(QStringLiteral("grayscale"));
        QTRY_VERIFY(window.document().modified);
        window.findChild<QAction *>(QStringLiteral("nextPage"))->trigger();
        QTRY_COMPARE(window.document().page, 1);
        window.dispatch(QStringLiteral("animation"));
        QCOMPARE(questions, 1);
        QVERIFY(!window.document().modified);
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
        QCOMPARE(ix::sha256File(frames), frameHash);
    }
    void imageChangeSaveQuestionCanBeEnabled() {
        QSettings settings;
        const auto old = settings.value(QStringLiteral("navigation/askToSave"));
        settings.setValue(QStringLiteral("navigation/askToSave"), true);
        auto restore = qScopeGuard([&] { settings.setValue(QStringLiteral("navigation/askToSave"), old); });
        const auto first = dir(QStringLiteral("ask-navigation-1.png")),
                   second = dir(QStringLiteral("ask-navigation-2.png"));
        QVERIFY(pattern().save(first));
        QVERIFY(pattern(65, 48).save(second));
        ix::MainWindow window;
        window.openPath(first);
        QTRY_COMPARE(window.document().path, first);
        window.dispatch(QStringLiteral("grayscale"));
        QTRY_VERIFY(window.document().modified);
        const auto edited = window.document().pixels;
        int questions = 0;
        auto answer = QMessageBox::Cancel;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++questions;
                message->button(answer)->click();
            }
        });
        timer.start(20);
        window.openPath(second);
        QCOMPARE(questions, 1);
        QCOMPARE(window.document().path, first);
        QCOMPARE(window.document().pixels, edited);
        QVERIFY(window.document().modified);
        answer = QMessageBox::Discard;
        window.openPath(second);
        QTRY_COMPARE(window.document().path, second);
        QCOMPARE(questions, 2);
        QVERIFY(!window.document().modified);
    }
    void aspect() {
        ix::ResizeModel m({400, 200});
        m.setWidth(100);
        QCOMPARE(m.size(), QSize(100, 50));
        m.setHeight(80);
        QCOMPARE(m.size(), QSize(160, 80));
    }
    void independentAxes() {
        ix::ResizeModel m({400, 200});
        m.proportional = false;
        m.setWidth(125);
        QCOMPARE(m.size(), QSize(125, 200));
        m.setPercentHeight(25);
        QCOMPARE(m.size(), QSize(125, 50));
    }
    void units() {
        ix::ResizeModel m({300, 600}, 300);
        m.setUnit(ix::ResizeModel::Unit::Inch);
        QCOMPARE(m.width(), 1.0);
        m.setWidth(2);
        QCOMPARE(m.size(), QSize(600, 1200));
        m.setUnit(ix::ResizeModel::Unit::Centimeter);
        QCOMPARE(m.width(), 5.08);
        m.setWidth(2.54);
        QCOMPARE(m.size(), QSize(300, 600));
        m.setDpi(600);
        QCOMPARE(m.size(), QSize(600, 1200));
    }
    void percentAndMegapixel() {
        ix::ResizeModel m({4000, 2000});
        m.setPercentWidth(50);
        QCOMPARE(m.size(), QSize(2000, 1000));
        m.setMegapixels(2);
        QCOMPARE(m.size(), QSize(2000, 1000));
        m.preset({600, 600});
        QCOMPARE(m.size(), QSize(600, 300));
        m.swapSides();
        QCOMPARE(m.size(), QSize(300, 600));
        m.preset({800, 600}, false);
        QCOMPARE(m.size(), QSize(800, 600));
    }
    void dpiAdaptation() {
        ix::ResizeModel m({400, 200}, 96);
        m.adaptDpi = true;
        m.setWidth(200);
        QCOMPARE(m.dpi(), 48.0);
    }
    void memoryGuards() {
        QVERIFY(!ix::safeSize({0, 10}));
        QVERIFY(!ix::safeSize({100001, 1}));
        QVERIFY(!ix::safeSize({100000, 100000}));
        QVERIFY(ix::safeSize({4000, 3000}));
        ix::ResizeOptions o;
        o.size = {100000, 100000};
        QVERIFY_EXCEPTION_THROWN(ix::resample(pattern(), o), std::runtime_error);
    }
    void filterDifferences() {
        QSet<QByteArray> outputs;
        for (int i = 0; i < 7; ++i) {
            ix::ResizeOptions o;
            o.size = {37, 25};
            o.filter = ix::ResampleFilter(i);
            o.fastDownscale = false;
            auto image = ix::resample(pattern(), o);
            QCOMPARE(image.size(), o.size);
            QVERIFY(!image.isNull());
            outputs.insert(
                QByteArray(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()));
        }
        QCOMPARE(outputs.size(), 7);
    }
    void nearest() {
        QImage image(2, 1, QImage::Format_ARGB32);
        image.setPixelColor(0, 0, Qt::red);
        image.setPixelColor(1, 0, Qt::blue);
        ix::ResizeOptions o;
        o.size = {4, 1};
        o.filter = ix::ResampleFilter::Nearest;
        auto result = ix::resample(image, o);
        QCOMPARE(result.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(result.pixelColor(1, 0), QColor(Qt::red));
        QCOMPARE(result.pixelColor(2, 0), QColor(Qt::blue));
        for (auto format : {QImage::Format_ARGB32, QImage::Format_RGBA64}) {
            auto pixels = pattern(3, 2).convertToFormat(format);
            pixels.setPixelColor(1, 0, QColor(0, 123, 255, 51));
            for (int factor : {2, 3, 4}) {
                o.size = pixels.size() * factor;
                const auto enlarged = ix::resample(pixels, o);
                QCOMPARE(enlarged.format(), pixels.format());
                for (int y = 0; y < enlarged.height(); ++y)
                    for (int x = 0; x < enlarged.width(); ++x)
                        QCOMPARE(enlarged.pixelColor(x, y), pixels.pixelColor(x / factor, y / factor));
            }
        }
    }
    void alphaInterpolation() {
        QImage image(2, 1, QImage::Format_ARGB32);
        image.setPixelColor(0, 0, QColor(255, 0, 0, 0));
        image.setPixelColor(1, 0, QColor(0, 0, 255, 255));
        ix::ResizeOptions o;
        o.size = {1, 1};
        o.filter = ix::ResampleFilter::Triangle;
        o.fastDownscale = false;
        auto result = ix::resample(image, o);
        auto c = result.pixelColor(0, 0);
        QVERIFY(c.red() < 2);
        QVERIFY(c.blue() > 250);
        QVERIFY(c.alpha() > 120 && c.alpha() < 135);
    }
    void gammaInterpolation() {
        QImage image(2, 1, QImage::Format_RGB32);
        image.setPixelColor(0, 0, Qt::black);
        image.setPixelColor(1, 0, Qt::white);
        ix::ResizeOptions o;
        o.size = {1, 1};
        o.filter = ix::ResampleFilter::Triangle;
        o.fastDownscale = false;
        auto standard = ix::resample(image, o);
        o.linearLight = true;
        auto linear = ix::resample(image, o);
        QVERIFY(standard.pixelColor(0, 0).red() < 140);
        QVERIFY(linear.pixelColor(0, 0).red() > 180);
    }
    void processingProperties() {
        auto image = pattern();
        auto r = ix::rotate(image, 90);
        QCOMPARE(r.size(), QSize(48, 64));
        QCOMPARE(r.dotsPerMeterX(), image.dotsPerMeterX());
        auto n = ix::negative(ix::negative(image));
        QCOMPARE(n, image);
        auto gray = ix::grayscale(image);
        for (int y = 0; y < gray.height(); ++y)
            for (int x = 0; x < gray.width(); ++x) {
                auto c = gray.pixelColor(x, y);
                QCOMPARE(c.red(), c.green());
                QCOMPARE(c.green(), c.blue());
            }
        auto unchanged = ix::adjustColors(image, {});
        QCOMPARE(unchanged, image);
    }
    void borderCropping() {
        QImage image(10, 10, QImage::Format_RGB32);
        image.fill(Qt::white);
        for (int y = 3; y < 7; ++y)
            for (int x = 2; x < 8; ++x)
                image.setPixelColor(x, y, Qt::red);
        QCOMPARE(ix::autoCropRect(image), QRect(2, 3, 6, 4));
        auto c = ix::canvas(image, 1, 2, 3, 4, Qt::black);
        QCOMPARE(c.size(), QSize(13, 17));
        QCOMPARE(c.pixelColor(0, 0), QColor(Qt::black));
        QCOMPARE(c.pixelColor(3, 6), QColor(Qt::red));
    }
    void png16AndICC() {
        ix::ImageDocument d;
        d.pixels = QImage(3, 2, QImage::Format_RGBA64);
        d.pixels.fill(QColor::fromRgba64(12345, 23456, 34567, 45678));
        d.pixels.setColorSpace(QColorSpace::DisplayP3);
        d.pixels.setDotsPerMeterX(11811);
        d.pixels.setDotsPerMeterY(11811);
        d.pixels.setText(QStringLiteral("Description"), QStringLiteral("IxView fixture"));
        ix::CodecRegistry registry;
        auto path = dir(QStringLiteral("16bit_Größe_测试.png"));
        QVERIFY2(registry.save(d, path, "png", 95, true).isEmpty(), "PNG save");
        auto read = registry.read(path);
        QVERIFY2(read.ok(), qPrintable(read.error));
        QVERIFY(read.document.pixels.depth() > 32);
        auto a = reinterpret_cast<const QRgba64 *>(d.pixels.constScanLine(0))[0],
             b = reinterpret_cast<const QRgba64 *>(read.document.pixels.constScanLine(0))[0];
        QCOMPARE(a, b);
        QCOMPARE(read.document.pixels.colorSpace(), d.pixels.colorSpace());
        QCOMPARE(read.document.pixels.dotsPerMeterX(), 11811);
        QCOMPARE(read.document.pixels.text(QStringLiteral("Description")), QStringLiteral("IxView fixture"));
    }
    void invalidImages() {
        ix::CodecRegistry registry;
        QVERIFY(!registry.read(dir(QStringLiteral("missing.png"))).ok());
        QFile file(dir(QStringLiteral("bad.png")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not an image");
        file.close();
        QVERIFY(!registry.read(file.fileName()).ok());
        QFile ppm(dir(QStringLiteral("huge.ppm")));
        QVERIFY(ppm.open(QIODevice::WriteOnly));
        ppm.write("P6\n100000 100000\n255\n");
        ppm.close();
        QVERIFY(!registry.read(ppm.fileName()).ok());
    }
    void saveDoesNotClobber() {
        ix::CodecRegistry registry;
        QString path = dir(QStringLiteral("protected.invalid"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("original");
        file.close();
        ix::ImageDocument d;
        d.pixels = pattern();
        QVERIFY(!registry.save(d, path, "invalid").isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("original"));
    }
    void exportPolicy() {
        ix::ImageDocument d;
        d.pixels = pattern();
        d.sourceHasExif = true;
        d.pageCount = 2;
        auto losses = ix::CodecRegistry::exportLosses(d, "jpeg");
        QVERIFY(losses.size() >= 2);
        d.pixels.setPixelColor(0, 0, Qt::transparent);
        QVERIFY(!ix::CodecRegistry::exportLosses(d, "bmp").isEmpty());
    }
    void grayscale16RetainsEverySampleAcrossEditing() {
        QImage ramp(256, 256, QImage::Format_Grayscale16);
        for (int y = 0; y < 256; ++y) {
            auto *row = reinterpret_cast<quint16 *>(ramp.scanLine(y));
            for (int x = 0; x < 256; ++x)
                row[x] = quint16(y * 256 + x);
        }
        const auto expected = ramp.convertToFormat(QImage::Format_RGBA64);
        ix::ImageDocument document;
        document.pixels = ramp;
        ix::CodecRegistry registry;
        const auto path = dir(QStringLiteral("grayscale16-ramp.png"));
        QVERIFY(registry.save(document, path, "png", 95, false).isEmpty());
        auto decoded = registry.read(path);
        QVERIFY(decoded.ok());
        QCOMPARE(decoded.document.pixels.convertToFormat(QImage::Format_RGBA64), expected);
        QCOMPARE(ix::imageChannelBits(decoded.document.pixels), 16);
        QVERIFY(!ix::CodecRegistry::exportLosses(document, "jpeg").isEmpty());
        ix::ResizeOptions resize;
        resize.size = ramp.size();
        resize.filter = ix::ResampleFilter::Lanczos;
        const auto identity = [](const QImage &input) { return input; };
        std::atomic_bool cancel = false;
        ix::PipelineProfile profile;
        profile.steps = {{QStringLiteral("watermark"), {{QStringLiteral("text"), QStringLiteral("Test")},
                           {QStringLiteral("opacity"), 0.0}}},
                         {QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        QVERIFY2(profile.validate().isEmpty(), qPrintable(profile.validate()));
        const QList<QImage> results{
            ix::adjustColors(ramp, {}), ix::resample(ramp, resize),
            ix::applySelectionEffect(ramp, {30, 20, 90, 70}, false, identity),
            ix::applySelectionEffect(ramp, {30, 20, 90, 70}, true, identity),
            ix::canvas(ramp, 0, 0, 0, 0, Qt::black),
            ix::BatchPipeline::transform(document, profile, cancel).pixels};
        for (const auto &result : results)
            QCOMPARE(result.convertToFormat(QImage::Format_RGBA64), expected);
        for (bool fast : {false, true}) {
            ix::RotationOptions rotation;
            rotation.angle = 90;
            rotation.fastMethod = fast;
            const auto rotated = ix::rotateImage(ramp, rotation).pixels.convertToFormat(QImage::Format_RGBA64);
            QCOMPARE(rotated, expected.transformed(QTransform().rotate(90)));
        }
        const QRect selection(30, 20, 90, 70);
        const auto changed = ix::applySelectionEffect(ramp, selection, false,
            [](const QImage &input) { return ix::negative(input); }).convertToFormat(QImage::Format_RGBA64);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                const auto value = changed.pixelColor(x, y).rgba64().red();
                QCOMPARE(value, quint16(selection.contains(x, y) ? 65535 - (y * 256 + x) : y * 256 + x));
            }
        QImage hdr(4, 2, QImage::Format_RGBA32FPx4);
        hdr.fill(Qt::white);
        auto *samples = reinterpret_cast<float *>(hdr.scanLine(0));
        samples[0] = 2.5f;
        samples[1] = -.125f;
        QCOMPARE(ix::applySelectionEffect(hdr, {0, 0, 2, 1}, false, identity), hdr);
        QCOMPARE(ix::canvas(hdr, 0, 0, 0, 0, Qt::transparent), hdr);
    }
    void editedAnimationResumeHonorsSavePolicy() {
        const auto previousAsk = QSettings().value(QStringLiteral("navigation/askToSave"));
        auto restore = qScopeGuard([previousAsk] {
            QSettings().setValue(QStringLiteral("navigation/askToSave"), previousAsk);
        });
        QSettings().setValue(QStringLiteral("navigation/askToSave"), true);
        const auto path = dir(QStringLiteral("resume-edited.png"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(ix::test::apngFixture(2, {4, 4}, 1000));
        file.close();
        ix::MainWindow window;
        window.openPath(path);
        QTRY_VERIFY(!window.document().empty() && !window.isBusy());
        auto *timer = window.findChild<QTimer *>(QStringLiteral("animationTimer"));
        QVERIFY(timer);
        QVERIFY(timer->isActive());
        window.dispatch(QStringLiteral("animation"));
        window.dispatch(QStringLiteral("negativeAll"));
        QTRY_VERIFY(window.document().modified && !window.isBusy());
        const auto edited = window.document().pixels;
        const auto page = window.document().page;
        int questions = 0;
        QTimer::singleShot(0, &window, [&] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(message);
            ++questions;
            message->button(QMessageBox::Cancel)->click();
        });
        window.dispatch(QStringLiteral("animation"));
        QCOMPARE(questions, 1);
        QCOMPARE(window.document().pixels, edited);
        QCOMPARE(window.document().page, page);
        QVERIFY(window.document().modified);
        QVERIFY(window.findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
        QVERIFY(!timer->isActive());
        QTimer::singleShot(0, &window, [&] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(message);
            ++questions;
            message->button(QMessageBox::Discard)->click();
        });
        window.dispatch(QStringLiteral("animation"));
        QCOMPARE(questions, 2);
        QVERIFY(timer->isActive());
        QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
        QTRY_VERIFY(!window.document().modified && !window.isBusy());
        QCOMPARE(window.document().page, 1);
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
        window.dispatch(QStringLiteral("animation"));
        window.dispatch(QStringLiteral("sharpenDisplay"));
        QTRY_VERIFY(!window.isBusy());
        QVERIFY(!timer->isActive());
        const auto once = dir(QStringLiteral("replay-once.png"));
        QFile oneShot(once);
        QVERIFY(oneShot.open(QIODevice::WriteOnly));
        oneShot.write(ix::test::apngFixture(2, {4, 4}, 1000, 1));
        oneShot.close();
        window.openPath(once);
        QTRY_VERIFY(window.document().path == once && !window.isBusy());
        QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
        QTRY_VERIFY(window.document().page == 1 && !window.isBusy());
        QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
        QVERIFY(!timer->isActive());
        window.dispatch(QStringLiteral("animation"));
        QTRY_VERIFY(window.document().page == 0 && !window.isBusy());
        QVERIFY(timer->isActive());
        window.dispatch(QStringLiteral("animation"));
    }
    void activeDocumentReloadsAfterFileUndoAndPipeline() {
        const auto path = dir(QStringLiteral("active-undo.png"));
        const auto staged = dir(QStringLiteral("active-stage.png"));
        const auto input = dir(QStringLiteral("active-input.png"));
        const auto root = dir(QStringLiteral("active-journal"));
        QImage original(32, 24, QImage::Format_RGB32), replacement(16, 12, QImage::Format_RGB32);
        original.fill(Qt::red);
        replacement.fill(Qt::blue);
        QVERIFY(original.save(path));
        QVERIFY(replacement.save(staged));
        QVERIFY(replacement.save(input));
        QVERIFY(ix::FileJournal(root).installOutput(staged, path, true, ix::imageFileKey(path)).isEmpty());
        ix::MainWindow window(nullptr, root);
        window.openPath(path);
        QTRY_COMPARE(window.document().pixels, replacement);
        window.findChild<QAction *>(QStringLiteral("fileUndo"))->trigger();
        QTRY_COMPARE(window.document().pixels, original);
        QVERIFY(!window.document().modified);
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
        QVERIFY(reviewedOverwrite(window, input, path));
        QTRY_COMPARE(window.document().pixels, replacement);
        QVERIFY(!window.document().modified);
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
        QCOMPARE(window.document().sourceRevision, ix::imageFileKey(path));
    }
    void dirtyDocumentSurvivesPipelineReplacementAndFileUndo() {
        const auto path = dir(QStringLiteral("dirty-active.png")), input = dir(QStringLiteral("dirty-input.png"));
        const auto root = dir(QStringLiteral("dirty-journal"));
        QImage original(32, 24, QImage::Format_RGB32), replacement(16, 12, QImage::Format_RGB32);
        original.fill(Qt::red);
        replacement.fill(Qt::blue);
        QVERIFY(original.save(path));
        QVERIFY(replacement.save(input));
        ix::MainWindow window(nullptr, root);
        window.openPath(path);
        QTRY_COMPARE(window.document().pixels, original);
        window.dispatch(QStringLiteral("negativeAll"));
        QTRY_VERIFY(window.document().modified);
        const auto edited = window.document().pixels;
        int warnings = 0;
        QTimer acknowledge;
        connect(&acknowledge, &QTimer::timeout, &window, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++warnings;
                if (auto *ok = message->button(QMessageBox::Ok))
                    ok->click();
                else
                    message->reject();
            }
        });
        acknowledge.start(10);
        QVERIFY(reviewedOverwrite(window, input, path));
        QCOMPARE(warnings, 1);
        QCOMPARE(window.document().pixels, edited);
        QVERIFY(window.document().modified);
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY(!window.isBusy());
        QCOMPARE(window.document().pixels, original);
        QVERIFY(window.document().modified); // Original pixels are now a local edit over the blue file.
        window.findChild<QAction *>(QStringLiteral("fileUndo"))->trigger();
        QCOMPARE(QImage(path), original);
        QCOMPARE(window.document().pixels, original);
        QVERIFY(window.document().modified);
        QCOMPARE(warnings, 2);
        acknowledge.stop();
    }
    void undoRedoDisplayRunsInWorker() {
        QSettings settings;
        const auto previous = settings.value(QStringLiteral("view/sharpenDisplay"));
        auto restore = qScopeGuard([previous] { QSettings().setValue(QStringLiteral("view/sharpenDisplay"), previous); });
        settings.setValue(QStringLiteral("view/sharpenDisplay"), true);
        const auto path = dir(QStringLiteral("history-worker.png"));
        const auto original = pattern(800, 600);
        QVERIFY(original.save(path));
        ix::MainWindow window;
        window.openPath(path);
        auto *options = window.findChild<QAction *>(QStringLiteral("settings"));
        QTRY_VERIFY(!window.document().empty() && !window.isBusy());
        window.dispatch(QStringLiteral("negativeAll"));
        QTRY_VERIFY(window.document().modified && !window.isBusy());
        const auto edited = window.document().pixels;
        window.dispatch(QStringLiteral("undo"));
        QVERIFY(!options->isEnabled());
        QCOMPARE(window.document().pixels, edited); // No history commit until worker completion.
        bool heartbeat = false;
        QTimer::singleShot(0, &window, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        QTRY_VERIFY(options->isEnabled());
        QCOMPARE(window.document().pixels, original);
        window.dispatch(QStringLiteral("redo"));
        QVERIFY(!options->isEnabled());
        QCOMPARE(window.document().pixels, original);
        QTRY_VERIFY(options->isEnabled());
        QCOMPARE(window.document().pixels, edited);
    }
    void apngSessionsAdvanceLinearlyAndInvalidate() {
        const auto restore = qScopeGuard([] { ix::ImageLoadCache::configure(192, 16, true); ix::ImageLoadCache::clear(); });
        ix::CodecRegistry registry;
        for (int budget : {0, 16, 192}) {
            ix::ImageLoadCache::configure(budget, 16, true);
            for (int count : {20, 40, 80}) {
                const auto path = dir(QStringLiteral("session-%1-%2.png").arg(budget).arg(count));
                QFile file(path);
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(ix::test::apngFixture(count));
                file.close();
                ix::ImageLoadCache::clear();
                const auto before = ix::ApngCodec::cacheStats();
                QImage first;
                for (int page = 0; page < count; ++page) {
                    const auto frame = ix::ImageLoadCache::read(registry, path, page);
                    QVERIFY2(frame.ok(), qPrintable(frame.error));
                    QCOMPARE(frame.document.pixels.pixelColor(0, 0),
                        QColor((page * 41 + 17) % 256, (page * 13 + 34) % 256, (page * 7 + 51) % 256));
                    if (page == 0)
                        first = frame.document.pixels;
                }
                const auto after = ix::ApngCodec::cacheStats();
                QCOMPARE(after.filesParsed - before.filesParsed, quint64(1));
                QCOMPARE(after.framesComposited - before.framesComposited, quint64(count));
                QVERIFY(after.retainedBytes <= after.budgetBytes);
                QCOMPARE(first.pixelColor(0, 0), QColor(17, 34, 51));
                const auto backward = registry.read(path, 4);
                QVERIFY(backward.ok());
                QCOMPARE(backward.document.pixels.pixelColor(0, 0), QColor(181, 86, 79));
                QCOMPARE(ix::ApngCodec::cacheStats().framesComposited - after.framesComposited, quint64(5));
                const auto stamp = QFileInfo(path).lastModified();
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(ix::test::apngFixture(count, {128, 128}, 1, 1));
                QVERIFY(file.setFileTime(stamp, QFileDevice::FileModificationTime));
                file.close();
                const auto changed = ix::ImageLoadCache::read(registry, path);
                QVERIFY(changed.ok());
                QCOMPARE(changed.document.loopCount, 0);
                QCOMPARE(ix::ApngCodec::cacheStats().filesParsed - after.filesParsed, quint64(1));
            }
        }
        ix::ImageLoadCache::clear();
        QCOMPARE(ix::ApngCodec::cacheStats().retainedBytes, qint64(0));
        const auto large = dir(QStringLiteral("session-over-budget.png"));
        QFile file(large);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(ix::test::apngFixture(1, {4096, 4096}));
        file.close();
        auto oversized = registry.read(large);
        QVERIFY2(oversized.ok(), qPrintable(oversized.error));
        QCOMPARE(oversized.document.pixels.size(), QSize(4096, 4096));
        QCOMPARE(ix::ApngCodec::cacheStats().retainedBytes, qint64(0));
    }
    void animationFrames() {
        QFile file(dir(QStringLiteral("two.gif")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(gif());
        file.close();
        ix::CodecRegistry registry;
        auto first = registry.read(file.fileName());
        auto second = registry.read(file.fileName(), 1);
        QVERIFY2(first.ok(), qPrintable(first.error));
        QVERIFY2(second.ok(), qPrintable(second.error));
        QCOMPARE(first.document.pageCount, 2);
        QVERIFY(first.document.animated);
        QCOMPARE(first.document.pixels.pixelColor(0, 0).red(), 0);
        QCOMPARE(second.document.pixels.pixelColor(0, 0).red(), 255);
        QVERIFY(!registry.read(file.fileName(), 2).ok());
    }
    void tiffPages() {
        auto path = dir(QStringLiteral("pages.tiff"));
        auto *tiff = TIFFOpen(QFile::encodeName(path).constData(), "w");
        QVERIFY(tiff);
        for (int page = 0; page < 2; ++page) {
            TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 2);
            TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 1);
            TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
            TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
            TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
            TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
            TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
            unsigned char pixels[6] = {0, 0, 0, 0, 0, 0};
            pixels[page] = 255;
            pixels[3 + page] = 255;
            QVERIFY(TIFFWriteScanline(tiff, pixels, 0, 0) >= 0);
            QVERIFY(TIFFWriteDirectory(tiff));
        }
        TIFFClose(tiff);
        QFile::copy(path, QStringLiteral(IX_TEST_OUTPUT "/multipage.tiff"));
        ix::CodecRegistry registry;
        auto first = registry.read(path), second = registry.read(path, 1);
        QVERIFY2(first.ok(), qPrintable(first.error));
        QVERIFY2(second.ok(), qPrintable(second.error));
        QCOMPARE(first.document.pageCount, 2);
        QVERIFY(!first.document.animated);
        QCOMPARE(first.document.pixels.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(second.document.pixels.pixelColor(0, 0), QColor(Qt::green));
    }
    void apngComposition() {
        auto be32 = [](quint32 value) {
            QByteArray data(4, Qt::Uninitialized);
            qToBigEndian(value, reinterpret_cast<uchar *>(data.data()));
            return data;
        };
        auto crc = [](QByteArray data) {
            quint32 c = 0xffffffff;
            for (auto b : data) {
                c ^= quint8(b);
                for (int n = 0; n < 8; ++n)
                    c = (c >> 1) ^ (0xedb88320 & quint32(-qint32(c & 1)));
            }
            return ~c;
        };
        auto chunk = [&](QByteArray type, QByteArray data) {
            return be32(quint32(data.size())) + type + data + be32(crc(type + data));
        };
        QByteArray png = QByteArray::fromHex("89504e470d0a1a0a");
        png += chunk("IHDR", be32(3) + be32(1) + QByteArray::fromHex("0806000000"));
        png += chunk("acTL", be32(4) + be32(0));
        quint32 sequence = 0;
        auto frame = [&](int x, int width, int disposal, int blend, QByteArray rgba, bool first) {
            QByteArray control = be32(sequence++) + be32(width) + be32(1) + be32(x) + be32(0) +
                                 QByteArray::fromHex("0001000a") + char(disposal) + char(blend);
            png += chunk("fcTL", control);
            auto compressed = qCompress(QByteArray(1, 0) + rgba).mid(4);
            png += first ? chunk("IDAT", compressed) : chunk("fdAT", be32(sequence++) + compressed);
        };
        frame(0, 3, 0, 0, QByteArray::fromHex("ff0000ffff0000ffff0000ff"), true);
        frame(1, 1, 2, 0, QByteArray::fromHex("0000ffff"), false);
        frame(0, 1, 1, 0, QByteArray::fromHex("00ff00ff"), false);
        frame(2, 1, 0, 1, QByteArray::fromHex("ffffff80"), false);
        png += chunk("IEND", {});
        QFile file(dir(QStringLiteral("animation.png")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(png);
        file.close();
        ix::CodecRegistry registry;
        auto second = registry.read(file.fileName(), 1);
        QVERIFY2(second.ok(), qPrintable(second.error));
        QCOMPARE(second.document.pageCount, 4);
        QCOMPARE(second.document.pixels.pixelColor(1, 0), QColor(Qt::blue));
        auto third = registry.read(file.fileName(), 2);
        QVERIFY(third.ok());
        QCOMPARE(third.document.pixels.pixelColor(0, 0), QColor(Qt::green));
        QCOMPARE(third.document.pixels.pixelColor(1, 0), QColor(Qt::red));
        auto fourth = registry.read(file.fileName(), 3);
        QVERIFY(fourth.ok());
        QCOMPARE(fourth.document.pixels.pixelColor(0, 0).alpha(), 0);
        QVERIFY(fourth.document.pixels.pixelColor(2, 0).green() > 120);
        QCOMPARE(fourth.document.loopCount, -1);
        QCOMPARE(fourth.document.delayMs, 100);
        QVERIFY(file.open(QIODevice::WriteOnly));
        png[png.size() - 1] = char(png.back() ^ 1);
        file.write(png);
        file.close();
        // This assertion tests CRC parsing, independently of filesystem timestamp
        // granularity. Session invalidation has its own rewrite regression test.
        ix::ApngCodec::clearSession();
        QVERIFY(!registry.read(file.fileName()).ok());
    }
    void psd() {
        if (!QImageReader::supportedImageFormats().contains("psd"))
            QSKIP("Optional PSD plugin not installed in this platform build");
        QByteArray psd = QByteArray::fromHex(
            "38425053000100000000000000030000000100000002000800030000000000000000000000000000ff0000ff0000");
        QFile file(dir(QStringLiteral("fixture.psd")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(psd);
        file.close();
        ix::CodecRegistry registry;
        auto image = registry.read(file.fileName());
        QVERIFY2(image.ok(), qPrintable(image.error));
        QCOMPARE(image.document.pixels.size(), QSize(2, 1));
        QCOMPARE(image.document.pixels.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(image.document.pixels.pixelColor(1, 0), QColor(Qt::green));
        // Exercise the shipped handler's composite-image path beyond one RGB8
        // sample. The independent fixtures contain planar, big-endian channels.
        for (const int depth : {8, 16}) {
            for (const bool rle : {false, true}) {
                QByteArray fixture;
                QDataStream stream(&fixture, QIODevice::WriteOnly);
                stream.writeRawData("8BPS", 4);
                stream << quint16(1);
                stream.writeRawData("\0\0\0\0\0\0", 6);
                stream << quint16(3) << quint32(1) << quint32(2) << quint16(depth) << quint16(3);
                stream << quint32(0) << quint32(0) << quint32(0) << quint16(rle ? 1 : 0);
                const quint16 channels[3][2]{{0x1234, 0xabcd}, {0x4567, 0x789a}, {0xdef0, 0x2345}};
                if (rle)
                    for (int c = 0; c < 3; ++c)
                        stream << quint16(1 + 2 * depth / 8);
                for (const auto &channel : channels) {
                    if (rle)
                        stream << quint8(2 * depth / 8 - 1); // one PackBits literal run
                    for (const auto sample : channel) {
                        if (depth == 16)
                            stream << sample;
                        else
                            stream << quint8(sample >> 8);
                    }
                }
                QFile variant(dir(QStringLiteral("composite-%1-%2.psd").arg(depth).arg(rle)));
                QVERIFY(variant.open(QIODevice::WriteOnly));
                QCOMPARE(variant.write(fixture), fixture.size());
                variant.close();
                const auto decoded = registry.read(variant.fileName());
                QVERIFY2(decoded.ok(), qPrintable(decoded.error));
                QCOMPARE(decoded.document.pixels.size(), QSize(2, 1));
                for (int x = 0; x < 2; ++x) {
                    const auto color = decoded.document.pixels.pixelColor(x, 0).rgba64();
                    const auto expected = [depth](quint16 v) { return depth == 16 ? v : quint16((v >> 8) * 257); };
                    QCOMPARE(color.red(), expected(channels[0][x]));
                    QCOMPARE(color.green(), expected(channels[1][x]));
                    QCOMPARE(color.blue(), expected(channels[2][x]));
                }
                // Incomplete composite data must not produce a successful load.
                QVERIFY(variant.open(QIODevice::WriteOnly));
                QCOMPARE(variant.write(fixture.left(fixture.size() - 2)), fixture.size() - 2);
                variant.close();
                QVERIFY(!registry.read(variant.fileName()).ok());
            }
        }
    }
    void svg() {
        ix::CodecRegistry registry;
        QFile svg(dir(QStringLiteral("fixture.svg")));
        QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='10'><rect width='20' "
                  "height='10' fill='red'/></svg>");
        svg.close();
        auto rendered = registry.read(svg.fileName());
        QVERIFY(rendered.ok());
        QCOMPARE(rendered.document.pixels.size(), QSize(20, 10));
    }
    void batchSafety() {
        QVERIFY(ix::BatchProcessor::outputName(QStringLiteral("photo.jpg"), QStringLiteral("{name}_####"), 12,
                                               "png") == QStringLiteral("photo_0012.png"));
        for (const auto &name : {QStringLiteral("../bad"), QStringLiteral("CON"), QStringLiteral("AUX.txt"),
                                 QStringLiteral("COM1"), QStringLiteral("bad.")})
            QVERIFY(ix::BatchProcessor::outputName(QStringLiteral("photo.jpg"), name, 1, "png").isEmpty());
        ix::ImageDocument d;
        d.pixels = pattern();
        ix::CodecRegistry registry;
        QString input = dir(QStringLiteral("batch/input.png"));
        QDir().mkpath(QFileInfo(input).absolutePath());
        QVERIFY(registry.save(d, input, "png", 95, true).isEmpty());
        ix::BatchOptions o;
        o.journalRoot = dir(QStringLiteral("legacy-journal"));
        o.inputs = {input};
        o.outputDirectory = dir(QStringLiteral("batch/output"));
        o.allowLoss = true;
        std::atomic_bool cancel = false;
        auto first = ix::BatchProcessor::run(o, cancel);
        QCOMPARE(first.size(), 1);
        QVERIFY2(first[0].error.isEmpty(), qPrintable(first[0].error));
        auto second = ix::BatchProcessor::run(o, cancel);
        QVERIFY(!second[0].error.isEmpty());
        cancel = true;
        QVERIFY(ix::BatchProcessor::run(o, cancel).isEmpty());
    }
    void resizeDialogCoupling() {
        ix::ResizeDialog d({400, 200}, 96);
        auto *w = d.findChild<QDoubleSpinBox *>(QStringLiteral("width"));
        auto *h = d.findChild<QDoubleSpinBox *>(QStringLiteral("height"));
        auto *p = d.findChild<QCheckBox *>(QStringLiteral("proportional"));
        QVERIFY(w && h && p);
        w->setValue(100);
        QCOMPARE(h->value(), 50.0);
        p->setChecked(false);
        w->setValue(80);
        QCOMPARE(h->value(), 50.0);
        QCOMPARE(d.options().size, QSize(80, 50));
        auto *filter = d.findChild<QComboBox *>(QStringLiteral("filter"));
        QCOMPARE(filter->count(), 6);
        QCOMPARE(filter->currentIndex(), 5);
        filter->setCurrentIndex(1);
        QCOMPARE(d.options().filter, ix::ResampleFilter::Triangle);
        auto *nearest = d.findChild<QRadioButton *>(QStringLiteral("nearestNeighbor"));
        QVERIFY(nearest);
        nearest->click();
        QCOMPARE(d.options().filter, ix::ResampleFilter::Nearest);
        QVERIFY(!filter->isEnabled());
        QVERIFY(!d.options().sharpen);
        QVERIFY(!d.options().linearLight);
    }
    void menuReference() {
        ix::MainWindow window;
        QFile f(QStringLiteral(":/menus.de.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto menus = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("menus")).toArray();
        for (const auto &m : menus) {
            auto object = m.toObject();
            auto *menu = window.findChild<QMenu *>(object.value(QStringLiteral("id")).toString());
            if (!menu)
                continue;
            QList<int> expected;
            for (auto v : object.value(QStringLiteral("items")).toArray()) {
                auto item = v.toObject();
                if (!item.value(QStringLiteral("command")).toString().isEmpty() &&
                    item.contains(QStringLiteral("originalId")))
                    expected << item.value(QStringLiteral("originalId")).toInt();
            }
            int position = 0;
            for (auto *action : menu->actions()) {
                if (action->isSeparator() || action->menu() || !action->property("referenceId").isValid())
                    continue;
                QVERIFY(position < expected.size());
                if (action->property("referenceId").isValid())
                    QCOMPARE(action->property("referenceId").toInt(), expected[position++]);
            }
            QCOMPARE(position, expected.size());
        }
        auto *resize = window.findChild<QAction *>(QStringLiteral("resize"));
        QVERIFY(resize);
        QCOMPARE(resize->text(), QStringLiteral("Größe ändern..."));
        QCOMPARE(resize->shortcut(), QKeySequence(QStringLiteral("Ctrl+R")));
        QCOMPARE(window.findChild<QAction *>(QStringLiteral("saveAs"))->shortcut(),
                 QKeySequence(QStringLiteral("S")));
    }
    void asyncLoadUndo() {
        ix::ImageDocument d;
        d.pixels = pattern();
        ix::CodecRegistry registry;
        auto path = dir(QStringLiteral("async.png"));
        QVERIFY(registry.save(d, path, "png", 95, true).isEmpty());
        ix::MainWindow window;
        window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        QCOMPARE(window.document().pixels.size(), QSize(64, 48));
        window.dispatch(QStringLiteral("right"));
        QTRY_VERIFY_WITH_TIMEOUT(window.document().modified, 10000);
        QCOMPARE(window.document().pixels.size(), QSize(48, 64));
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().pixels.size(), QSize(64, 48));
        window.dispatch(QStringLiteral("redo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().pixels.size(), QSize(48, 64));
    }
    void fullscreenFitsWholeImageWithoutFrame() {
        QSettings settings;
        const auto previousStrip = settings.value(QStringLiteral("view/previewStrip"));
        const auto previousCursor = settings.value(QStringLiteral("fullscreen/hideCursor"));
        auto restore = qScopeGuard([&] {
            settings.setValue(QStringLiteral("view/previewStrip"), previousStrip);
            settings.setValue(QStringLiteral("fullscreen/hideCursor"), previousCursor);
        });
        settings.setValue(QStringLiteral("view/previewStrip"), true);
        settings.setValue(QStringLiteral("fullscreen/hideCursor"), true);
        const auto path = dir(QStringLiteral("fullscreen.png"));
        QVERIFY(pattern(1600, 1200).save(path));
        ix::MainWindow window;
        window.show();
        window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        auto *view = window.findChild<ix::ImageView *>();
        const auto browsingFitBefore = view->browsingFit();
        view->setZoom(3);
        view->setSelection(QRect(30, 40, 300, 200));
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(window.isFullScreen());
        QCOMPARE(view->frameShape(), QFrame::NoFrame);
        QVERIFY(view->selection().isEmpty());
        QVERIFY(window.findChild<ix::PreviewStrip *>()->isHidden());
        QTRY_COMPARE(view->visibleImageRect(), view->image().rect());
        QTRY_COMPARE(view->viewport()->mapTo(&window, QPoint()), QPoint());
        QTRY_COMPARE(view->viewport()->size(), window.size());
        QCOMPARE(view->viewport()->cursor().shape(), Qt::BlankCursor);
        QVERIFY(window.grab().save(QStringLiteral(IX_TEST_OUTPUT "/fullscreen-borderless.png")));
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(!window.isFullScreen());
        QCOMPARE(view->zoom(), 3.0);
        QCOMPARE(view->browsingFit(), browsingFitBefore);
        QVERIFY(!window.findChild<ix::PreviewStrip *>()->isHidden());
        auto *menu = window.menuBar()->findChild<QMenu *>(QStringLiteral("ansicht"));
        const auto actions = menu->actions();
        const int strip = actions.indexOf(window.findChild<QAction *>(QStringLiteral("previewStripToggle")));
        const int full = actions.indexOf(window.findChild<QAction *>(QStringLiteral("fullscreen")));
        QVERIFY(strip > 0 && strip < full);
        QVERIFY(actions[strip - 1]->isSeparator());
        QVERIFY(actions[strip + 1]->isSeparator());
    }
    void autoCorrectionIgnoresIsolatedExtremes() {
        QImage source(200, 100, QImage::Format_RGBA64);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x) {
                int v = 90 + x % 61;
                source.setPixelColor(x, y, QColor(v, v, v, 128));
            }
        source.setPixelColor(0, 0, Qt::black);
        source.setPixelColor(1, 0, Qt::white);
        source.setPixelColor(2, 0, QColor(123, 75, 13, 0));
        auto result = ix::autoCorrect(source);
        QCOMPARE(result.format(), QImage::Format_RGBA64);
        QVERIFY(result.pixelColor(0, 1).red() < 10);
        QVERIFY(result.pixelColor(60, 1).red() > 245);
        QCOMPARE(result.pixelColor(2, 0), source.pixelColor(2, 0));
        QCOMPARE(result.pixelColor(25, 1).alpha(), source.pixelColor(25, 1).alpha());
    }
    void languageSelectionAndTranslatedWindows() {
        auto restore = qScopeGuard([] { ix::setUiLanguage(QStringLiteral("de")); });
        const QStringList codes{QStringLiteral("de"), QStringLiteral("en"), QStringLiteral("ko"),
                                QStringLiteral("ja"), QStringLiteral("fr")};
        const QStringList titles{QStringLiteral("Einstellungen"), QStringLiteral("Settings"),
                                 QStringLiteral("설정"), QStringLiteral("設定"),
                                 QStringLiteral("Paramètres")};
        for (int i = 0; i < codes.size(); ++i) {
            QVERIFY(ix::setUiLanguage(codes[i]));
            QCOMPARE(ix::uiLanguage(), codes[i]);
            ix::SettingsDialog settings;
            QCOMPARE(settings.windowTitle(), titles[i]);
            auto *language = settings.findChild<QComboBox *>(QStringLiteral("ui/language"));
            QVERIFY(language);
            QCOMPARE(language->count(), 5);
            settings.show();
            QCoreApplication::processEvents();
            QVERIFY(settings.grab().save(QStringLiteral(IX_TEST_OUTPUT "/language-%1.png").arg(codes[i])));
            ix::MainWindow window;
            QVERIFY(window.menuBar()->findChild<QMenu *>(QStringLiteral("datei")));
            QCOMPARE(window.findChild<QAction *>(QStringLiteral("resize"))->text(),
                     ix::localized(QStringLiteral("Größe ändern...")));
            QCOMPARE(window.findChild<QAction *>(QStringLiteral("resize"))->shortcut(),
                     QKeySequence(QStringLiteral("Ctrl+R")));
            QCOMPARE(window.findChild<QAction *>(QStringLiteral("fullscreen"))->shortcut(),
                     QKeySequence(Qt::Key_Return));
            QCOMPARE(ix::localized(QStringLiteral("view/fit")), QStringLiteral("view/fit"));
        }
        QVERIFY(!ix::setUiLanguage(QStringLiteral("unknown")));
    }
    void screenshotShortcutAndNewPreferencesPersist() {
        QSettings stored;
        QVariantMap previous;
        for (const auto &key : stored.allKeys())
            previous.insert(key, stored.value(key));
        auto restore = qScopeGuard([previous] {
            QSettings settings;
            settings.clear();
            for (auto it = previous.begin(); it != previous.end(); ++it)
                settings.setValue(it.key(), it.value());
        });
        {
            ix::SettingsDialog dialog;
            dialog.findChild<QComboBox *>(QStringLiteral("ui/language"))->setCurrentIndex(3);
            dialog.findChild<QKeySequenceEdit *>(QStringLiteral("capture/shortcut"))
                ->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+F10")));
            dialog.findChild<QSpinBox *>(QStringLiteral("capture/delayMs"))->setValue(1250);
            dialog.findChild<QCheckBox *>(QStringLiteral("slideshow/random"))->setChecked(true);
            dialog.findChild<QCheckBox *>(QStringLiteral("slideshow/repeat"))->setChecked(false);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        }
        QCOMPARE(stored.value(QStringLiteral("ui/language")).toString(), QStringLiteral("ja"));
        QCOMPARE(stored.value(QStringLiteral("capture/delayMs")).toInt(), 1250);
        QVERIFY(stored.value(QStringLiteral("slideshow/random")).toBool());
        QVERIFY(!stored.value(QStringLiteral("slideshow/repeat")).toBool());
        stored.setValue(QStringLiteral("capture/globalShortcut"), false);
        ix::MainWindow window;
        QCOMPARE(window.findChild<QAction *>(QStringLiteral("capture"))->shortcut(),
                 QKeySequence(QStringLiteral("Ctrl+Alt+F10")));
    }
    void randomSlideshowWithoutRepeatVisitsEachImageOnce() {
        QSettings stored;
        const QStringList keys{QStringLiteral("slideshow/seconds"), QStringLiteral("slideshow/random"),
                               QStringLiteral("slideshow/repeat"), QStringLiteral("slideshow/fullscreen")};
        QVariantMap old;
        for (const auto &key : keys)
            old.insert(key, stored.value(key));
        auto restore = qScopeGuard([old] {
            QSettings s;
            for (auto it = old.begin(); it != old.end(); ++it)
                s.setValue(it.key(), it.value());
        });
        stored.setValue(QStringLiteral("slideshow/seconds"), .1);
        stored.setValue(QStringLiteral("slideshow/random"), true);
        stored.setValue(QStringLiteral("slideshow/repeat"), false);
        stored.setValue(QStringLiteral("slideshow/fullscreen"), false);
        const auto folder = dir(QStringLiteral("random-slideshow"));
        QVERIFY(QDir().mkpath(folder));
        for (int i = 0; i < 3; ++i)
            QVERIFY(pattern().save(folder + QStringLiteral("/%1.png").arg(i)));
        ix::MainWindow window;
        window.openPath(folder + QStringLiteral("/0.png"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        QStringList shown{window.document().path};
        auto *view = window.findChild<ix::ImageView *>();
        connect(view, &ix::ImageView::selectionChanged, &window,
                [&] { shown.append(window.document().path); });
        window.dispatch(QStringLiteral("slideshow"));
        QTRY_COMPARE_WITH_TIMEOUT(shown.size(), 3, 5000);
        QTest::qWait(400);
        QCOMPARE(shown.size(), 3);
        QCOMPARE(QSet<QString>(shown.begin(), shown.end()).size(), 3);
    }
    void saveAsUndoKeepsNewDestination() {
        const auto source = dir(QStringLiteral("save-as-source.png"));
        const auto target = dir(QStringLiteral("save-as-target.png"));
        QVERIFY(pattern().save(source));
        auto originalHash = ix::sha256File(source);
        ix::MainWindow window;
        window.openPath(source);
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        window.dispatch(QStringLiteral("right"));
        QTRY_VERIFY_WITH_TIMEOUT(window.document().modified, 10000);
        const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        auto restore =
            qScopeGuard([native] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native); });
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible()) {
                    dialog->selectFile(target);
                    QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
                }
        });
        timer.start(20);
        window.dispatch(QStringLiteral("saveAs"));
        timer.stop();
        QCOMPARE(window.document().path, target);
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().path, target);
        QCOMPARE(window.document().pixels.size(), QSize(64, 48));
        QCOMPARE(ix::sha256File(source), originalHash);
    }
    void batchPlanGuardsContentInAdditionToTimestamps() {
        const auto source = dir(QStringLiteral("hash-input.bmp"));
        const auto target = dir(QStringLiteral("hash-output.bmp"));
        const auto stage = dir(QStringLiteral("hash-stage.bmp"));
        QVERIFY(pattern().save(source));
        QVERIFY(pattern().save(target));
        QVERIFY(pattern().save(stage));
        auto hash = ix::sha256File(target);
        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.seek(file.size() - 1));
        QCOMPARE(file.write("X", 1), 1);
        file.close();
        ix::FileJournal journal(dir(QStringLiteral("hash-journal")));
        // Supply the current stamp to independently exercise the content guard.
        auto error = journal.installOutput(stage, target, true, ix::imageFileKey(target), hash);
        QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo::exists(stage));
        QVERIFY(ix::sha256File(target) != hash);
    }
    void cacheInvalidatesSameSizeRewriteWithPreservedMtime() {
        const auto path = dir(QStringLiteral("cache-rewrite.bmp"));
        auto pixels = pattern();
        QVERIFY(pixels.save(path));
        ix::CodecRegistry registry;
        ix::ImageLoadCache::clear();
        auto before = ix::ImageLoadCache::read(registry, path);
        QVERIFY(before.ok());
        const auto size = QFileInfo(path).size();
        const auto time = QFileInfo(path).lastModified();
        QTest::qWait(10);
        pixels.fill(Qt::blue);
        QVERIFY(pixels.save(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(time, QFileDevice::FileModificationTime));
        file.close();
        QCOMPARE(QFileInfo(path).size(), size);
        QCOMPARE(QFileInfo(path).lastModified(), time);
        auto after = ix::ImageLoadCache::read(registry, path);
        QVERIFY(after.ok());
        QCOMPARE(after.document.pixels.pixelColor(0, 0), QColor(Qt::blue));
    }
    void cacheRejectsFilesChangedDuringDecoding() {
        class ChangingCodec final : public ix::ImageCodec {
          public:
            mutable int reads = 0;
            bool accepts(const QString &) const override { return true; }
            QList<ix::FormatCapability> capabilities() const override { return {}; }
            ix::ImageResult read(const QString &path, int) const override {
                ++reads;
                ix::ImageDocument document;
                document.pixels = pattern(); document.path = path; document.format = "bmp";
                if (reads == 1) {
                    QFile file(path);
                    if (file.open(QIODevice::Append)) file.write("changed");
                }
                return {document, {}};
            }
        };
        const auto path = dir(QStringLiteral("changing-during-read.bmp"));
        QVERIFY(pattern().save(path));
        ix::ImageLoadCache::clear();
        ix::CodecRegistry registry;
        auto codec = std::make_shared<ChangingCodec>(); registry.add(codec);
        const auto changed = ix::ImageLoadCache::read(registry, path);
        QVERIFY(!changed.ok()); QVERIFY(!changed.error.isEmpty());
        const auto stable = ix::ImageLoadCache::read(registry, path);
        QVERIFY(stable.ok()); QCOMPARE(codec->reads, 2);
        QCOMPARE(stable.document.sourceRevision, ix::imageFileKey(path));
        QVERIFY(ix::ImageLoadCache::read(registry, path).ok());
        QCOMPARE(codec->reads, 2);
        ix::ImageLoadCache::clear();
    }
    void selectionExportRemovesOriginalExifThumbnail() {
#ifdef IX_TEST_EXIV2
        const auto source = dir(QStringLiteral("thumbnail-source.jpg"));
        const auto target = dir(QStringLiteral("thumbnail-crop.jpg"));
        auto pixels = pattern(100, 80);
        QVERIFY(pixels.save(source));
        QByteArray thumbnail;
        QBuffer buffer(&thumbnail);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(pixels.scaled(50, 40).save(&buffer, "JPEG"));
        auto metadata = Exiv2::ImageFactory::open(source.toStdString());
        metadata->readMetadata();
        Exiv2::ExifThumb(metadata->exifData())
            .setJpegThumbnail(reinterpret_cast<const Exiv2::byte *>(thumbnail.constData()), thumbnail.size());
        metadata->writeMetadata();
        ix::MainWindow window;
        window.openPath(source);
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        window.findChild<ix::ImageView *>()->setSelection(QRect(0, 0, 20, 20));
        const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        auto restore =
            qScopeGuard([native] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native); });
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible()) {
                    dialog->selectFile(target);
                    QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
                }
        });
        timer.start(20);
        window.dispatch(QStringLiteral("saveSelection"));
        timer.stop();
        QCOMPARE(QImage(target).size(), QSize(20, 20));
        auto result = Exiv2::ImageFactory::open(target.toStdString());
        result->readMetadata();
        QCOMPARE(Exiv2::ExifThumb(result->exifData()).copy().size(), size_t(0));
#endif
    }

    void customCropRatiosSettingsValidateAndPersist() {
        QSettings settings;
        const auto saved = settings.value(QStringLiteral("crop/customRatios"));
        auto restore = qScopeGuard([saved] { QSettings().setValue(QStringLiteral("crop/customRatios"), saved); });
        settings.remove(QStringLiteral("crop/customRatios"));
        QStringList parsed;
        QVERIFY(ix::parseCropRatios(QStringLiteral("5:4\n2:3\n10:8\n\n239:100"), parsed));
        QCOMPARE(parsed, QStringList({QStringLiteral("5:4"), QStringLiteral("2:3"), QStringLiteral("239:100")}));
        for (const auto &invalid : {QStringLiteral("0:4"), QStringLiteral("1:-1"), QStringLiteral("nan:4"),
                                    QStringLiteral("1.5:1"), QStringLiteral("10001:1"), QStringLiteral("4"),
                                    QString(5000, QLatin1Char('1'))})
            QVERIFY(!ix::parseCropRatios(invalid, parsed));
        QStringList tooMany;
        for (int i = 1; i <= 33; ++i)
            tooMany.append(QStringLiteral("%1:1").arg(i));
        QVERIFY(!ix::parseCropRatios(tooMany.join(QLatin1Char('\n')), parsed));
        const auto setRatio = [](QTableWidget *table, int row, int width, int height) {
            qobject_cast<QSpinBox *>(table->cellWidget(row, 0))->setValue(width);
            qobject_cast<QSpinBox *>(table->cellWidget(row, 1))->setValue(height);
        };
        {
            ix::SettingsDialog dialog;
            dialog.findChild<QPushButton *>(QStringLiteral("addCropRatio"))->click();
            auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("crop/customRatios"));
            QCOMPARE(table->rowCount(), 1);
            setRatio(table, 0, 5, 4);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            QVERIFY(!settings.contains(QStringLiteral("crop/customRatios")));
        }
        {
            ix::SettingsDialog dialog;
            dialog.show();
            dialog.selectPage(QStringLiteral("cropSettingsPage"));
            auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("crop/customRatios"));
            auto *add = dialog.findChild<QPushButton *>(QStringLiteral("addCropRatio"));
            auto *remove = dialog.findChild<QPushButton *>(QStringLiteral("removeCropRatio"));
            QVERIFY(table && add && remove);
            QVERIFY(!remove->isEnabled());
            for (int i = 0; i < 32; ++i) add->click();
            QCOMPARE(table->rowCount(), 32);
            QVERIFY(!add->isEnabled());
            table->setCurrentCell(0, 0);
            remove->click();
            QCOMPARE(table->rowCount(), 31);
            QVERIFY(add->isEnabled());
            while (table->rowCount() > 3) { table->setCurrentCell(table->rowCount() - 1, 0); remove->click(); }
            setRatio(table, 0, 5, 4);
            setRatio(table, 1, 2, 3);
            setRatio(table, 2, 10, 8);
            auto *width = qobject_cast<QSpinBox *>(table->cellWidget(0, 0));
            QCOMPARE(width->minimum(), 1);
            QCOMPARE(width->maximum(), 10000);
            // Editing a numeric cell selects its row for removal; no syntax helper is needed.
            table->scrollToItem(table->item(0, 0));
            QCoreApplication::processEvents();
            QTest::mouseClick(width, Qt::LeftButton, Qt::NoModifier, width->rect().center());
            QCOMPARE(table->currentRow(), 0);
            QVERIFY(dialog.grab().save(QStringLiteral(IX_TEST_OUTPUT "/settings-crop-ratios.png")));
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        QCOMPARE(settings.value(QStringLiteral("crop/customRatios")).toStringList(),
                 QStringList({QStringLiteral("5:4"), QStringLiteral("2:3")}));
        ix::SettingsDialog reopened;
        auto *table = reopened.findChild<QTableWidget *>(QStringLiteral("crop/customRatios"));
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(qobject_cast<QSpinBox *>(table->cellWidget(0, 0))->value(), 5);
        QCOMPARE(qobject_cast<QSpinBox *>(table->cellWidget(1, 1))->value(), 3);
    }

    void aspectRatioSelectionDragResizeAndMove() {
        ix::ImageView view;
        view.resize(600, 450);
        view.setCentered(false);
        view.setImage(pattern(400, 300));
        view.setZoom(1);
        view.show();
        QTest::qWait(20);
        auto *surface = view.viewport();
        auto correct = [&](QSize ratio) {
            const auto r = view.selection();
            return !r.isEmpty() && view.image().rect().contains(r) &&
                   qAbs(qint64(r.width()) * ratio.height() - qint64(r.height()) * ratio.width()) <=
                       std::max(ratio.width(), ratio.height());
        };
        auto ratios = ix::standardCropRatios();
        ratios.append({5, 4});
        ratios.append({239, 100});
        for (const auto &ratio : ratios) {
            view.setSelection({});
            view.setSelectionAspectRatio(ratio);
            QVERIFY(correct(ratio));
            const auto inside = view.selection().center();
            QTest::mousePress(surface, Qt::LeftButton, Qt::NoModifier, inside);
            QTest::mouseMove(surface, inside + QPoint(20, 20));
            QTest::mouseRelease(surface, Qt::LeftButton, Qt::NoModifier, inside + QPoint(20, 20));
            QCOMPARE(view.zoom(), 1.0);
            QVERIFY(correct(ratio));
            const QList<QPoint> ends{{380, 270}, {20, 270}, {380, 20}, {20, 20}, {590, 420}, {0, 0}};
            for (const auto &end : ends) {
                view.setSelection({});
                QTest::mousePress(surface, Qt::LeftButton, Qt::NoModifier, {180, 130});
                QTest::mouseMove(surface, end);
                QTest::mouseRelease(surface, Qt::LeftButton, Qt::NoModifier, end);
                QVERIFY(correct(ratio));
            }
            view.setSelection({20, 20, 80, 60});
            view.setSelectionAspectRatio(ratio);
            const auto before = view.selection();
            const auto center = before.center();
            QTest::mousePress(surface, Qt::RightButton, Qt::NoModifier, center);
            QTest::mouseMove(surface, center + QPoint(30, 40));
            QTest::mouseRelease(surface, Qt::RightButton, Qt::NoModifier, center + QPoint(30, 40));
            QCOMPARE(view.selection().size(), before.size());
            QCOMPARE(view.selection().topLeft(), before.topLeft() + QPoint(30, 40));
            QTest::keyClick(&view, Qt::Key_Right, Qt::ControlModifier);
            QVERIFY(correct(ratio));
            QTest::keyClick(&view, Qt::Key_Down, Qt::AltModifier);
            QVERIFY(correct(ratio));
        }
        view.setSelection({});
        view.setSelectionAspectRatio({10000, 1});
        QCOMPARE(view.selection().height(), 1);
        QVERIFY(view.image().rect().contains(view.selection()));
        view.setSelectionAspectRatio({});
        view.setSelection({});
        QTest::mousePress(surface, Qt::LeftButton, Qt::NoModifier, {30, 40});
        QTest::mouseMove(surface, {130, 100});
        QTest::mouseRelease(surface, Qt::LeftButton, Qt::NoModifier, {130, 100});
        QCOMPARE(view.selection().size(), QSize(101, 61));
    }
    void aspectRatioCropMenuAppliesAndUndoes() {
        QSettings settings;
        const auto saved = settings.value(QStringLiteral("crop/customRatios"));
        auto restore = qScopeGuard([saved] { QSettings().setValue(QStringLiteral("crop/customRatios"), saved); });
        settings.setValue(QStringLiteral("crop/customRatios"),
                          QStringList({QStringLiteral("5:4"), QStringLiteral("2:3"), QStringLiteral("32:18")}));
        auto image = pattern(400, 300);
        const auto path = dir(QStringLiteral("aspect-crop.png"));
        QVERIFY(image.save(path));
        const auto originalHash = ix::sha256File(path);
        ix::MainWindow window;
        window.show();
        window.openPath(path);
        QTRY_COMPARE(window.document().pixels.size(), image.size());
        auto *view = window.findChild<ix::ImageView *>();
        auto *menu = window.findChild<QMenu *>(QStringLiteral("cropAspectMenu"));
        QVERIFY(menu);
        auto find = [&](const QString &label) -> QAction * {
            for (auto *action : menu->actions())
                if (action->text() == label)
                    return action;
            return nullptr;
        };
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
        for (const auto &ratio : ix::standardCropRatios())
            QVERIFY(find(ix::cropRatioLabel(ratio)));
        QVERIFY(find(QStringLiteral("5:4")));
        QVERIFY(find(QStringLiteral("2:3")));
        QVERIFY(!find(QStringLiteral("32:18")));
        find(QStringLiteral("16:9"))->trigger();
        QCOMPARE(view->selection(), QRect(0, 37, 400, 225));
        QVERIFY(window.grab().save(QStringLiteral(IX_TEST_OUTPUT "/aspect-crop.png")));
        const auto selected = view->selection();
        window.findChild<QAction *>(QStringLiteral("crop"))->trigger();
        QTRY_VERIFY(window.document().modified);
        QCOMPARE(window.document().pixels, image.copy(selected));
        QCOMPARE(window.document().pixels.colorSpace(), image.colorSpace());
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().pixels, image);
        QCOMPARE(view->selection(), selected);
        QCOMPARE(ix::sha256File(path), originalHash);
        settings.setValue(QStringLiteral("crop/customRatios"), QStringList{QStringLiteral("7:5")});
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
        QVERIFY(find(QStringLiteral("7:5")));
        QVERIFY(!find(QStringLiteral("5:4")));
        menu->actions().first()->trigger();
        QVERIFY(!view->selectionAspectRatio().isValid());
        window.close();
    }
    void fullscreenMouseNavigationAndDrags() {
        const auto folder = dir(QStringLiteral("fullscreen-clicks"));
        QVERIFY(QDir().mkpath(folder));
        QStringList paths;
        for (int i = 0; i < 3; ++i) {
            paths.append(folder + QStringLiteral("/%1.png").arg(i));
            QVERIFY(pattern(400, 300).save(paths.last()));
        }
        ix::MainWindow window;
        window.show();
        window.openPath(paths[1]);
        QTRY_COMPARE(window.document().path, paths[1]);
        QTest::qWait(150);
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(window.isFullScreen());
        auto *view = window.findChild<ix::ImageView *>();
        auto *surface = view->viewport();
        QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, surface->rect().center());
        QTRY_COMPARE(window.document().path, paths[0]);
        QTest::mouseClick(surface, Qt::RightButton, Qt::NoModifier, surface->rect().center());
        QTRY_COMPARE(window.document().path, paths[1]);
        view->setSelection({30, 30, 100, 80});
        QTest::mouseClick(surface, Qt::RightButton, Qt::NoModifier, surface->rect().center());
        QTRY_COMPARE(window.document().path, paths[2]);
        const auto imageOrigin = surface->rect().center() - QPoint(200, 150);
        const auto start = imageOrigin + QPoint(40, 40), end = imageOrigin + QPoint(140, 110);
        QTest::mousePress(surface, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(surface, end);
        QTest::mouseRelease(surface, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(!view->selection().isEmpty());
        QCOMPARE(window.document().path, paths[2]);
        const auto before = view->selection();
        const auto center = imageOrigin + before.center();
        QTest::mousePress(surface, Qt::RightButton, Qt::NoModifier, center);
        QTest::mouseMove(surface, center + QPoint(20, 20));
        QTest::mouseRelease(surface, Qt::RightButton, Qt::NoModifier, center + QPoint(20, 20));
        QCOMPARE(view->selection().topLeft(), before.topLeft() + QPoint(20, 20));
        QCOMPARE(window.document().path, paths[2]);
        QTest::mouseClick(surface, Qt::LeftButton, Qt::ControlModifier, imageOrigin + view->selection().center());
        QVERIFY(view->selection().isEmpty());
        QCOMPARE(window.document().path, paths[2]);
        // Background clicks navigate as well. Leaving fullscreen restores normal mouse editing.
        QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, {1, 1});
        QTRY_COMPARE(window.document().path, paths[1]);
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(!window.isFullScreen());
        bool menuOpened = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                menuOpened = menu->objectName() == QStringLiteral("imageRecognitionMenu");
                menu->close();
            }
        });
        QTest::mouseClick(surface, Qt::RightButton, Qt::NoModifier, surface->rect().center());
        QVERIFY(menuOpened);
        QTest::qWait(50);
        QCOMPARE(window.document().path, paths[1]);
        QTest::mouseDClick(surface, Qt::LeftButton, Qt::NoModifier, surface->rect().center());
        QTRY_VERIFY(window.isFullScreen());
        QCOMPARE(window.document().path, paths[1]);
        window.dispatch(QStringLiteral("fullscreen"));
        window.close();
    }
    void selectionDamageCoversEveryChangedPixel() {
        for (const auto format : {QImage::Format_ARGB32_Premultiplied, QImage::Format_RGBA64}) {
            ix::ImageView view;
            view.resize(600, 450);
            auto source = pattern(1800, 1400).convertToFormat(format);
            view.setImage(source);
            view.grid = true;
            view.show();
            PaintRegionProbe probe;
            view.viewport()->installEventFilter(&probe);
            for (double zoom : {.29, .73, 1.3}) {
                view.setZoom(zoom);
                view.horizontalScrollBar()->setValue(50);
                view.verticalScrollBar()->setValue(30);
                QCoreApplication::processEvents();
                const QList<QRect> selections{{150, 100, 800, 600}, {160, 110, 810, 610},
                                               {0, 0, 1, 1}, source.rect(), {}, {-30, -50, 120, 100}};
                for (const auto &selection : selections) {
                    const auto before = view.viewport()->grab().toImage();
                    probe.painted = {};
                    view.setSelection(selection);
                    QCoreApplication::processEvents();
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
                    const auto damaged = probe.painted;
                    const auto after = view.viewport()->grab().toImage();
                    QCOMPARE(before.size(), after.size());
                    const auto dpr = before.devicePixelRatio();
                    for (int y = 0; y < before.height(); ++y)
                        for (int x = 0; x < before.width(); ++x)
                            if (before.pixel(x, y) != after.pixel(x, y))
                                QVERIFY2(damaged.contains(QPoint(int(x / dpr), int(y / dpr))),
                                         qPrintable(QStringLiteral("Missed changed pixel %1,%2 at zoom %3")
                                                        .arg(x).arg(y).arg(zoom)));
                    qint64 area = 0;
                    for (const auto &rect : damaged)
                        area += qint64(rect.width()) * rect.height();
                    QVERIFY(area < qint64(view.viewport()->width()) * view.viewport()->height() / 2);
                    QSignalSpy changes(&view, &ix::ImageView::selectionChanged);
                    view.setSelection(view.selection());
                    QCOMPARE(changes.count(), 0);
                }
            }
            QCOMPARE(view.image(), source);
        }
    }
    void selectionMagnifierMouseFlow() {
        ix::ImageView view;
        view.resize(800, 600);
        view.setCentered(false);
        auto image = pattern(600, 400);
        view.setImage(image);
        view.setZoom(1);
        view.show();
        QTest::qWait(20);
        auto *surface = view.viewport();
        QTest::mousePress(surface, Qt::LeftButton, Qt::NoModifier, {80, 60});
        QTest::mouseMove(surface, {200, 160});
        QTest::mouseRelease(surface, Qt::LeftButton, Qt::NoModifier, {200, 160});
        QCOMPARE(view.selection(), QRect(80, 60, 121, 101));
        QVERIFY(!surface->cursor().pixmap().isNull());
        auto marked = view.selection();
        QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, {130, 100});
        QVERIFY(view.zoom() > 4);
        QVERIFY(view.selection().isEmpty());
        QVERIFY(view.visibleImageRect().contains(marked));
        QVERIFY((view.visibleImageRect().center() - marked.center()).manhattanLength() <= 3);
        QCOMPARE(view.image(), image);
        QVERIFY(view.screenshot().save(QStringLiteral(IX_TEST_OUTPUT "/selection-zoom.png")));
    }
    void mouseSelectionClearMoveAndPan() {
        ix::ImageView view;
        view.resize(600, 450);
        view.setCentered(false);
        view.setImage(pattern(400, 300));
        view.setZoom(1);
        view.show();
        QTest::qWait(20);
        auto *surface = view.viewport();
        view.setSelection({30, 40, 90, 70});
        QTest::mousePress(surface, Qt::RightButton, Qt::NoModifier, {40, 50});
        QTest::mouseMove(surface, {60, 80});
        QTest::mouseRelease(surface, Qt::RightButton, Qt::NoModifier, {60, 80});
        QCOMPARE(view.selection(), QRect(50, 70, 90, 70));
        QTest::mouseClick(surface, Qt::LeftButton, Qt::ControlModifier, {60, 80});
        QVERIFY(view.selection().isEmpty());
        QCOMPARE(view.zoom(), 1.0);
        QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, {20, 20});
        QVERIFY(view.selection().isEmpty());
        view.setZoom(3);
        view.horizontalScrollBar()->setValue(200);
        view.verticalScrollBar()->setValue(200);
        QTest::mousePress(surface, Qt::RightButton, Qt::NoModifier, {50, 50});
        QTest::mouseMove(surface, {80, 90});
        QTest::mouseRelease(surface, Qt::RightButton, Qt::NoModifier, {80, 90});
        QCOMPARE(view.horizontalScrollBar()->value(), 170);
        QCOMPARE(view.verticalScrollBar()->value(), 160);
        QSignalSpy contextMenus(&view, &ix::ImageView::recognitionMenuRequested);
        QTest::mouseClick(surface, Qt::RightButton, Qt::NoModifier, {80, 90});
        QCOMPARE(contextMenus.size(), 1);
        QContextMenuEvent nativeMouse(QContextMenuEvent::Mouse, {80, 90}, surface->mapToGlobal(QPoint(80, 90)));
        QCoreApplication::sendEvent(surface, &nativeMouse);
        QCOMPARE(contextMenus.size(), 1);
        QContextMenuEvent keyboardMenu(QContextMenuEvent::Keyboard, surface->rect().center(), surface->mapToGlobal(surface->rect().center()));
        QCoreApplication::sendEvent(surface, &keyboardMenu);
        QCOMPARE(contextMenus.size(), 2);
    }
    void zoomKeepsMousePoint() {
        ix::ImageView view;
        view.resize(600, 450);
        view.setCentered(false);
        view.setImage(pattern(1200, 900));
        view.setZoom(1);
        view.show();
        QTest::qWait(20);
        view.horizontalScrollBar()->setValue(200);
        view.verticalScrollBar()->setValue(180);
        QPointF cursor(170, 140);
        QPointF pixel((cursor.x() + 200) / view.zoom(), (cursor.y() + 180) / view.zoom());
        view.zoomAt(2, cursor);
        QVERIFY(std::abs((cursor.x() + view.horizontalScrollBar()->value()) / view.zoom() - pixel.x()) < .6);
        QVERIFY(std::abs((cursor.y() + view.verticalScrollBar()->value()) / view.zoom() - pixel.y()) < .6);
    }
    void arrowSelectionAndScrollContexts() {
        ix::ImageView view;
        view.resize(600, 450);
        view.setCentered(false);
        view.setImage(pattern(400, 300));
        view.setZoom(1);
        view.show();
        view.setFocus();
        QSignalSpy navigate(&view, &ix::ImageView::navigate);
        view.setSelection({30, 40, 100, 50});
        QTest::keyClick(&view, Qt::Key_Right);
        QCOMPARE(view.selection(), QRect(31, 40, 100, 50));
        QCOMPARE(navigate.size(), 0);
        QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(view.selection(), QRect(31, 41, 100, 50));
        QTest::keyClick(&view, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(view.selection().size(), QSize(101, 51));
        QTest::keyClick(&view, Qt::Key_Left, Qt::ControlModifier);
        QCOMPARE(view.selection().size(), QSize(100, 50));
        QTest::keyClick(&view, Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(view.selection().size(), QSize(100, 51));
        view.setZoom(3);
        view.horizontalScrollBar()->setValue(100);
        QTest::keyClick(&view, Qt::Key_Right);
        QCOMPARE(view.horizontalScrollBar()->value(), 125);
        QCOMPARE(navigate.size(), 0);
        auto mark = view.selection();
        QTest::keyClick(&view, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(view.selection().x(), mark.x() - 1);
        view.setSelection({});
        view.setFit(ix::ImageView::Fit::LargeOnly);
        QTest::keyClick(&view, Qt::Key_Right);
        QCOMPARE(navigate.size(), 1);
        QCOMPARE(navigate.takeFirst()[0].toInt(), 1);
    }
    void navigationSurvivesAnimationPageLoading() {
        const auto folder = dir(QStringLiteral("navigation-animation"));
        QVERIFY(QDir().mkpath(folder));
        const auto animation = folder + QStringLiteral("/a.png");
        const auto next = folder + QStringLiteral("/b.png");
        QFile file(animation);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto fixture = ix::test::apngFixture(2, {128, 128}, 1000);
        QCOMPARE(file.write(fixture), fixture.size()); file.close();
        QVERIFY(pattern().save(next));
        ix::MainWindow window; window.show();
        auto *view = window.findChild<ix::ImageView *>();
        for (bool toolbar : {false, true}) {
            window.openPath(animation);
            QTRY_COMPARE_WITH_TIMEOUT(window.document().path, animation, 10000);
            QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
            window.openPath(animation, 1, false);
            QVERIFY(window.isBusy());
            if (toolbar) window.dispatch(QStringLiteral("next"));
            else view->navigate(1);
            QTRY_COMPARE_WITH_TIMEOUT(window.document().path, next, 10000);
            QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        }
    }
    void keyboardFolderNavigation() {
        QStringList files;
        QDir().mkpath(dir(QStringLiteral("navigation")));
        ix::CodecRegistry registry;
        for (int i = 0; i < 8; ++i) {
            ix::ImageDocument d;
            d.pixels = pattern(64 + i, 48);
            auto path = dir(QStringLiteral("navigation/image%1.png").arg(i));
            QVERIFY(registry.save(d, path, "png", 95, true).isEmpty());
            files << path;
        }
        ix::MainWindow window;
        window.show();
        window.activateWindow();
        window.openPath(files[0]);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[0], 10000);
        auto *view = window.findChild<ix::ImageView *>();
        view->setFit(ix::ImageView::Fit::LargeOnly);
        view->setFocus();
        QTest::keyClick(view, Qt::Key_Right);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[1], 10000);
        QTest::keyClick(view, Qt::Key_Left);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[0], 10000);
        QTest::keyClick(view, Qt::Key_PageDown);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[1], 10000);
        QTest::keyClick(view, Qt::Key_PageUp);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[0], 10000);
        QTest::keyClick(view, Qt::Key_Right, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[5], 10000);
        QTest::keyClick(view, Qt::Key_Home, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[0], 10000);
        // Repeated keys while asynchronous loading retain their requested steps.
        QTest::keyClick(view, Qt::Key_Right);
        QTest::keyClick(view, Qt::Key_Right);
        QTest::keyClick(view, Qt::Key_Right);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[3], 10000);
        QTest::keyClick(view, Qt::Key_End, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[7], 10000);
        QTest::keyClick(view, Qt::Key_Space);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[0], 10000);
        QTest::keyClick(view, Qt::Key_Backspace);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[7], 10000);
        QTest::keyClick(view, Qt::Key_3, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, files[4], 10000);
    }
    void keyboardZoomAndEditingFocus() {
        ix::ImageDocument d;
        d.pixels = pattern(640, 480);
        ix::CodecRegistry registry;
        auto path = dir(QStringLiteral("keyboard-zoom.png"));
        QVERIFY(registry.save(d, path, "png", 95, true).isEmpty());
        ix::MainWindow window;
        window.show();
        window.activateWindow();
        window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        auto *view = window.findChild<ix::ImageView *>();
        view->setFocus();
        QTest::keyClick(view, Qt::Key_H, Qt::ControlModifier);
        QCOMPARE(view->zoom(), 1.0);
        QTest::keyClick(view, Qt::Key_Equal);
        QCOMPARE(view->zoom(), 1.25);
        QTest::keyClick(view, Qt::Key_Minus);
        QCOMPARE(view->zoom(), 1.0);
        QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(view->selection(), d.pixels.rect());
        QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
        QVERIFY(view->selection().isEmpty());
        QTest::keyClick(view, Qt::Key_P, Qt::ShiftModifier);
        QCOMPARE(QApplication::clipboard()->text(), path);
        QTest::keyClick(view, Qt::Key_T, Qt::ShiftModifier);
        auto *zoom = window.findChild<QComboBox *>();
        QVERIFY(zoom);
        QVERIFY(zoom->hasFocus() || zoom->lineEdit()->hasFocus());
        QTest::keyClicks(zoom->lineEdit(), QStringLiteral("150"));
        QTest::keyClick(zoom->lineEdit(), Qt::Key_Return);
        QCOMPARE(view->zoom(), 1.5);
        QVERIFY(!window.isFullScreen());
        QVERIFY(view->hasFocus());
        QTest::keyClick(view, Qt::Key_T, Qt::ShiftModifier);
        QTest::keyClicks(zoom->lineEdit(), QStringLiteral("200"));
        view->setFocus(); // Accept a completed preset by leaving the editor.
        QCOMPARE(view->zoom(), 2.0);
        QTest::keyClick(view, Qt::Key_T, Qt::ShiftModifier);
        QTest::keyClicks(zoom->lineEdit(), QStringLiteral("150"));
        QTest::keyClick(zoom->lineEdit(), Qt::Key_Return);
        QCOMPARE(view->zoom(), 1.5);
        QTest::keyClick(view, Qt::Key_T, Qt::ShiftModifier);
        QTest::keyClicks(zoom->lineEdit(), QStringLiteral("900"));
        QTest::keyClick(zoom->lineEdit(), Qt::Key_Escape);
        QVERIFY(window.isVisible());
        QCOMPARE(view->zoom(), 1.5);
        QTest::keyClick(view, Qt::Key_Return);
        QTRY_VERIFY(window.isFullScreen());
        QTest::keyClick(view, Qt::Key_Escape);
        QTRY_VERIFY(!window.isFullScreen());
        QVERIFY(window.isVisible());
        auto second = dir(QStringLiteral("keyboard-zoom-second.png"));
        QVERIFY(registry.save(d, second, "png", 95, true).isEmpty());
        view->setZoom(2);
        window.findChild<QAction *>(QStringLiteral("lockZoom"))->trigger();
        window.openPath(second);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, second, 10000);
        QCOMPARE(view->zoom(), 2.0);
        window.findChild<QAction *>(QStringLiteral("lockZoom"))->trigger();
        window.openPath(path);
        QTRY_COMPARE_WITH_TIMEOUT(window.document().path, path, 10000);
        QCOMPARE(view->zoom(), 1.0);
    }
    void keyboardHelpAndBranding() {
        ix::MainWindow window;
        window.show();
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        auto *view = window.findChild<ix::ImageView *>();
        view->setFocus();
        bool helpOpened = false, brandCorrect = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto *d = window.findChild<QDialog *>(QStringLiteral("keyboardHelpDialog"))) {
                auto *table = d->findChild<QTableWidget *>(QStringLiteral("shortcutTable"));
                helpOpened = table && table->rowCount() > 40;
                d->reject();
            }
        });
        QTest::keyClick(view, Qt::Key_F1);
        QVERIFY(helpOpened);
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        view->setFocus();
        QTimer::singleShot(0, &window, [&] {
            if (auto *d = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                brandCorrect = d->text().contains(QStringLiteral("Von IrfanView und nomacs inspiriert.")) &&
                               !d->text().contains(QStringLiteral("Reimplementierung"));
                d->accept();
            }
        });
        QTest::keyClick(view, Qt::Key_A);
        QVERIFY(brandCorrect);
        QVERIFY(window.findChild<QAction *>(QStringLiteral("zoomIn"))
                    ->shortcuts()
                    .contains(QKeySequence(QStringLiteral("="))));
        QVERIFY(window.findChild<QAction *>(QStringLiteral("rename"))
                    ->shortcuts()
                    .contains(QKeySequence(Qt::Key_F6)));
        QVERIFY(window.findChild<QAction *>(QStringLiteral("copy"))
                    ->shortcuts()
                    .contains(QKeySequence(QStringLiteral("Ctrl+Insert"))));
    }
    void rotationIdentityAndMetadata() {
        auto source = pattern(73, 49).convertToFormat(QImage::Format_RGBA64);
        source.setText(QStringLiteral("Description"), QStringLiteral("precision fixture"));
        source.setPixelColor(3, 2, QColor::fromRgba64(231, 65432, 117, 0));
        for (double angle : {0.0, 360.0, -360.0})
            for (bool fast : {false, true}) {
                ix::RotationOptions o;
                o.angle = angle;
                o.fastMethod = fast;
                o.selectOriginal = true;
                auto r = ix::rotateImage(source, o);
                QCOMPARE(r.pixels, source);
                QCOMPARE(r.selection, source.rect());
                QCOMPARE(r.pixels.colorSpace(), source.colorSpace());
                QCOMPARE(r.pixels.dotsPerMeterX(), source.dotsPerMeterX());
                QCOMPARE(r.pixels.text(QStringLiteral("Description")),
                         source.text(QStringLiteral("Description")));
            }
    }
    void rotationCanvasAndAlpha() {
        QImage source(80, 40, QImage::Format_RGB32);
        source.fill(Qt::white);
        for (bool fast : {false, true}) {
            ix::RotationOptions o;
            o.angle = 45;
            o.antialias = false;
            o.fastMethod = fast;
            o.background = Qt::red;
            auto expanded = ix::rotateImage(source, o);
            QVERIFY(expanded.pixels.width() > 80);
            QVERIFY(expanded.pixels.height() > 40);
            QCOMPARE(expanded.pixels.pixelColor(0, 0), QColor(Qt::red));
            o.keepCanvas = true;
            auto cropped = ix::rotateImage(source, o);
            QCOMPARE(cropped.pixels.size(), source.size());
            QCOMPARE(cropped.pixels.pixelColor(79, 0), QColor(Qt::red));
            o.originalAsBackground = true;
            auto layered = ix::rotateImage(source, o);
            QCOMPARE(layered.pixels.pixelColor(79, 0), QColor(Qt::white));
            source = source.convertToFormat(QImage::Format_ARGB32);
            o.keepCanvas = false;
            o.originalAsBackground = false;
            auto alpha = ix::rotateImage(source, o);
            QCOMPARE(alpha.pixels.pixelColor(0, 0).alpha(), 0);
            source = source.convertToFormat(QImage::Format_RGB32);
        }
    }
    void rotationSamplingAndSharpen() {
        auto source = pattern(43, 31);
        ix::RotationOptions o;
        o.angle = 31.7;
        o.antialias = false;
        auto nearest = ix::rotateImage(source, o).pixels;
        o.antialias = true;
        auto smooth = ix::rotateImage(source, o).pixels;
        QVERIFY(nearest != smooth);
        o.sharpen = true;
        QVERIFY(ix::rotateImage(source, o).pixels != smooth);
        o.angle = std::numeric_limits<double>::quiet_NaN();
        QVERIFY_EXCEPTION_THROWN(ix::rotateImage(source, o), std::runtime_error);
        o.angle = 361;
        QVERIFY_EXCEPTION_THROWN(ix::rotateImage(source, o), std::runtime_error);
    }
    void maximalRotationSelection() {
        QCOMPARE(ix::maximalOriginalRect({100, 50}, {100, 50}, 0), QRect(0, 0, 100, 50));
        QCOMPARE(ix::maximalOriginalRect({100, 50}, {50, 100}, 90), QRect(0, 0, 50, 100));
        auto square = ix::maximalOriginalRect({100, 100}, {142, 142}, 45);
        QCOMPARE(square.size(), QSize(70, 70));
        for (double angle : {-73.0, -45.0, -12.5, 3.0, 25.0, 60.0, 89.0}) {
            ix::RotationOptions o;
            o.angle = angle;
            o.selectOriginal = true;
            auto r = ix::rotateImage(pattern(140, 80), o);
            QVERIFY(!r.selection.isEmpty());
            QVERIFY(r.pixels.rect().contains(r.selection));
            const double c = std::abs(std::cos(angle * std::numbers::pi / 180)),
                         sine = std::abs(std::sin(angle * std::numbers::pi / 180));
            // Include pixel-boundary coordinates and the integer centering offset.
            const double a = r.selection.width() / 2.0 +
                             std::abs(r.selection.x() + r.selection.width() / 2.0 - r.pixels.width() / 2.0);
            const double b = r.selection.height() / 2.0 +
                             std::abs(r.selection.y() + r.selection.height() / 2.0 - r.pixels.height() / 2.0);
            QVERIFY2(c * a + sine * b <= 70.00001 && sine * a + c * b <= 40.00001,
                     qPrintable(QString::number(angle)));
        }
    }
    void selectionMaskExactness() {
        auto source = pattern(13, 9).convertToFormat(QImage::Format_RGBA64);
        source.setPixelColor(1, 1, QColor::fromRgba64(127, 61357, 8191, 0));
        source.setText(QStringLiteral("Comment"), QStringLiteral("mask"));
        QRect selected(3, 2, 6, 4);
        auto paintWhite = [](const QImage &input) {
            auto out = input.convertToFormat(QImage::Format_RGBA64);
            out.fill(Qt::white);
            return out;
        };
        for (bool inverted : {false, true}) {
            auto output = ix::applySelectionEffect(source, selected, inverted, paintWhite);
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 13; ++x) {
                    bool changed = selected.contains(x, y) != inverted;
                    QCOMPARE(output.pixelColor(x, y), changed ? QColor(Qt::white) : source.pixelColor(x, y));
                }
            QCOMPARE(output.colorSpace(), source.colorSpace());
            QCOMPARE(output.dotsPerMeterX(), source.dotsPerMeterX());
            QCOMPARE(output.text(QStringLiteral("Comment")), QStringLiteral("mask"));
        }
        QVERIFY_EXCEPTION_THROWN(
            ix::applySelectionEffect(source, selected, false,
                                     [](const QImage &) { return QImage(1, 1, QImage::Format_ARGB32); }),
            std::runtime_error);
    }
    void maximizeSelection() {
        QCOMPARE(ix::centeredMaximalSelection({640, 480}, {80, 40}), QRect(0, 80, 640, 320));
        QCOMPARE(ix::centeredMaximalSelection({640, 480}, {40, 80}), QRect(200, 0, 240, 480));
        QVERIFY(ix::centeredMaximalSelection({640, 480}, {}).isEmpty());
    }
    void rotationDialogControls() {
        ix::RotationDialog dialog(pattern());
        dialog.show();
        auto *angle = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("angle"));
        QVERIFY(angle);
        QCOMPARE(angle->value(), 45.0);
        QVERIFY(dialog.options().antialias);
        QVERIFY(!dialog.options().keepCanvas);
        QCOMPARE(dialog.options().sharpenAmount, 20);
        auto *canvas = dialog.findChild<QCheckBox *>(QStringLiteral("keepCanvas"));
        auto *background = dialog.findChild<QCheckBox *>(QStringLiteral("originalBackground"));
        QVERIFY(!background->isEnabled());
        canvas->setChecked(true);
        QVERIFY(background->isEnabled());
        auto *sharpen = dialog.findChild<QCheckBox *>(QStringLiteral("sharpen"));
        auto *amount = dialog.findChild<QSpinBox *>(QStringLiteral("sharpenAmount"));
        QVERIFY(!amount->isEnabled());
        sharpen->setChecked(true);
        QVERIFY(amount->isEnabled());
        amount->setValue(45);
        QCOMPARE(dialog.options().sharpenAmount, 45);
        QTest::keyClick(angle, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(angle->value(), 46.0);
        QTest::keyClick(angle, Qt::Key_Down);
        QCOMPARE(angle->value(), 45.9);
        auto *original = dialog.findChild<QWidget *>(QStringLiteral("rotationOriginal"));
        QTest::mousePress(original, Qt::LeftButton, Qt::NoModifier, QPoint(35, 45));
        QTest::mouseMove(original, QPoint(135, 95));
        QTest::mouseRelease(original, Qt::LeftButton, Qt::NoModifier, QPoint(135, 95));
        QVERIFY(std::abs(angle->value() + 26.6) < .1);
        QTest::mouseClick(dialog.findChild<QWidget *>(QStringLiteral("rotationResult")), Qt::RightButton);
        QCOMPARE(angle->value(), 0.0);
        QTest::mouseClick(original, Qt::RightButton);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }
    void selectionHistoryAndInversion() {
        ix::ImageDocument source;
        source.pixels = pattern();
        ix::CodecRegistry registry;
        auto path = dir(QStringLiteral("selection-history.png"));
        QVERIFY(registry.save(source, path, "png", 95, true).isEmpty());
        ix::MainWindow window;
        window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        auto *view = window.findChild<ix::ImageView *>();
        QVERIFY(view);
        QRect selected(7, 9, 20, 10);
        view->setSelection(selected);
        view->setSelection({});
        window.dispatch(QStringLiteral("restoreSelection"));
        QCOMPARE(view->selection(), selected);
        window.dispatch(QStringLiteral("maximizeSelection"));
        QCOMPARE(view->selection(), QRect(0, 8, 64, 32));
        view->setSelection(selected);
        window.dispatch(QStringLiteral("crop"));
        QTRY_COMPARE_WITH_TIMEOUT(window.document().pixels.size(), selected.size(), 10000);
        QVERIFY(view->selection().isEmpty());
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().pixels.size(), QSize(64, 48));
        QCOMPARE(view->selection(), selected);
        window.dispatch(QStringLiteral("redo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().pixels.size(), selected.size());
        QVERIFY(view->selection().isEmpty());
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        window.dispatch(QStringLiteral("invertSelection"));
        QVERIFY(window.findChild<QAction *>(QStringLiteral("invertSelection"))->isChecked());
        window.dispatch(QStringLiteral("negativeAll"));
        QTRY_VERIFY_WITH_TIMEOUT(window.document().modified, 10000);
        auto output = window.document().pixels;
        QCOMPARE(output.pixelColor(8, 10), source.pixels.pixelColor(8, 10));
        auto before = source.pixels.pixelColor(0, 0), after = output.pixelColor(0, 0);
        QCOMPARE(after.red(), 255 - before.red());
        QCOMPARE(after.green(), 255 - before.green());
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().pixels, source.pixels);
        QCOMPARE(view->selection(), selected);
    }
    void visibleCropUsesOriginalPixels() {
        ix::ImageDocument d;
        d.pixels = pattern(1400, 1000);
        ix::CodecRegistry registry;
        auto path = dir(QStringLiteral("viewport.png"));
        QVERIFY(registry.save(d, path, "png", 95, true).isEmpty());
        ix::MainWindow window;
        window.show();
        window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        auto *view = window.findChild<ix::ImageView *>();
        view->setZoom(2);
        view->horizontalScrollBar()->setValue(213);
        view->verticalScrollBar()->setValue(147);
        auto r = view->visibleImageRect();
        QVERIFY(r.width() < 1400);
        QVERIFY(r.height() < 1000);
        QVERIFY(r.x() > 0);
        QVERIFY(window.findChild<QAction *>(QStringLiteral("cropViewport"))->isEnabled());
        window.dispatch(QStringLiteral("cropViewport"));
        QTRY_COMPARE_WITH_TIMEOUT(window.document().pixels.size(), r.size(), 10000);
        QCOMPARE(window.document().pixels, d.pixels.copy(r));
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().pixels, d.pixels);
    }
    void rotateCommandWorker() {
        ix::MainWindow window;
        ix::ImageDocument d;
        d.pixels = pattern(80, 40);
        ix::CodecRegistry registry;
        auto path = dir(QStringLiteral("dialog-rotation.png"));
        QVERIFY(registry.save(d, path, "png", 95, true).isEmpty());
        window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        auto *view = window.findChild<ix::ImageView *>();
        QRect old(5, 5, 20, 10);
        view->setSelection(old);
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<ix::RotationDialog *>();
            QVERIFY(dialog);
            dialog->findChild<QDoubleSpinBox *>(QStringLiteral("angle"))->setValue(45);
            dialog->findChild<QCheckBox *>(QStringLiteral("selectOriginal"))->setChecked(true);
            dialog->accept();
        });
        window.dispatch(QStringLiteral("rotate"));
        QTRY_VERIFY_WITH_TIMEOUT(window.document().modified, 10000);
        QVERIFY(!view->selection().isEmpty());
        auto rotated = window.document().pixels;
        auto mark = view->selection();
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().pixels, d.pixels);
        QCOMPARE(view->selection(), old);
        window.dispatch(QStringLiteral("redo"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 10000);
        QCOMPARE(window.document().pixels, rotated);
        QCOMPARE(view->selection(), mark);
    }

    void automaticSelectionWorker() {
        ix::ImageDocument d;
        d.pixels = QImage(100, 80, QImage::Format_RGB32);
        d.pixels.fill(Qt::white);
        QPainter painter(&d.pixels);
        painter.fillRect(20, 15, 60, 50, Qt::blue);
        painter.end();
        ix::CodecRegistry registry;
        auto path = dir(QStringLiteral("automatic-selection.png"));
        QVERIFY(registry.save(d, path, "png", 95, true).isEmpty());
        ix::MainWindow window;
        window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        window.dispatch(QStringLiteral("autoSelect"));
        auto *view = window.findChild<ix::ImageView *>();
        QTRY_COMPARE_WITH_TIMEOUT(view->selection(), QRect(20, 15, 60, 50), 10000);
        QVERIFY(!window.document().modified);
        QCOMPARE(window.document().pixels, d.pixels);
    }
    void histogramWorker() {
        QTimer::singleShot(0, this, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto *chart = dialog->findChild<QLabel *>(QStringLiteral("histogramChart"));
            QVERIFY(chart);
            QTRY_VERIFY_WITH_TIMEOUT(!chart->pixmap().isNull(), 10000);
            QVERIFY(dialog->grab().save(QStringLiteral(IX_TEST_OUTPUT "/histogram-dialog.png")));
            dialog->reject();
        });
        ix::histogramDialog(nullptr, pattern(640, 480));
        // Closing immediately cancels row scanning before joining the worker.
        QTimer::singleShot(0, this, [] {
            auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(d);
            d->reject();
        });
        ix::histogramDialog(nullptr, pattern(1000, 1000));
    }
    void jpegLossless() {
        ix::ImageDocument d;
        d.pixels = pattern(64, 48);
        ix::CodecRegistry registry;
        auto source = dir(QStringLiteral("lossless.jpg")), rotated = dir(QStringLiteral("rotated.jpg")),
             restored = dir(QStringLiteral("restored.jpg"));
        QVERIFY(registry.save(d, source, "jpeg", 95, true).isEmpty());
        QVERIFY(ix::transformJpeg(source, rotated, ix::JpegTransform::Right).isEmpty());
        QVERIFY(ix::transformJpeg(rotated, restored, ix::JpegTransform::Left).isEmpty());
        auto original = registry.read(source), result = registry.read(restored);
        QVERIFY(original.ok() && result.ok());
        QCOMPARE(original.document.pixels, result.document.pixels);
        QVERIFY(!ix::transformJpeg(source, dir(QStringLiteral("invalidCrop.jpg")), ix::JpegTransform::Crop,
                                   {1, 1, 20, 20})
                     .isEmpty());
    }
    void metadataStreamsUnicodeReadOnlyFiles() {
#ifdef IX_TEST_EXIV2
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(pattern().save(&buffer, "JPEG"));
        auto image = Exiv2::ImageFactory::open(reinterpret_cast<const Exiv2::byte *>(bytes.constData()),
                                               size_t(bytes.size()));
        image->readMetadata();
        image->exifData()["Exif.Image.Make"] = "IxView streaming fixture";
        image->iptcData()["Iptc.Application2.Caption"] = "Metadata range reading";
        image->xmpData()["Xmp.dc.description"] = "Read only Unicode file";
        image->writeMetadata();
        auto &io = image->io();
        io.open();
        const auto encoded = io.read(io.size());
        const auto path = dir(QStringLiteral("元画像-é-읽기.jpg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
#if EXIV2_TEST_VERSION(0, 28, 0)
        QCOMPARE(file.write(reinterpret_cast<const char *>(encoded.c_data()), qint64(encoded.size())),
                 qint64(encoded.size()));
#else
        QCOMPARE(file.write(reinterpret_cast<const char *>(encoded.pData_), qint64(encoded.size_)),
                 qint64(encoded.size_));
#endif
        // Valid JPEG metadata precedes the pixel stream; trailing payload must
        // not require copying the entire file merely to inspect those tags.
        const QByteArray padding(1024 * 1024, 'x');
        for (int i = 0; i < 16; ++i)
            QCOMPARE(file.write(padding), qint64(padding.size()));
        file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::ReadUser));
        const auto restore = qScopeGuard([&] { file.setPermissions(QFile::ReadOwner | QFile::WriteOwner); });
        QString warning;
        const auto metadata = ix::readMetadata(path, warning);
        QVERIFY2(metadata, qPrintable(warning));
        QVERIFY(metadata->hasExif && metadata->hasIptc && metadata->hasXmp);
        QCOMPARE(metadata->tags.value(QStringLiteral("Exif.Image.Make")), QStringLiteral("IxView streaming fixture"));
        QCOMPARE(metadata->tags.value(QStringLiteral("Iptc.Application2.Caption")), QStringLiteral("Metadata range reading"));
        QVERIFY(metadata->tags.value(QStringLiteral("Xmp.dc.description")).contains(QStringLiteral("Read only Unicode file")));
        QVERIFY(file.open(QIODevice::ReadOnly));
#if EXIV2_TEST_VERSION(0, 28, 0)
        QCOMPARE(file.size(), qint64(encoded.size()) + 16 * 1024 * 1024);
#else
        QCOMPARE(file.size(), qint64(encoded.size_) + 16 * 1024 * 1024);
#endif
        QVERIFY(file.seek(file.size() - padding.size()));
        QCOMPARE(file.readAll(), padding);
        file.close();
        const auto empty = dir(QStringLiteral("metadata-empty.jpg"));
        QFile invalid(empty);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.close();
        warning.clear();
        QVERIFY(!ix::readMetadata(empty, warning));
        QVERIFY(!warning.isEmpty());
        ix::ImageDocument document;
        document.pixels = QImage(600, 400, QImage::Format_RGB32);
        quint32 state = 42;
        for (int y = 0; y < document.pixels.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(document.pixels.scanLine(y));
            for (int x = 0; x < document.pixels.width(); ++x) {
                state = state * 1664525u + 1013904223u;
                row[x] = qRgb(state >> 24, state >> 16, state >> 8);
            }
        }
        document.metadata = metadata;
        document.sourceHasExif = true;
        const auto png = dir(QStringLiteral("metadata-buffer-boundaries.png"));
        ix::CodecRegistry registry;
        QVERIFY(registry.save(document, png, "png", 95, true).isEmpty());
        QVERIFY(QFileInfo(png).size() > 64 * 1024);
        warning.clear();
        const auto pngMetadata = ix::readMetadata(png, warning);
        QVERIFY2(pngMetadata, qPrintable(warning));
        QVERIFY(pngMetadata->hasExif && pngMetadata->hasIptc && pngMetadata->hasXmp);
        QCOMPARE(pngMetadata->tags.value(QStringLiteral("Exif.Image.Make")),
                 metadata->tags.value(QStringLiteral("Exif.Image.Make")));
        QCOMPARE(pngMetadata->tags.value(QStringLiteral("Iptc.Application2.Caption")),
                 metadata->tags.value(QStringLiteral("Iptc.Application2.Caption")));
        QCOMPARE(pngMetadata->tags.value(QStringLiteral("Xmp.dc.description")),
                 metadata->tags.value(QStringLiteral("Xmp.dc.description")));
#else
        QSKIP("Exiv2 not built");
#endif
    }
    void exifOrientation() {
#ifdef IX_TEST_EXIV2
        QImage image = pattern(64, 48);
        QByteArray bytes;
        QBuffer b(&bytes);
        b.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&b, "JPEG"));
        auto ex = Exiv2::ImageFactory::open(reinterpret_cast<const Exiv2::byte *>(bytes.constData()),
                                            size_t(bytes.size()));
        ex->readMetadata();
        ex->exifData()["Exif.Image.Orientation"] = uint16_t(6);
        ex->exifData()["Exif.Image.Make"] = "IxView Fixture";
        ex->exifData()["Exif.Photo.DateTimeOriginal"] = "2026:10:17 12:34:56";
        ex->writeMetadata();
        auto &io = ex->io();
        io.open();
        auto buffer = io.read(io.size());
        QFile file(dir(QStringLiteral("oriented.jpg")));
        QVERIFY(file.open(QIODevice::WriteOnly));
#if EXIV2_TEST_VERSION(0, 28, 0)
        file.write(reinterpret_cast<const char *>(buffer.c_data()), qint64(buffer.size()));
#else
        file.write(reinterpret_cast<const char *>(buffer.pData_), qint64(buffer.size_));
#endif
        file.close();
        ix::CodecRegistry registry;
        auto read = registry.read(file.fileName());
        QVERIFY2(read.ok(), qPrintable(read.error));
        QCOMPARE(read.document.pixels.size(), QSize(48, 64));
        QVERIFY(read.document.metadata);
        auto out = dir(QStringLiteral("oriented.png"));
        auto error = registry.save(read.document, out, "png", 95, true);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto reread = registry.read(out);
        QVERIFY2(reread.ok(), qPrintable(reread.error));
        QCOMPARE(reread.document.pixels.size(), read.document.pixels.size());
        QCOMPARE(reread.document.pixels, read.document.pixels);
        QCOMPARE(reread.document.metadata->tags.value(QStringLiteral("Exif.Image.Make")),
                 QStringLiteral("IxView Fixture"));
        QCOMPARE(reread.document.metadata->tags.value(QStringLiteral("Exif.Image.Orientation")),
                 QStringLiteral("1"));
#else
        QSKIP("Exiv2 not built");
#endif
    }
    void roundTripFormats_data() {
        QTest::addColumn<QByteArray>("format");
        QSet<QByteArray> added;
        for (auto f : QImageWriter::supportedImageFormats()) {
            if (f == "jpg" || f == "tif" || f == "jpe" || f == "icns")
                continue;
            if (!added.contains(f)) {
                QTest::newRow(f.constData()) << f;
                added.insert(f);
            }
        }
    }
    void roundTripFormats() {
        QFETCH(QByteArray, format);
        ix::ImageDocument d;
        d.pixels = pattern();
        ix::CodecRegistry registry;
        auto path = dir(QStringLiteral("roundtrip.") + QString::fromLatin1(format));
        auto error = registry.save(d, path, format, 95, true);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto read = registry.read(path);
        QVERIFY2(read.ok(), qPrintable(read.error));
        QCOMPARE(read.document.pixels.size(), d.pixels.size());
        QVERIFY2(!read.document.format.isEmpty(), "Decoded document lost its detected format");
    }
    void renderUi() {
        QApplication::setFont(ix::referenceFont(9));
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("windows")));
        ix::MainWindow window;
        window.resize(980, 662);
        window.show();
        ix::ImageDocument document;
        document.pixels = pattern(640, 480);
        ix::CodecRegistry registry;
        auto path = dir(QStringLiteral("display.png"));
        QVERIFY(registry.save(document, path, "png", 95, true).isEmpty());
        window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.document().empty(), 10000);
        QVERIFY(window.grab().save(QStringLiteral(IX_TEST_OUTPUT "/main-window.png")));
        ix::ResizeDialog resize({640, 480}, 96, &window);
        resize.show();
        QTest::qWait(30);
        QVERIFY(resize.grab().save(QStringLiteral(IX_TEST_OUTPUT "/resize-dialog.png")));
        resize.hide();
        ix::ColorDialog colors(document.pixels, &window);
        colors.show();
        QTest::qWait(30);
        QVERIFY(colors.grab().save(QStringLiteral(IX_TEST_OUTPUT "/color-dialog.png")));
        colors.hide();
        ix::RotationDialog rotation(document.pixels, &window);
        rotation.show();
        QTest::qWait(30);
        QVERIFY(rotation.grab().save(QStringLiteral(IX_TEST_OUTPUT "/rotation-dialog.png")));
        rotation.hide();
    }
    void writeReports() {
        ix::CodecRegistry registry;
        QJsonArray formats;
        for (const auto &f : registry.capabilities()) {
            formats.append(
                QJsonObject{{QStringLiteral("format"), f.name},
                            {QStringLiteral("extensions"), QJsonArray::fromStringList(f.extensions)},
                            {QStringLiteral("read"), f.read},
                            {QStringLiteral("write"), f.write},
                            {QStringLiteral("backend"), f.library}});
        }
        QDir().mkpath(QStringLiteral(IX_TEST_OUTPUT));
        QFile report(QStringLiteral(IX_TEST_OUTPUT "/format-capabilities.json"));
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(formats).toJson());
        ix::ImageDocument d;
        d.pixels = pattern(640, 480);
        registry.save(d, QStringLiteral(IX_TEST_OUTPUT "/reference-pattern.png"), "png", 95, true);
    }
};
int main(int argc, char **argv) {
    ix::Application app(argc, argv);
    ix::configureDesktopAppearance();
    // Offscreen has no native Windows theme handles.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("windows")));
    IxTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_ixview.moc"
