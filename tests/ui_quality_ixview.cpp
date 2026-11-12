#include <QSettings>
#include "core/image_storage.h"
#include "core/image_document.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/localization.h"
#include "core/directory_index.h"
#include "core/directory_index_service.h"
#include "core/image_catalog.h"
#include "ui/appearance.h"
#include "ui/dialogs.h"
#include "ui/extension_dialog.h"
#include "ui/interface_language.h"
#include "ui/main_window.h"
#include "ui/menu_support.h"
#include "ui/pipeline_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/library_dialog.h"
#include "ui/worker_wait.h"
#include "ui/platform_support.h"
#include <QLockFile>
#include <QSemaphore>
#include <QPromise>
#include <QAccessible>
#include <QScopeGuard>
#include <QtTest>
#include <QtWidgets>
#include <cmath>
#include <atomic>
#include <future>
#include <thread>
#include <chrono>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <bit>
#endif

namespace {
double luminance(QColor color) {
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}
double contrast(QColor a, QColor b) {
    const double first = luminance(a), second = luminance(b);
    return (std::max(first, second) + .05) / (std::min(first, second) + .05);
}
}
class UiQualityTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_;
    void variants() {
        QTest::addColumn<QString>("theme"); QTest::addColumn<QString>("language");
        for (const auto *theme : {"light", "dark"})
            for (const auto *language : {"de", "en", "fr", "ko", "ja"})
                QTest::newRow((QByteArray(theme) + '-' + language).constData())
                    << QString::fromLatin1(theme) << QString::fromLatin1(language);
    }
    void probe(const std::function<void()> &open, const std::function<void(QDialog *)> &inspect) {
        bool inspected = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            timer.stop(); inspected = true;
            auto close = qScopeGuard([&] { dialog->reject(); });
            inspect(dialog);
        });
        timer.start(25); open(); timer.stop(); QVERIFY(inspected);
    }
    void capture(QWidget *widget, const QString &name) {
        const auto output = QStringLiteral(IX_UI_OUTPUT "/ui-audit");
        QVERIFY(QDir().mkpath(output));
        QVERIFY(widget->grab().save(QDir(output).filePath(name + QStringLiteral(".png"))));
    }
  private slots:
    void pasteAvailabilityDoesNotReadPayloads_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("empty") << 0;
        QTest::newRow("text") << 1;
        QTest::newRow("image") << 2;
        QTest::newRow("local-url") << 3;
        QTest::newRow("remote-url") << 4;
    }
    void pasteAvailabilityDoesNotReadPayloads() {
        QFETCH(int, kind);
        class MimeData final : public QMimeData {
          public:
            mutable int reads = 0;
          protected:
            QVariant retrieveData(const QString &type, QMetaType preferred) const override {
                ++reads;
                return QMimeData::retrieveData(type, preferred);
            }
        };
        auto *data = new MimeData;
        QImage image(5, 3, QImage::Format_ARGB32); image.fill(QColor(17, 39, 83, 123));
        if (kind == 1) data->setText(QStringLiteral("ordinary text"));
        if (kind == 2) data->setImageData(image);
        if (kind == 3) data->setUrls({QUrl::fromLocalFile(QStringLiteral("/test/image.png"))});
        if (kind == 4) data->setUrls({QUrl(QStringLiteral("https://example.invalid/image.png"))});
        auto *clipboard = QApplication::clipboard();
        clipboard->setMimeData(data);
        auto cleanup = qScopeGuard([&] { clipboard->clear(); });
        QApplication::processEvents();
        data->reads = 0;
        QCOMPARE(ix::clipboardHasImageOrUrls(), kind >= 2);
        QCOMPARE(data->reads, 0);
#ifdef Q_OS_WIN
        // Availability must not wait for an unrelated clipboard lock. The
        // timeout bounds a regression; the assertion observes lock lifetime.
        if (kind == 2 && QGuiApplication::platformName() == QStringLiteral("windows")) {
            std::promise<bool> acquired;
            std::promise<void> release;
            auto done = release.get_future();
            std::atomic_bool locked = false;
            std::thread holder([&] {
                const bool opened = OpenClipboard(nullptr);
                locked.store(opened); acquired.set_value(opened);
                if (opened) {
                    done.wait_for(std::chrono::seconds(3));
                    CloseClipboard(); locked.store(false);
                }
            });
            auto unlock = qScopeGuard([&] { release.set_value(); holder.join(); });
            QVERIFY(acquired.get_future().get());
            QVERIFY(ix::clipboardHasImageOrUrls());
            QVERIFY(locked.load());
        }
#endif
        ix::MainWindow window;
        QAction *paste = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->data().toString() == QStringLiteral("paste")) paste = action;
        QVERIFY(paste); QCOMPARE(paste->isEnabled(), kind >= 2);
        QCOMPARE(data->reads, 0);
        if (kind == 2) {
            window.dispatch(QStringLiteral("paste"));
            QCOMPARE(window.document().pixels, image);
        }
        clipboard->setText(QStringLiteral("changed to plain text"));
        QTRY_VERIFY(!paste->isEnabled());
    }
    void progressJoinHandlesFastAndShowTimeCompletion() {
        for (int mode=0;mode<4;++mode) {
            QPromise<int> promise; promise.start();
            QFutureWatcher<int> watcher; watcher.setFuture(promise.future());
            struct Progress final : QProgressDialog {
                int shows=0;
                std::function<void()> duringShow;
                void showEvent(QShowEvent *event) override {
                    QProgressDialog::showEvent(event); ++shows;
                    if (duringShow) duringShow();
                }
            } progress;
            progress.setCancelButton(nullptr); progress.setRange(0,0);
            progress.setWindowModality(Qt::ApplicationModal);
            connect(&watcher,&QFutureWatcherBase::finished,&progress,&QDialog::accept);
            const auto finish=[&] { promise.addResult(42); promise.finish(); };
            if (mode==0) finish();
            else if (mode==1) QTimer::singleShot(1,&watcher,finish);
            else if (mode==2) progress.duringShow=[&] {
                finish(); QCoreApplication::sendPostedEvents(&watcher);
            };
            else QTimer::singleShot(350,&watcher,finish);
            bool timedOut=false;
            QTimer watchdog; watchdog.setSingleShot(true);
            connect(&watchdog,&QTimer::timeout,&progress,[&] { timedOut=true; finish(); progress.reject(); });
            watchdog.start(2000);
            ix::waitForGuiWorker(watcher,progress);
            watchdog.stop(); QVERIFY(!timedOut); QCOMPARE(watcher.future().takeResult(),42);
            QCOMPARE(progress.shows,mode<2 ? 0 : 1); QVERIFY(!progress.isVisible());
        }
    }
    void brandIconsFollowThemeWithoutReplacingTheDocument() {
        const auto restore = qScopeGuard([] {
            ix::applyInterfaceTheme(QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString());
        });
        QSettings settings;
        const auto previous = settings.value(QStringLiteral("ui/theme"));
        const auto restoreSetting = qScopeGuard([&] {
            if (previous.isValid()) settings.setValue(QStringLiteral("ui/theme"), previous);
            else settings.remove(QStringLiteral("ui/theme"));
        });
        settings.setValue(QStringLiteral("ui/theme"), QStringLiteral("light"));
        ix::MainWindow window;
        window.show();
        const auto path = temporary_.filePath(QStringLiteral("brand-image.png"));
        QImage image(80,60,QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(path));
        window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && window.document().path == path);
        const auto source = window.document().pixels.cacheKey();
        QDialog existing(&window); existing.show();
        for (const auto &mode : {QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("light")}) {
            ix::applyInterfaceTheme(mode);
            QCoreApplication::processEvents();
            const auto icon = QGuiApplication::windowIcon();
            const auto expected = QIcon(QStringLiteral(":/ixview-%1.svg").arg(mode)).pixmap(250,250).toImage();
            QVERIFY(!expected.isNull()); QVERIFY(expected.hasAlphaChannel());
            QCOMPARE(expected.pixelColor(0,0).alpha(),0);
            QCOMPARE(icon.pixmap(250,250).toImage(),expected);
            QCOMPARE(window.windowIcon().pixmap(250,250).toImage(),expected);
            QCOMPARE(existing.windowIcon().pixmap(250,250).toImage(),expected);
            QCOMPARE(window.document().pixels.cacheKey(),source);
            const auto cached = icon.cacheKey();
            ix::applyInterfaceTheme(mode);
            QCOMPARE(QGuiApplication::windowIcon().cacheKey(),cached);
            capture(&window,QStringLiteral("brand-")+mode);
        }
        bool inspected = false;
        QTimer aboutTimer;
        connect(&aboutTimer,&QTimer::timeout,&window,[&] {
            for (auto *box : window.findChildren<QMessageBox *>()) {
                if (!box->isVisible() || !box->property("ixBrandLogo").toBool()) continue;
                aboutTimer.stop(); inspected = true;
                const auto close = qScopeGuard([&] { box->accept(); });
                const auto before = box->iconPixmap().toImage();
                ix::applyInterfaceTheme(QStringLiteral("dark"));
                QVERIFY(box->iconPixmap().toImage() != before);
                QCOMPARE(box->iconPixmap().toImage(),QGuiApplication::windowIcon().pixmap(QSize(64,64),box->devicePixelRatioF()).toImage());
                capture(box,QStringLiteral("brand-about-dark"));
                return;
            }
        });
        auto *about = window.findChild<QAction *>(QStringLiteral("about")); QVERIFY(about);
        aboutTimer.start(25); about->trigger();
        QTRY_VERIFY(inspected); aboutTimer.stop();
        ix::applyInterfaceTheme(QStringLiteral("system"));
        QCoreApplication::processEvents();
        QTRY_COMPARE(window.palette().color(QPalette::Window),QApplication::palette().color(QPalette::Window));
        const bool dark = window.palette().color(QPalette::Window).lightnessF() < .5;
        QCOMPARE(window.windowIcon().pixmap(250,250).toImage(),
            QIcon(dark ? QStringLiteral(":/ixview-dark.svg") : QStringLiteral(":/ixview-light.svg")).pixmap(250,250).toImage());
    }
    void blackEntryCanLeaveFullscreen() {
        QSettings settings; QVariantMap previous;
        for (const auto &key : settings.allKeys()) previous.insert(key, settings.value(key));
        const auto restore = qScopeGuard([&] {
            settings.clear();
            for (auto i = previous.cbegin(); i != previous.cend(); ++i) settings.setValue(i.key(), i.value());
        });
        settings.setValue(QStringLiteral("navigation/unreadableFiles"), 2);
        settings.setValue(QStringLiteral("navigation/sort"), 0);
        settings.setValue(QStringLiteral("navigation/reverse"), false);
        QTemporaryDir folder; QVERIFY(folder.isValid());
        const auto first = folder.filePath(QStringLiteral("1.png"));
        const auto bad = folder.filePath(QStringLiteral("2.txt"));
        QImage image(16,12,QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(first));
        QFile file(bad); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("unreadable"); file.close();
        ix::MainWindow window; window.show();
        auto *index = window.findChild<ix::DirectoryIndexService *>(); QVERIFY(index);
        QSignalSpy ready(index, &ix::DirectoryIndexService::ready);
        window.openPath(first); QTRY_VERIFY(!window.isBusy() && !ready.isEmpty());
        window.dispatch(QStringLiteral("fullscreen")); QTRY_VERIFY(window.isFullScreen());
        window.dispatch(QStringLiteral("next"));
        QTRY_VERIFY(!window.isBusy() && window.document().path == bad);
        QVERIFY(window.document().empty());
        auto *fullscreen = window.findChild<QAction *>(QStringLiteral("fullscreen"));
        QVERIFY(fullscreen); QVERIFY(fullscreen->isEnabled());
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY(!window.isFullScreen());
        QCOMPARE(window.document().path, bad);
        window.dispatch(QStringLiteral("previous"));
        QTRY_VERIFY(!window.isBusy() && window.document().path == first);
    }
    void blackEntryKeepsFullscreenMouseNavigation() {
        ix::ImageView view;
        view.setUnavailable(true);
        view.showFullScreen();
        QTRY_VERIFY(view.isFullScreen());
        QSignalSpy navigation(&view, &ix::ImageView::navigate);
        const auto center = view.viewport()->rect().center();
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier, center);
        QCOMPARE(navigation.size(), 2);
        QCOMPARE(navigation[0][0].toInt(), -1);
        QCOMPARE(navigation[1][0].toInt(), 1);
        QVERIFY(view.image().isNull());
        QVERIFY(view.selection().isEmpty());
        view.setUnavailable(false);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, center);
        QCOMPARE(navigation.size(), 2);
    }
    void nativeTitleBarsFollowThemeAndHandleChanges() {
        const auto restore = qScopeGuard([] {
            ix::applyInterfaceTheme(QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString());
        });
        QWidget untouched;
        QVERIFY(!untouched.internalWinId());
        QVERIFY(!ix::applyNativeWindowTheme(&untouched, true));
        QVERIFY(!untouched.internalWinId()); // The helper must not create a native surface.
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            using Get = HRESULT (WINAPI *)(HWND, DWORD, PVOID, DWORD);
            const auto module = GetModuleHandleW(L"dwmapi.dll");
            const auto get = module ? std::bit_cast<Get>(GetProcAddress(module, "DwmGetWindowAttribute")) : nullptr;
            QVERIFY(get);
            const auto darkFrame = [get](QWidget &window) {
                BOOL value = FALSE;
                const auto handle = reinterpret_cast<HWND>(window.internalWinId());
                if (FAILED(get(handle, 20, &value, sizeof(value))) &&
                    FAILED(get(handle, 19, &value, sizeof(value)))) return -1;
                return value ? 1 : 0;
            };
            ix::applyInterfaceTheme(QStringLiteral("light"));
            QWidget window;
            window.setWindowTitle(QStringLiteral("Title bar regression"));
            window.resize(320,160); window.show();
            QTRY_COMPARE(darkFrame(window),0);
            const auto geometry = window.geometry();
            const auto handle = window.internalWinId();
            for (const auto &mode : {QStringLiteral("dark"),QStringLiteral("light"),QStringLiteral("dark")}) {
                ix::applyInterfaceTheme(mode);
                QCoreApplication::processEvents();
                QTRY_COMPARE(darkFrame(window),mode==QStringLiteral("dark") ? 1 : 0);
                QCOMPARE(window.internalWinId(),handle);
                QCOMPARE(window.geometry(),geometry);
            }
            QDialog dialog(&window); dialog.resize(200,100); dialog.show();
            QTRY_COMPARE(darkFrame(dialog),1);
            ix::applyInterfaceTheme(QStringLiteral("light"));
            QCoreApplication::processEvents();
            QTRY_COMPARE(darkFrame(window),0); QTRY_COMPARE(darkFrame(dialog),0);
            dialog.hide();
            window.hide();
            window.setWindowFlag(Qt::FramelessWindowHint,true);
            window.show();
            QVERIFY(!ix::applyNativeWindowTheme(&window,true));
            window.hide(); window.setWindowFlag(Qt::FramelessWindowHint,false);
            ix::applyInterfaceTheme(QStringLiteral("dark")); window.show();
            QTRY_COMPARE(darkFrame(window),1);
            return;
        }
#endif
        untouched.show();
        QVERIFY(!ix::applyNativeWindowTheme(&untouched,true));
    }
    void workerWaitKeepsGuiTimersAlive() {
        QSemaphore release;
        QFutureWatcher<int> watcher;
        bool heartbeat=false;
        QTimer::singleShot(30,&watcher,[&] { heartbeat=true; release.release(); });
        watcher.setFuture(QtConcurrent::run([&] { return release.tryAcquire(1,1000) ? 42 : -1; }));
        ix::waitForGuiWorker(watcher);
        QVERIFY(heartbeat); QCOMPARE(watcher.future().takeResult(),42);
    }
    void catalogAnnotationWaitKeepsGuiResponsive() {
        const auto prior=qApp->property("ixStorageData");
        const auto restore=qScopeGuard([&] { qApp->setProperty("ixStorageData",prior); });
        const auto root=temporary_.filePath(QStringLiteral("catalog-responsive"));
        QVERIFY(QDir().mkpath(root)); qApp->setProperty("ixStorageData",root);
        const auto path=temporary_.filePath(QStringLiteral("catalog-responsive.png"));
        QImage image(8,6,QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(path));
        ix::ImageCatalog catalog; QVERIFY(catalog.setAnnotation(path,1,QStringLiteral("green")).isEmpty());
        QLockFile lock(root+QStringLiteral("/catalog.json.lock")); QVERIFY(lock.tryLock(0));
        bool heartbeat=false, inspected=false;
        QTimer::singleShot(0,this,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            const auto close=qScopeGuard([&] { dialog->reject(); });
            QTRY_VERIFY_WITH_TIMEOUT(dialog->property("catalogReady").toBool(),10000);
            auto *list=dialog->findChild<QListWidget *>(QStringLiteral("catalogImages")); QVERIFY(list);
            QCOMPARE(list->count(),1); list->setCurrentRow(0); list->item(0)->setSelected(true);
            auto *rating=dialog->findChild<QSpinBox *>(QStringLiteral("catalogRating")); QVERIFY(rating);
            rating->setValue(4);
            auto *apply=dialog->findChild<QPushButton *>(QStringLiteral("catalogApply")); QVERIFY(apply);
            QTimer errors;
            connect(&errors,&QTimer::timeout,dialog,[] {
                if (auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) message->accept();
            });
            errors.start(10);
            QTimer::singleShot(100,dialog,[&] { heartbeat=true; lock.unlock(); });
            apply->click(); errors.stop();
            QVERIFY(heartbeat);
            QCOMPARE(catalog.record(path).value(QStringLiteral("rating")).toInt(),4);
            inspected=true;
        });
        ix::libraryDialog(nullptr,{path},[](QString) {});
        QVERIFY(inspected);
    }
    void missingRootMenusHaveStableFallbacks() {
        QMenuBar bar;
        auto *menu = ix::ensureRootMenu(bar, QStringLiteral("ansicht"), QStringLiteral("&Ansicht"));
        QVERIFY(menu); QCOMPARE(bar.actions().size(), 1);
        auto *action = menu->addAction(QStringLiteral("Keep"));
        QCOMPARE(ix::ensureRootMenu(bar, QStringLiteral("ansicht"), QStringLiteral("Other")), menu);
        QVERIFY(menu->actions().contains(action)); QCOMPARE(bar.actions().size(), 1);
        delete menu;
        QVERIFY(!bar.findChild<QMenu *>(QStringLiteral("ansicht")));
        auto *replacement = ix::ensureRootMenu(bar, QStringLiteral("ansicht"), QStringLiteral("&Ansicht"));
        QVERIFY(replacement); QCOMPARE(replacement->objectName(), QStringLiteral("ansicht"));
        QCOMPARE(bar.actions().size(), 1);
    }
    void missingCropMenuKeepsDocumentAndSelection() {
        const auto path = temporary_.filePath(QStringLiteral("missing-crop-menu.png"));
        QImage image(16, 12, QImage::Format_RGB32); image.fill(Qt::green);
        QVERIFY(image.save(path));
        ix::MainWindow window; window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && !window.document().empty(), 10000);
        auto *view = window.findChild<ix::ImageView *>();
        view->setSelection({1, 2, 3, 4});
        const auto key = window.document().pixels.cacheKey();
        auto *menu = window.findChild<QMenu *>(QStringLiteral("cropAspectMenu"));
        QVERIFY(menu); delete menu;
        window.dispatch(QStringLiteral("cropAspect"));
        QCOMPARE(window.document().pixels.cacheKey(), key);
        QCOMPARE(view->selection(), QRect(1, 2, 3, 4));
    }
    void init() {
        ix::configureImageStorage({false});
        QSettings().setValue(QStringLiteral("performance/swapEnabled"), false);
        // Existing workflow fixtures deliberately exercise the configured discard mode.
        QSettings().setValue(QStringLiteral("navigation/askToSave"), false);
    }
    void initTestCase() {
        ix::initializeImageMemoryLimit(512);
        ix::configureImageStorage({false});
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewUiQualityTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
        QSettings().setValue(QStringLiteral("performance/swapEnabled"), false);
        QStandardPaths::setTestModeEnabled(true);
        ix::applyInterfaceLanguage(QStringLiteral("de"));
        ix::applyInterfaceTheme(QStringLiteral("light"));
    }
    void compactSettingsPages_data() { variants(); }
    void compactSettingsPages() {
        QFETCH(QString, theme); QFETCH(QString, language);
        ix::applyInterfaceLanguage(language); ix::applyInterfaceTheme(theme);
        ix::SettingsDialog dialog;
        dialog.show(); QTest::qWait(20);
        QVERIFY(dialog.width() <= 800); QVERIFY(dialog.height() <= 500);
        auto *categories=dialog.findChild<QListWidget *>(QStringLiteral("settingsCategories"));
        auto *pages=dialog.findChild<QStackedWidget *>(QStringLiteral("settingsPages"));
        QVERIFY(categories && pages); QCOMPARE(categories->count(),12);
        const auto size=dialog.size();
        for(int i=0;i<categories->count();++i) {
            categories->setCurrentRow(i); QCoreApplication::processEvents();
            QCOMPARE(dialog.size(),size);
            QVERIFY(categories->viewport()->rect().contains(categories->visualItemRect(categories->item(i))));
            auto *scroll=qobject_cast<QScrollArea *>(pages->currentWidget()); QVERIFY(scroll);
            QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
            for(auto *number:scroll->findChildren<QSpinBox *>()) QVERIFY(number->width() <= 200);
            for(auto *number:scroll->findChildren<QDoubleSpinBox *>()) QVERIFY(number->width() <= 200);
            if(i==0 || i==2 || i==4 || i==5 || i==7 || i==9) capture(&dialog,QStringLiteral("settings-compact-%1-%2-%3").arg(theme,language).arg(i));
        }
        auto *buttons=dialog.findChild<QDialogButtonBox *>(); QVERIFY(buttons && buttons->isVisible());
    }
    void settingsTargetsAndLabelsStayUsable() {
        ix::SettingsDialog dialog;
        dialog.show(); QTest::qWait(20);
        for (auto *field : dialog.findChildren<QComboBox *>()) {
            QVERIFY(field->height() >= 32);
            QStyleOptionViewItem option; option.initFrom(field); option.fontMetrics=field->fontMetrics();
            QVERIFY(field->itemDelegate()->sizeHint(option,field->model()->index(0,0)).height() >= 32);
        }
        for (auto *field : dialog.findChildren<QAbstractSpinBox *>()) {
            QVERIFY(field->height() >= 32); QVERIFY(field->width() >= 96);
        }
        auto *box=dialog.findChild<QCheckBox *>(QStringLiteral("view/center")); QVERIFY(box);
        const bool wasChecked=box->isChecked();
        QTest::mouseClick(box,Qt::LeftButton,Qt::NoModifier,QPoint(box->width()-3,box->height()/2));
        QCOMPARE(box->isChecked(),!wasChecked);
        box->setFocus(); QTest::keyClick(box,Qt::Key_Space); QCOMPARE(box->isChecked(),wasChecked);
        dialog.selectPage(QStringLiteral("navigation/askToSave"));
        dialog.resize(620,460); QCoreApplication::processEvents();
        auto *longOption=dialog.findChild<QCheckBox *>(QStringLiteral("navigation/askToSave")); QVERIFY(longOption);
        QVERIFY(longOption->hasHeightForWidth());
        QVERIFY(longOption->height() >= longOption->heightForWidth(longOption->width()));
        QVERIFY(!longOption->text().contains(QLatin1Char('\n')));
        dialog.reject();
    }
    void settingsWheelRequiresDeliberateFocus() {
        ix::SettingsDialog dialog; dialog.show(); QTest::qWait(20);
        dialog.selectPage(QStringLiteral("previewSettingsPage"));
        auto *enabled=dialog.findChild<QCheckBox *>(QStringLiteral("view/previewStrip")); enabled->setChecked(true);
        auto *spin=dialog.findChild<QSpinBox *>(QStringLiteral("preview/previous")); spin->setValue(2);
        auto *categories=dialog.findChild<QListWidget *>(QStringLiteral("settingsCategories")); categories->setFocus();
        auto wheel=[](QWidget *field) {
            const auto point=field->rect().center();
            QWheelEvent event(QPointF(point),QPointF(field->mapToGlobal(point)),{},QPoint(0,120),
                              Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(field,&event);
        };
        wheel(spin); QCOMPARE(spin->value(),2);
        spin->setFocus(); QCoreApplication::processEvents(); wheel(spin); QCOMPARE(spin->value(),3);
        dialog.selectPage(QStringLiteral("view/fit"));
        auto *combo=dialog.findChild<QComboBox *>(QStringLiteral("view/fit")); combo->setCurrentIndex(2);
        categories->setFocus(); wheel(combo); QCOMPARE(combo->currentIndex(),2);
        combo->setFocus(); QCoreApplication::processEvents(); wheel(combo); QCOMPARE(combo->currentIndex(),1);
        dialog.reject();
    }
    void settingsPreviewDependenciesAndNewRatiosUseComfortableControls() {
        ix::SettingsDialog dialog; dialog.show();
        dialog.selectPage(QStringLiteral("previewSettingsPage"));
        auto *toggle=dialog.findChild<QCheckBox *>(QStringLiteral("view/previewStrip"));
        auto *previous=dialog.findChild<QSpinBox *>(QStringLiteral("preview/previous"));
        auto *form=qobject_cast<QFormLayout *>(previous->parentWidget()->layout());
        // Form is nested in the page's vertical layout, not its direct layout.
        if(!form) form=previous->parentWidget()->findChild<QFormLayout *>();
        QVERIFY(form); auto *label=form->labelForField(previous); QVERIFY(label);
        toggle->setChecked(false); QVERIFY(!previous->isEnabled()); QVERIFY(!label->isEnabled());
        toggle->setChecked(true); QVERIFY(previous->isEnabled()); QVERIFY(label->isEnabled());
        dialog.selectPage(QStringLiteral("cropSettingsPage"));
        auto *table=dialog.findChild<QTableWidget *>(QStringLiteral("crop/customRatios"));
        auto *add=dialog.findChild<QPushButton *>(QStringLiteral("addCropRatio")); QVERIFY(table && add);
        const int before=table->rowCount(); QTest::mouseClick(add,Qt::LeftButton);
        QCOMPARE(table->rowCount(),before+1);
        auto *field=qobject_cast<QSpinBox *>(table->cellWidget(before,0)); QVERIFY(field);
        QVERIFY(field->height()>=32); QVERIFY(field->width()>=96);
        QVERIFY(table->rowHeight(before)>field->height());
        dialog.reject();
    }
    void paletteTextReadability_data() {
        QTest::addColumn<QString>("theme");
        QTest::newRow("light") << QStringLiteral("light");
        QTest::newRow("dark") << QStringLiteral("dark");
    }
    void paletteTextReadability() {
        QFETCH(QString, theme); ix::applyInterfaceTheme(theme);
        const auto palette = QApplication::palette();
        for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
            for (const auto pair : {qMakePair(QPalette::Text, QPalette::Base),
                                   qMakePair(QPalette::WindowText, QPalette::Window),
                                   qMakePair(QPalette::ButtonText, QPalette::Button),
                                   qMakePair(QPalette::HighlightedText, QPalette::Highlight)}) {
                const auto ratio = contrast(palette.color(group, pair.first), palette.color(group, pair.second));
                QVERIFY2(ratio >= 4.5, qPrintable(QStringLiteral("%1 group %2 role %3: %4:1")
                    .arg(theme).arg(int(group)).arg(int(pair.first)).arg(ratio, 0, 'f', 3)));
            }
        QVERIFY(contrast(palette.color(QPalette::PlaceholderText), palette.color(QPalette::Base)) >= 4.5);
        QVERIFY(contrast(palette.color(QPalette::ToolTipText), palette.color(QPalette::ToolTipBase)) >= 4.5);
    }
    void informationValuesAreLiteralAndKeyboardSelectable() {
        ix::ImageDocument document;
        document.pixels = QImage(64, 48, QImage::Format_RGB32); document.pixels.fill(Qt::blue);
        document.path = QStringLiteral("/photos/<b>literal-name.png");
        probe([&] { ix::imageInformation(nullptr, document); }, [&](QDialog *dialog) {
            QLabel *value = nullptr;
            for (auto *label : dialog->findChildren<QLabel *>())
                if (label->text() == document.path) value = label;
            QVERIFY(value);
            QCOMPARE(value->textFormat(), Qt::PlainText);
            QVERIFY(value->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard));
            auto *metadata = dialog->findChild<QPlainTextEdit *>();
            QVERIFY(metadata && !metadata->accessibleName().isEmpty());
            capture(dialog, QStringLiteral("information"));
        });
    }
    void histogramFitsSmallWindows_data() { variants(); }
    void histogramFitsSmallWindows() {
        QFETCH(QString, theme); QFETCH(QString, language);
        ix::applyInterfaceLanguage(language); ix::applyInterfaceTheme(theme);
        QImage image(256, 64, QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x, 255 - x, (x * 13) % 256));
        const auto key = image.cacheKey();
        probe([&] { ix::histogramDialog(nullptr, image); }, [&](QDialog *dialog) {
            dialog->resize(560, 360); QCoreApplication::processEvents();
            capture(dialog, QStringLiteral("histogram-%1-%2").arg(theme, language));
            QVERIFY2(dialog->width() <= 560, "Histogram forces a wider window than requested");
            auto *chart = dialog->findChild<QWidget *>(QStringLiteral("histogramChart"));
            QVERIFY(chart && !chart->accessibleName().isEmpty());
            QVERIFY(chart->width() >= 250);
            auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
            QVERIFY(dialog->rect().contains(QRect(buttons->mapTo(dialog, QPoint()), buttons->size())));
        });
        QCOMPARE(image.cacheKey(), key);
    }
    void extensionSearchRetainsRowsAndClearsHiddenSelection() {
#ifndef IX_TEST_EXTENSION_MANAGER
        QSKIP("Optional extension-manager module not built");
#else
        probe([&] { ix::extensionManagerDialog(nullptr); }, [&](QDialog *dialog) {
            auto *tabs=dialog->findChild<QTabWidget *>(QStringLiteral("extensionSections")); QVERIFY(tabs);
            tabs->setCurrentIndex(1);
            auto *search=dialog->findChild<QLineEdit *>(QStringLiteral("extensionSearch")); QVERIFY(search);
            auto *table=dialog->findChild<QTableWidget *>(QStringLiteral("functionExtensionTable")); QVERIFY(table);
            const int count=table->rowCount(); QVERIFY(count>0);
            auto *first=table->item(0,0); QVERIFY(first); table->setCurrentCell(0,0);
            search->setText(QStringLiteral("no-such-package-92f65be8"));
            QCOMPARE(table->rowCount(),count); QCOMPARE(table->item(0,0),first);
            QVERIFY(table->isRowHidden(0)); QCOMPARE(table->currentRow(),-1);
            auto *toggle=dialog->findChild<QPushButton *>(QStringLiteral("toggleExtension")); QVERIFY(toggle);
            QVERIFY(!toggle->isEnabled());
            search->clear();
            QCOMPARE(table->item(0,0),first); QVERIFY(!table->isRowHidden(0));
        });
#endif
    }
    void extensionActionsFitSmallWindows_data() { variants(); }
    void extensionActionsFitSmallWindows() {
#ifndef IX_TEST_EXTENSION_MANAGER
        QSKIP("Optional extension-manager module not built");
#else
        QFETCH(QString, theme); QFETCH(QString, language);
        ix::applyInterfaceLanguage(language); ix::applyInterfaceTheme(theme);
        probe([&] { ix::extensionManagerDialog(nullptr); }, [&](QDialog *dialog) {
            auto *tabs = dialog->findChild<QTabWidget *>(QStringLiteral("extensionSections"));
            QVERIFY(tabs); tabs->setCurrentIndex(1);
            dialog->resize(720, 500); QCoreApplication::processEvents();
            capture(dialog, QStringLiteral("extensions-%1-%2").arg(theme, language));
            QVERIFY2(dialog->width() <= 720, "Extension actions force an oversized dialog");
            for (const auto *name : {"installLocalExtension", "installCatalogExtension", "toggleExtension"}) {
                auto *button = dialog->findChild<QPushButton *>(QString::fromLatin1(name));
                QVERIFY(button && dialog->rect().contains(QRect(button->mapTo(dialog, QPoint()), button->size())));
            }
            auto *url = dialog->findChild<QLineEdit *>(QStringLiteral("extensionCatalogUrl"));
            QVERIFY(url); url->setFocus(); QTest::keyClick(url, Qt::Key_Tab);
            QVERIFY(dialog->focusWidget() && dialog->focusWidget() != url);
            QTest::keyClick(dialog, Qt::Key_Escape);
            QVERIFY(!dialog->isVisible());
        });
#endif
    }
    void editingFieldsHaveStableAccessibleNames() {
        ix::applyInterfaceLanguage(QStringLiteral("de"));
        ix::MainWindow window;
        auto *zoom = window.findChild<QComboBox *>(QStringLiteral("zoom"));
        QVERIFY(zoom && !zoom->accessibleName().isEmpty());
        ix::PipelineDialog pipeline({});
        for (const auto *name : {"pipelineSearch", "pipelineFormat", "pipelineInputs", "pipelineChain",
                                 "pipelineOutput", "pipelinePattern"}) {
            auto *field = pipeline.findChild<QWidget *>(QString::fromLatin1(name));
            QVERIFY(field);
            auto *accessible = QAccessible::queryAccessibleInterface(field);
            QVERIFY2(accessible && !accessible->text(QAccessible::Name).isEmpty(), name);
        }
    }
    void statusTipsNeverReplaceImageInformation() {
        ix::MainWindow window;
        window.show();
        const auto path = temporary_.filePath(QStringLiteral("status-image.png"));
        QImage pixels(32, 24, QImage::Format_RGB32); pixels.fill(Qt::blue);
        QVERIFY(pixels.save(path)); window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && window.document().path == path);
        auto *bar = window.statusBar();
        const auto labels = bar->findChildren<QLabel *>();
        QStringList before;
        for (auto *label : labels) before.append(label->text());
        QSignalSpy messages(bar, &QStatusBar::messageChanged);
        for (const auto &tip : {QStringLiteral("Open image"), QString(), QStringLiteral("Resize")}) {
            QStatusTipEvent event(tip);
            QApplication::sendEvent(&window, &event);
            QVERIFY(bar->currentMessage().isEmpty());
        }
        QCOMPARE(messages.count(), 0);
        for (int i = 0; i < labels.size(); ++i) {
            QCOMPARE(labels[i]->text(), before[i]);
            QVERIFY(!labels[i]->isHidden());
        }
    }
    void mixedDirectoriesSkipUnsupportedFiles() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (const auto *name : {"one.png", "two.JPG", "clip.mp4", "notes.txt", "archive.zip", "fake.png"}) {
            QFile file(directory.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture");
        }
        QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("subfolder.png"))));
        const auto result = ix::scanDirectory(directory.path(), {QStringLiteral("png"), QStringLiteral("jpg")}, 0, false);
        QCOMPARE(result.files.size(), 3);
        QVERIFY(result.files.contains(directory.filePath(QStringLiteral("fake.png"))));
        // Indexing checks suffixes only. Corrupt supported files are resolved
        // later by the configured unreadable-file navigation policy.
        for (const auto &path : result.files)
            QVERIFY(QStringList({QStringLiteral("png"), QStringLiteral("jpg")}).contains(QFileInfo(path).suffix().toLower()));
    }
};
QTEST_MAIN(UiQualityTests)
#include "ui_quality_ixview.moc"
