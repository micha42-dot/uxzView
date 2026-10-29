// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/codec_registry.h"
#include "core/directory_index_service.h"
#include "core/batch_pipeline.h"
#include "core/batch_report.h"
#include "core/file_operations.h"
#include "core/resize_model.h"
#include "core/image_loading.h"
#include "core/image_catalog.h"
#include "core/localization.h"
#include "core/preview_service.h"
#include "ui/main_window.h"
#include "ui/appearance.h"
#include "ui/interface_language.h"
#include "ui/dialogs.h"
#include "ui/action_search.h"
#include "ui/library_dialog.h"
#include "ui/preview_strip.h"
#include "ui/platform_support.h"
#include "ui/pipeline_dialog.h"
#include "ui/pipeline_result_model.h"
#include "ui/batch_progress.h"
#include <QAbstractItemModelTester>
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
    void menuCheckmarksMatchForExclusiveAndIndependentChoices_data() {
        QTest::addColumn<QString>("theme");
        QTest::newRow("light") << QStringLiteral("light");
        QTest::newRow("dark") << QStringLiteral("dark");
    }
    void menuCheckmarksMatchForExclusiveAndIndependentChoices() {
        QFETCH(QString, theme);
        const auto palette = QApplication::palette();
        const auto previous = QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
        const auto restore = qScopeGuard([&] {
            ix::applyInterfaceTheme(previous);
            QApplication::setPalette(palette);
        });
        ix::applyInterfaceTheme(theme);
        QMenu menu;
        for (bool enabled : {false, true}) for (bool selected : {false, true}) {
            auto render = [&](QStyleOptionMenuItem::CheckType type) {
                QImage image(240, 30, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                QStyleOptionMenuItem option;
                option.initFrom(&menu);
                option.rect = image.rect();
                option.text = QStringLiteral("Display option");
                option.menuItemType = QStyleOptionMenuItem::Normal;
                option.menuHasCheckableItems = true;
                option.checkType = type;
                option.checked = true;
                option.state.setFlag(QStyle::State_Enabled, enabled);
                option.state.setFlag(QStyle::State_Selected, selected);
                QPainter painter(&image);
                menu.style()->drawControl(QStyle::CE_MenuItem, &option, &painter, &menu);
                return image;
            };
            QCOMPARE(render(QStyleOptionMenuItem::Exclusive), render(QStyleOptionMenuItem::NonExclusive));
        }
    }
    void displayFitMenuSelectionPersists() {
        QSettings settings;
        QVariantMap saved;
        for (const auto &key : settings.allKeys()) saved.insert(key, settings.value(key));
        const auto restore = qScopeGuard([&] {
            settings.clear();
            for (auto i = saved.cbegin(); i != saved.cend(); ++i) settings.setValue(i.key(), i.value());
        });
        settings.setValue(QStringLiteral("view/fit"), int(ix::ImageView::Fit::LargeOnly));
        settings.remove(QStringLiteral("view/fitCommand"));
        const auto path = photo(QStringLiteral("fit-menu/image.png"), Qt::green);
        ix::MainWindow window;
        window.resize(983, 617);
        window.show();
        window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && window.document().path == path);
        auto *menu = window.findChild<QMenu *>(QStringLiteral("anzeige-optionen"));
        QVERIFY(menu);
        auto *choices = menu->findChild<QActionGroup *>(QStringLiteral("windowFitChoices"));
        QVERIFY(choices && choices->isExclusive());
        QCOMPARE(choices->actions().size(), 12);
        auto selected = [&] {
            QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
            int count = 0;
            QString command;
            for (auto *action : choices->actions()) if (action->isChecked()) {
                ++count;
                command = action->data().toString();
            }
            return count == 1 ? command : QString();
        };
        QCOMPARE(selected(), QStringLiteral("fitScreenLarge"));
        auto *view = window.findChild<ix::ImageView *>();
        QSignalSpy zoomed(view, &ix::ImageView::zoomChanged);
        const auto pixels = view->image().cacheKey();
        selected();
        QVERIFY(zoomed.isEmpty());
        QCOMPARE(view->image().cacheKey(), pixels);
        menu->popup(window.mapToGlobal(QPoint(24, 80)));
        QTRY_VERIFY(menu->isVisible());
        QVERIFY(menu->grab().save(QStringLiteral(IX_WORKFLOW_OUTPUT "/display-fit-menu.png")));
        menu->hide();
        const QStringList commands{QStringLiteral("fitOriginal"), QStringLiteral("fitWindow"),
            QStringLiteral("fitLarge"), QStringLiteral("fitWidth"), QStringLiteral("fitHeight"),
            QStringLiteral("fitShort"), QStringLiteral("fitScreen"), QStringLiteral("fitScreenLarge"),
            QStringLiteral("fitScreenWidth"), QStringLiteral("fitScreenHeight"),
            QStringLiteral("fitScreenShort"), QStringLiteral("noFit")};
        for (const auto &command : commands) {
            auto *action = window.findChild<QAction *>(command);
            QVERIFY(action && action->isEnabled() && action->isCheckable());
            action->trigger();
            QCOMPARE(selected(), command);
            action->trigger(); // a selected radio item cannot uncheck itself
            QCOMPARE(selected(), command);
        }
        window.dispatch(QStringLiteral("fitScreenWidth"));
        QCOMPARE(settings.value(QStringLiteral("view/fitCommand")).toString(), QStringLiteral("fitScreenWidth"));
        ix::MainWindow reopened;
        auto *reopenedChoices = reopened.findChild<QActionGroup *>(QStringLiteral("windowFitChoices"));
        QVERIFY(reopenedChoices && reopenedChoices->checkedAction());
        QCOMPARE(reopenedChoices->checkedAction()->data().toString(), QStringLiteral("fitScreenWidth"));
        // A stale/unknown alias must not overrule the actual stored fit policy.
        settings.setValue(QStringLiteral("view/fit"), int(ix::ImageView::Fit::Height));
        ix::MainWindow mismatched;
        QCOMPARE(mismatched.findChild<QActionGroup *>(QStringLiteral("windowFitChoices"))->checkedAction()->data().toString(),
                 QStringLiteral("fitHeight"));
    }
    void displayFitMenuTracksZoomFullscreenAndBlockedActions() {
        QSettings settings;
        QVariantMap saved;
        for (const auto &key : settings.allKeys()) saved.insert(key, settings.value(key));
        const auto restore = qScopeGuard([&] {
            settings.clear();
            for (auto i = saved.cbegin(); i != saved.cend(); ++i) settings.setValue(i.key(), i.value());
        });
        settings.setValue(QStringLiteral("view/fit"), int(ix::ImageView::Fit::LargeOnly));
        settings.setValue(QStringLiteral("view/fitCommand"), QStringLiteral("fitLarge"));
        settings.setValue(QStringLiteral("view/lockZoom"), false);
        settings.setValue(QStringLiteral("fullscreen/fit"), int(ix::ImageView::Fit::Width));
        const auto first = photo(QStringLiteral("fit-menu-state/1.png"), Qt::blue);
        const auto second = photo(QStringLiteral("fit-menu-state/2.png"), Qt::red);
        ix::MainWindow window;
        window.show();
        window.openPath(first);
        QTRY_VERIFY(!window.isBusy() && window.document().path == first);
        auto *view = window.findChild<ix::ImageView *>();
        auto *menu = window.findChild<QMenu *>(QStringLiteral("anzeige-optionen"));
        auto *choices = menu->findChild<QActionGroup *>(QStringLiteral("windowFitChoices"));
        auto selected = [&] {
            QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
            return choices->checkedAction()->data().toString();
        };
        view->setZoom(2);
        QCOMPARE(selected(), QStringLiteral("noFit"));
        QCOMPARE(settings.value(QStringLiteral("view/fitCommand")).toString(), QStringLiteral("fitLarge"));
        window.openPath(second);
        QTRY_VERIFY(!window.isBusy() && window.document().path == second);
        QCOMPARE(selected(), QStringLiteral("fitLarge"));
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(window.isFullScreen());
        QCOMPARE(view->fit(), ix::ImageView::Fit::Width);
        QCOMPARE(selected(), QStringLiteral("fitLarge")); // menu describes the window policy
        window.findChild<QAction *>(QStringLiteral("fitHeight"))->trigger();
        QCOMPARE(selected(), QStringLiteral("fitHeight"));
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(!window.isFullScreen());
        QCOMPARE(view->fit(), ix::ImageView::Fit::Height);
        QCOMPARE(settings.value(QStringLiteral("fullscreen/fit")).toInt(), int(ix::ImageView::Fit::Width));
        window.openPath(first);
        QVERIFY(window.isBusy());
        window.findChild<QAction *>(QStringLiteral("fitWindow"))->trigger();
        QCOMPARE(choices->checkedAction()->data().toString(), QStringLiteral("fitHeight"));
        QTRY_VERIFY(!window.isBusy() && window.document().path == first);
        QCOMPARE(view->fit(), ix::ImageView::Fit::Height);
        QDialog blocker(&window);
        blocker.setWindowModality(Qt::ApplicationModal);
        blocker.show();
        QTRY_COMPARE(QApplication::activeModalWidget(), &blocker);
        window.findChild<QAction *>(QStringLiteral("fitShort"))->trigger();
        QCOMPARE(choices->checkedAction()->data().toString(), QStringLiteral("fitHeight"));
        QCOMPARE(view->fit(), ix::ImageView::Fit::Height);
        blocker.reject();
    }
    void windowFitChangedWhileFullscreenAppliesOnExit_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("landscape-3331x2244") << QSize(3331, 2244);
        QTest::newRow("portrait-2594x3486") << QSize(2594, 3486);
    }
    void windowFitChangedWhileFullscreenAppliesOnExit() {
        QFETCH(QSize, size);
        QSettings settings;
        QVariantMap saved;
        for (const auto &key : settings.allKeys()) saved.insert(key, settings.value(key));
        const auto restore = qScopeGuard([&] {
            settings.clear();
            for (auto i = saved.cbegin(); i != saved.cend(); ++i) settings.setValue(i.key(), i.value());
        });
        settings.setValue(QStringLiteral("view/fit"), int(ix::ImageView::Fit::Original));
        settings.setValue(QStringLiteral("fullscreen/fit"), int(ix::ImageView::Fit::Window));
        settings.setValue(QStringLiteral("view/lockZoom"), false);
        settings.setValue(QStringLiteral("view/previewStrip"), true);
        settings.setValue(QStringLiteral("window/rememberGeometry"), false);
        const auto folder = temporary_.filePath(QString::fromLatin1(QTest::currentDataTag()));
        QVERIFY(QDir().mkpath(folder));
        const auto path = folder + QStringLiteral("/1.jpg"), next = folder + QStringLiteral("/2.jpg");
        QImage image(size, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(path));
        QVERIFY(image.save(next));
        ix::MainWindow window;
        window.resize(983, 617);
        window.show();
        window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path == path, 15000);
        auto *view = window.findChild<ix::ImageView *>();
        QVERIFY(view);
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(window.isFullScreen());
        bool changed = false;
        QTimer::singleShot(30, qApp, [&] {
            auto *dialog = qobject_cast<ix::SettingsDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *fit = dialog->findChild<QComboBox *>(QStringLiteral("view/fit"));
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (fit && buttons) {
                fit->setCurrentIndex(int(ix::ImageView::Fit::LargeOnly));
                changed = true;
                buttons->button(QDialogButtonBox::Ok)->click();
            } else dialog->reject();
        });
        window.dispatch(QStringLiteral("settings"));
        QVERIFY(changed);
        QCOMPARE(view->fit(), ix::ImageView::Fit::Window); // fullscreen policy retained
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(!window.isFullScreen());
        QCOMPARE(view->fit(), ix::ImageView::Fit::LargeOnly);
        QTRY_COMPARE(view->visibleImageRect(), image.rect());
        QCOMPARE(view->horizontalScrollBar()->maximum(), 0);
        QCOMPARE(view->verticalScrollBar()->maximum(), 0);
        QTest::keyClick(view, Qt::Key_Right);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path == next, 15000);
        const auto center = QPointF(view->viewport()->rect().center());
        QWheelEvent previous(center, view->viewport()->mapToGlobal(center.toPoint()), {},
            QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view->viewport(), &previous);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path == path, 15000);
    }
    void closingOlderWindowKeepsNewestFitPreference() {
        QSettings settings;
        const auto previous = settings.value(QStringLiteral("view/fit"));
        const auto restore = qScopeGuard([&] {
            if (previous.isValid()) settings.setValue(QStringLiteral("view/fit"), previous);
            else settings.remove(QStringLiteral("view/fit"));
        });
        settings.setValue(QStringLiteral("view/fit"), int(ix::ImageView::Fit::Original));
        const auto path = photo(QStringLiteral("older-window-fit.png"), Qt::green);
        ix::MainWindow older, newer;
        older.openPath(path);
        newer.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!older.isBusy() && older.document().path == path, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!newer.isBusy() && newer.document().path == path, 15000);
        newer.dispatch(QStringLiteral("fitLarge"));
        QCOMPARE(settings.value(QStringLiteral("view/fit")).toInt(), int(ix::ImageView::Fit::LargeOnly));
        QVERIFY(older.close());
        QCOMPARE(settings.value(QStringLiteral("view/fit")).toInt(), int(ix::ImageView::Fit::LargeOnly));
        ix::MainWindow reopened;
        QCOMPARE(reopened.findChild<ix::ImageView *>()->browsingFit(), ix::ImageView::Fit::LargeOnly);
        reopened.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!reopened.isBusy() && reopened.document().path == path, 15000);
        // Accepting the displayed preference must also update a still-open
        // stale window without requiring a change to the combo's current index.
        reopened.dispatch(QStringLiteral("original"));
        newer.dispatch(QStringLiteral("fitLarge"));
        bool accepted = false;
        QTimer::singleShot(30, qApp, [&] {
            auto *dialog = qobject_cast<ix::SettingsDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            accepted = dialog->findChild<QComboBox *>(QStringLiteral("view/fit"))->currentIndex() ==
                int(ix::ImageView::Fit::LargeOnly);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        reopened.dispatch(QStringLiteral("settings"));
        QVERIFY(accepted);
        QCOMPARE(reopened.findChild<ix::ImageView *>()->browsingFit(), ix::ImageView::Fit::LargeOnly);
    }
    void automaticFitSurvivesLockedZoomDuringFolderNavigation_data() {
        QTest::addColumn<int>("fit");
        QTest::addColumn<bool>("locked");
        QTest::newRow("large-only") << int(ix::ImageView::Fit::LargeOnly) << false;
        QTest::newRow("large-only-locked") << int(ix::ImageView::Fit::LargeOnly) << true;
        QTest::newRow("window") << int(ix::ImageView::Fit::Window) << false;
        QTest::newRow("window-locked") << int(ix::ImageView::Fit::Window) << true;
    }
    void automaticFitSurvivesLockedZoomDuringFolderNavigation() {
        QFETCH(int, fit);
        QFETCH(bool, locked);
        QSettings settings;
        const QStringList keys{QStringLiteral("view/fit"), QStringLiteral("view/lockZoom"),
            QStringLiteral("view/lockScroll"), QStringLiteral("view/previewStrip"),
            QStringLiteral("navigation/wrap"), QStringLiteral("navigation/sort"),
            QStringLiteral("navigation/reverse")};
        QVariantMap saved;
        for (const auto &key : keys) saved.insert(key, settings.value(key));
        const auto restore = qScopeGuard([&] {
            for (const auto &key : keys)
                if (saved[key].isValid()) settings.setValue(key, saved[key]);
                else settings.remove(key);
        });
        settings.setValue(QStringLiteral("view/fit"), fit);
        settings.setValue(QStringLiteral("view/lockZoom"), false);
        settings.setValue(QStringLiteral("view/lockScroll"), false);
        settings.setValue(QStringLiteral("view/previewStrip"), false);
        settings.setValue(QStringLiteral("navigation/wrap"), true);
        settings.setValue(QStringLiteral("navigation/sort"), 0);
        settings.setValue(QStringLiteral("navigation/reverse"), false);
        const auto folder = temporary_.filePath(QStringLiteral("fit-navigation-%1-%2").arg(fit).arg(locked));
        QVERIFY(QDir().mkpath(folder));
        const auto first = folder + QStringLiteral("/1.png"), second = folder + QStringLiteral("/2.png");
        QImage landscape(1601, 1001, QImage::Format_RGB32);
        landscape.fill(Qt::green);
        QVERIFY(landscape.save(first));
        QImage panorama(20003, 1003, QImage::Format_RGB32);
        panorama.fill(Qt::blue);
        QVERIFY(panorama.save(second));
        ix::MainWindow window;
        window.resize(983, 617);
        window.show();
        window.openPath(first);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path == first, 15000);
        auto *view = window.findChild<ix::ImageView *>();
        QVERIFY(view);
        QTRY_COMPARE(view->visibleImageRect(), landscape.rect());
        if (locked) window.findChild<QAction *>(QStringLiteral("lockZoom"))->trigger();
        for (const bool previews : {false, true}) {
            window.findChild<QAction *>(QStringLiteral("previewStripToggle"))->setChecked(previews);
            for (const auto size : {QSize(983, 617), QSize(801, 603)}) {
                window.resize(size);
                view->setFocus();
                QTest::keyClick(view, Qt::Key_Right);
                QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path == second, 15000);
                QCOMPARE(view->fit(), ix::ImageView::Fit(fit));
                QTRY_COMPARE(view->visibleImageRect(), panorama.rect());
                QCOMPARE(view->horizontalScrollBar()->maximum(), 0);
                QCOMPARE(view->verticalScrollBar()->maximum(), 0);
                QVERIFY(panorama.width() * view->zoom() <= view->viewport()->width() + 1e-7);
                QVERIFY(panorama.height() * view->zoom() <= view->viewport()->height() + 1e-7);
                const auto center = QPointF(view->viewport()->rect().center());
                QWheelEvent previous(center, view->viewport()->mapToGlobal(center.toPoint()), {},
                    QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(view->viewport(), &previous);
                QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path == first, 15000);
            }
        }
        // Lock still preserves a deliberately chosen detail zoom.
        if (!locked) window.findChild<QAction *>(QStringLiteral("lockZoom"))->trigger();
        view->setZoom(2.0);
        window.openPath(second);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy() && window.document().path == second, 15000);
        QCOMPARE(view->fit(), ix::ImageView::Fit::Original);
        QCOMPARE(view->zoom(), 2.0);
        QVERIFY(view->horizontalScrollBar()->maximum() > 0);
        // A saved lock also must not force 100% on the first image after startup.
        settings.setValue(QStringLiteral("view/fit"), fit);
        ix::MainWindow fresh;
        fresh.resize(801, 603);
        fresh.show();
        fresh.openPath(second);
        QTRY_VERIFY_WITH_TIMEOUT(!fresh.isBusy() && fresh.document().path == second, 15000);
        auto *freshView = fresh.findChild<ix::ImageView *>();
        QVERIFY(freshView);
        QCOMPARE(freshView->fit(), ix::ImageView::Fit(fit));
        QTRY_COMPARE(freshView->visibleImageRect(), panorama.rect());
        QCOMPARE(freshView->horizontalScrollBar()->maximum(), 0);
        QCOMPARE(freshView->verticalScrollBar()->maximum(), 0);
    }
    void batchProgressCoalescesBurstsWithoutLosingLastUpdate() {
        ix::BatchProgress progress(100000);
        int notifications = 0;
        for (int i = 1; i <= 99999; ++i) notifications += progress.publish(i);
        QCOMPARE(notifications, 1);
        QCOMPARE(progress.take(), 99999);
        QVERIFY(!progress.publish(90000)); // late parallel completion
        QVERIFY(progress.publish(100000));
        QCOMPARE(progress.take(), 100000);
        QVERIFY(!progress.publish(100000));
        QCOMPARE(progress.total(), 100000);
    }
    void batchProgressMergesConcurrentWorkers() {
        ix::BatchProgress progress(100000);
        auto first = QtConcurrent::run([&] {
            int scheduled = 0;
            for (int i = 1; i <= 100000; i += 2) scheduled += progress.publish(i);
            return scheduled;
        });
        auto second = QtConcurrent::run([&] {
            int scheduled = 0;
            for (int i = 2; i <= 100000; i += 2) scheduled += progress.publish(i);
            return scheduled;
        });
        QCOMPARE(first.result() + second.result(), 1);
        QCOMPARE(progress.take(), 100000);
        QVERIFY(!progress.publish(1));
    }

    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewWorkflowTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
        ix::applyInterfaceTheme(QStringLiteral("system"));
    }
    void interfaceThemePreservesImageAndNavigationState() {
        QSettings settings;
        QVariantMap saved;
        for (const auto &key : settings.allKeys()) saved.insert(key, settings.value(key));
        const auto palette = QApplication::palette();
        const auto restore = qScopeGuard([&] {
            settings.clear();
            for (auto it = saved.cbegin(); it != saved.cend(); ++it) settings.setValue(it.key(), it.value());
            ix::applyInterfaceTheme(settings.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString());
            QApplication::setPalette(palette);
        });
        settings.setValue(QStringLiteral("ui/theme"), QStringLiteral("light"));
        settings.setValue(QStringLiteral("view/previewStrip"), true);
        settings.setValue(QStringLiteral("preview/background"), QStringLiteral("theme"));
        const auto path = photo(QStringLiteral("appearance/image.png"), Qt::green);
        ix::MainWindow window;
        window.resize(980, 660);
        window.show();
        window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && window.document().path == path);
        auto *view = window.findChild<ix::ImageView *>();
        auto *toolbar = window.findChild<QToolBar *>(QStringLiteral("mainToolbar"));
        auto *index = window.findChild<ix::DirectoryIndexService *>();
        QVERIFY(view && toolbar && index);
        QTRY_VERIFY_WITH_TIMEOUT(!index->isBusy(), 15000);
        view->setZoom(2.5);
        view->setSelection(QRect(3, 4, 20, 13));
        const auto pixels = window.document().pixels.cacheKey(), display = view->image().cacheKey();
        const auto revision = window.document().sourceRevision;
        const auto selection = view->selection();
        QSignalSpy reindexed(index, &ix::DirectoryIndexService::ready);
        QSignalSpy zoomed(view, &ix::ImageView::zoomChanged);
        for (const auto &mode : {QStringLiteral("dark"), QStringLiteral("light"), QStringLiteral("system")}) {
            bool handled = false;
            QTimer::singleShot(20, &window, [&] {
                auto *dialog = qobject_cast<ix::SettingsDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                auto *combo = dialog->findChild<QComboBox *>(QStringLiteral("ui/theme"));
                if (!combo) { dialog->reject(); return; }
                combo->setCurrentIndex(combo->findData(mode));
                handled = true;
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            });
            window.dispatch(QStringLiteral("settings"));
            QVERIFY(handled);
            QCoreApplication::processEvents();
            QCOMPARE(settings.value(QStringLiteral("ui/theme")).toString(), mode);
            if (mode != QStringLiteral("system"))
                QCOMPARE(window.palette().color(QPalette::Window).lightnessF() < .5, mode == QStringLiteral("dark"));
            QCOMPARE(window.document().pixels.cacheKey(), pixels);
            QCOMPARE(view->image().cacheKey(), display);
            QCOMPARE(window.document().sourceRevision, revision);
            QCOMPARE(view->selection(), selection);
            QCOMPARE(view->zoom(), 2.5);
            QCOMPARE(view->background, QColor(Qt::black));
            QVERIFY(!window.isBusy());
            const auto background = window.palette().color(QPalette::Window);
            const auto menuPixels = window.menuBar()->grab().toImage(), toolbarPixels = toolbar->grab().toImage();
            QCOMPARE(menuPixels.pixelColor(menuPixels.width() - 5, 2), background);
            QCOMPARE(toolbarPixels.pixelColor(toolbarPixels.width() - 5, 2), background);
            for (auto *label : window.statusBar()->findChildren<QLabel *>())
                QCOMPARE(label->palette().color(QPalette::WindowText), window.palette().color(QPalette::WindowText));
            QCOMPARE(window.findChild<ix::PreviewStrip *>()->palette().color(QPalette::Window), background);
            QVERIFY(window.grab().save(QStringLiteral(IX_WORKFLOW_OUTPUT "/interface-%1.png").arg(mode)));
        }
        QCOMPARE(reindexed.size(), 0);
        QCOMPARE(zoomed.size(), 0);
        ix::SettingsDialog cancelled;
        auto *combo = cancelled.findChild<QComboBox *>(QStringLiteral("ui/theme"));
        QVERIFY(combo);
        combo->setCurrentIndex(combo->findData(QStringLiteral("dark")));
        cancelled.reject();
        QCOMPARE(settings.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("system"));
    }
    void liveLanguageChangePreservesDocument_data() {
        QTest::addColumn<bool>("fullscreen");
        QTest::newRow("window") << false;
        QTest::newRow("fullscreen") << true;
    }
    void liveLanguageChangePreservesDocument() {
        QFETCH(bool, fullscreen);
        QSettings settings;
        QVariantMap saved;
        for (const auto &key : settings.allKeys()) saved.insert(key, settings.value(key));
        const auto oldLanguage = ix::uiLanguage();
        const auto oldFont = QApplication::font();
        const auto defaultLocale = QLocale().name();
        const auto restore = qScopeGuard([&] {
            settings.clear();
            for (auto it = saved.cbegin(); it != saved.cend(); ++it) settings.setValue(it.key(), it.value());
            ix::applyInterfaceLanguage(oldLanguage);
            QApplication::setFont(oldFont);
        });
        QVERIFY(ix::applyInterfaceLanguage(QStringLiteral("de")));
        settings.setValue(QStringLiteral("ui/language"), QStringLiteral("de"));
        settings.setValue(QStringLiteral("view/actionSearch"), true);
        settings.setValue(QStringLiteral("view/previewStrip"), true);
        const auto path = photo(QStringLiteral("live-language/image.png"), QColor(30, 100, 210));
        const auto sourceHash = ix::sha256File(path);
        ix::MainWindow window;
        window.show();
        window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && window.document().path == path);
        const auto original = window.document().pixels;
        window.dispatch(QStringLiteral("grayscale"));
        QTRY_VERIFY(!window.isBusy() && window.document().modified);
        QTest::qWait(150);
        if (fullscreen) { window.dispatch(QStringLiteral("fullscreen")); QTRY_VERIFY(window.isFullScreen()); }
        auto *view = window.findChild<ix::ImageView *>();
        auto *search = window.findChild<ix::ActionSearch *>();
        auto *resize = window.findChild<QAction *>(QStringLiteral("resize"));
        auto *toolbar = window.findChild<QToolBar *>(QStringLiteral("mainToolbar"));
        QVERIFY(view && search && resize && toolbar);
        view->setZoom(16);
        view->setSelection(QRect(3, 4, 20, 13));
        view->horizontalScrollBar()->setValue(31);
        view->verticalScrollBar()->setValue(29);
        const auto scroll = QPoint(view->horizontalScrollBar()->value(), view->verticalScrollBar()->value());
        const auto pixels = window.document().pixels.cacheKey(), display = view->image().cacheKey();
        const auto selection = view->selection();
        const auto revision = window.document().sourceRevision;
        const auto shortcuts = resize->shortcuts();
        const auto actions = window.findChildren<QAction *>();
        // A retained modeless message uses the same formatted binding as About.
        QLabel formatted(&window);
        ix::bindUiText(&formatted, "text", QStringLiteral("Hintergrundfarbe: %1 …"), {}, {}, {QStringLiteral("#000000")});
        search->setText(QStringLiteral("dpi"));
        auto *model = search->completer()->model();
        QPersistentModelIndex row;
        for (int i = 0; i < model->rowCount(); ++i)
            if (model->index(i, 0).data(Qt::UserRole).value<QObject *>() == resize) row = model->index(i, 0);
        QVERIFY(row.isValid());
        QSignalSpy rescanned(window.findChild<ix::DirectoryIndexService *>(), &ix::DirectoryIndexService::ready);
        for (const auto &code : {QStringLiteral("en"), QStringLiteral("ko"), QStringLiteral("ja"),
                                 QStringLiteral("fr"), QStringLiteral("de")}) {
            bool handled = false;
            QTimer::singleShot(20, &window, [&] {
                auto *dialog = qobject_cast<ix::SettingsDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                auto *combo = dialog->findChild<QComboBox *>(QStringLiteral("ui/language"));
                combo->setCurrentIndex(combo->findData(code));
                handled = true;
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            });
            window.dispatch(QStringLiteral("settings"));
            QVERIFY(handled);
            QCoreApplication::processEvents();
            QCOMPARE(ix::uiLanguage(), code);
            QCOMPARE(window.locale(), ix::uiLocale());
            QCOMPARE(QLocale().name(), defaultLocale); // no process-locale race with workers
            QCOMPARE(resize->text(), ix::localized(QStringLiteral("Größe ändern...")));
            QCOMPARE(window.findChild<QMenu *>(QStringLiteral("datei"))->title(), ix::localized(QStringLiteral("&Datei")));
            QCOMPARE(window.findChild<QMenu *>(QStringLiteral("bild"))->title(), ix::localized(QStringLiteral("B&ild")));
            if (code == QStringLiteral("en"))
                QCOMPARE(window.findChild<QMenu *>(QStringLiteral("bild"))->title(), QStringLiteral("&Image"));
            QVERIFY(!window.findChild<QAction *>(QStringLiteral("fileJournal")));
            QVERIFY(!window.findChild<QAction *>(QStringLiteral("fileUndo")));
            QCOMPARE(search->placeholderText(), ix::localized(QStringLiteral("Befehl suchen …")));
            QCOMPARE(formatted.text(), ix::localized(QStringLiteral("Hintergrundfarbe: %1 …")).arg(QStringLiteral("#000000")));
            QCOMPARE(search->text(), QStringLiteral("dpi"));
            QVERIFY(row.isValid());
            auto label = resize->text(); label.remove(QLatin1Char('&'));
            QCOMPARE(row.data().toString(), label);
            QCOMPARE(window.findChildren<QAction *>(), actions);
            QCOMPARE(resize->shortcuts(), shortcuts);
            auto *open = window.findChild<QAction *>(QStringLiteral("open"));
            const auto description = ix::localized(QStringLiteral("Bilddatei öffnen"));
            QVERIFY(open->toolTip().startsWith(description));
            QCOMPARE(toolbar->widgetForAction(open)->accessibleName(), description);
            ix::SettingsDialog next;
            QCOMPARE(next.windowTitle(), ix::localized(QStringLiteral("Einstellungen")));
            QCOMPARE(next.findChild<QComboBox *>(QStringLiteral("ui/language"))->currentData().toString(), code);
            ix::ResizeDialog resizeDialog({400, 200}, 96);
            QCOMPARE(resizeDialog.findChild<QDoubleSpinBox *>(QStringLiteral("width"))->locale(), ix::uiLocale());
            QCOMPARE(window.document().pixels.cacheKey(), pixels);
            QCOMPARE(view->image().cacheKey(), display);
            QCOMPARE(view->selection(), selection);
            QCOMPARE(view->zoom(), 16.0);
            QCOMPARE(QPoint(view->horizontalScrollBar()->value(), view->verticalScrollBar()->value()), scroll);
            QCOMPARE(window.document().sourceRevision, revision);
            QCOMPARE(window.document().path, path);
            QVERIFY(window.document().modified && !window.isBusy());
            QCOMPARE(window.isFullScreen(), fullscreen);
            if (!fullscreen) QVERIFY(window.grab().save(QStringLiteral(IX_WORKFLOW_OUTPUT "/live-language-%1.png").arg(code)));
        }
        QCOMPARE(rescanned.size(), 0);
        QTimer::singleShot(20, &window, [&] {
            auto *dialog = qobject_cast<ix::SettingsDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog->findChild<QComboBox *>(QStringLiteral("ui/language"))->setCurrentIndex(1);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        });
        window.dispatch(QStringLiteral("settings"));
        QCOMPARE(ix::uiLanguage(), QStringLiteral("de"));
        QCOMPARE(settings.value(QStringLiteral("ui/language")).toString(), QStringLiteral("de"));
        QVERIFY(!ix::applyInterfaceLanguage(QStringLiteral("invalid")));
        QCOMPARE(ix::uiLanguage(), QStringLiteral("de"));
        window.dispatch(QStringLiteral("undo"));
        QTRY_VERIFY(!window.isBusy() && window.document().pixels == original);
        QCOMPARE(ix::sha256File(path), sourceHash);
        if (fullscreen) window.dispatch(QStringLiteral("fullscreen"));
    }
    void languageCatalogSupportsConcurrentReaders() {
        const auto old = ix::uiLanguage();
        const auto restore = qScopeGuard([&] { ix::setUiLanguage(old); });
        const QStringList codes{QStringLiteral("de"), QStringLiteral("en"), QStringLiteral("ko"),
                                QStringLiteral("ja"), QStringLiteral("fr")};
        QSet<QString> expected;
        for (const auto &code : codes) {
            QVERIFY(ix::setUiLanguage(code));
            expected.insert(ix::localized(QStringLiteral("&Datei")));
        }
        const auto locale = QLocale().name();
        std::atomic_bool stop = false, failed = false;
        std::atomic_int reads = 0;
        const auto read = [&] {
            while (!stop.load()) {
                if (!expected.contains(ix::localized(QStringLiteral("&Datei"))) ||
                    !codes.contains(ix::uiLanguage()) ||
                    ix::localized(QStringLiteral("untranslated sentinel")) != QStringLiteral("untranslated sentinel"))
                    failed.store(true);
                ++reads;
            }
        };
        auto first = QtConcurrent::run(read), second = QtConcurrent::run(read);
        const auto join = qScopeGuard([&] { stop.store(true); first.waitForFinished(); second.waitForFinished(); });
        QTRY_VERIFY(reads.load() >= 1000);
        for (int cycle = 0; cycle < 20; ++cycle)
            for (const auto &code : codes) { QVERIFY(ix::setUiLanguage(code)); QTest::qWait(1); }
        QVERIFY(!failed.load());
        QCOMPARE(QLocale().name(), locale);
    }
    void themedDialogsRemainUsable_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<QString>("language");
        for (const auto *mode : {"light", "dark"})
            for (const auto *language : {"de", "en", "ko", "ja", "fr"})
                QTest::newRow(qPrintable(QString::fromLatin1(mode) + QLatin1Char('-') + QString::fromLatin1(language)))
                    << QString::fromLatin1(mode) << QString::fromLatin1(language);
    }
    void themedDialogsRemainUsable() {
        QFETCH(QString, mode); QFETCH(QString, language);
        const auto oldLanguage = ix::uiLanguage();
        const auto oldPalette = QApplication::palette();
        const auto restore = qScopeGuard([&] {
            ix::setUiLanguage(oldLanguage);
            ix::applyInterfaceTheme(QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString());
            QApplication::setPalette(oldPalette);
        });
        QVERIFY(ix::setUiLanguage(language));
        ix::applyInterfaceTheme(mode);
        QImage image(640, 480, QImage::Format_RGB32); image.fill(Qt::darkCyan);
        ix::ResizeDialog resize(image.size(), 96);
        ix::ColorDialog colors(image);
        ix::RotationDialog rotation(image);
        ix::SettingsDialog settings;
        for (auto *dialog : {static_cast<QDialog *>(&resize), static_cast<QDialog *>(&colors),
                            static_cast<QDialog *>(&rotation), static_cast<QDialog *>(&settings)}) {
            if (language == QStringLiteral("de")) {
                if (dialog == &settings)
                    settings.findChild<QListWidget *>(QStringLiteral("settingsCategories"))->setCurrentRow(1);
                dialog->show();
                QCoreApplication::processEvents();
                QVERIFY(dialog->grab().save(QStringLiteral(IX_WORKFLOW_OUTPUT "/%1-%2.png")
                    .arg(dialog->objectName(), mode)));
                dialog->hide();
            }
            dialog->resize(dialog->width(), 480);
            dialog->show();
            QTest::qWait(10);
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(buttons);
            for (const auto role : {QDialogButtonBox::Ok, QDialogButtonBox::Cancel}) {
                auto *button = buttons->button(role);
                QVERIFY(button && button->isVisible());
                QVERIFY(dialog->rect().contains(QRect(button->mapTo(dialog, QPoint()), button->size())));
                QVERIFY(!button->visibleRegion().isEmpty());
            }
            QCOMPARE(dialog->palette().color(QPalette::Window), QApplication::palette().color(QPalette::Window));
            QVERIFY(dialog->grab().save(QStringLiteral(IX_WORKFLOW_OUTPUT "/%1-%2-%3.png")
                .arg(dialog->objectName().isEmpty() ? QStringLiteral("settings") : dialog->objectName(), mode, language)));
            dialog->hide();
        }
    }
    void colorLayoutKeepsAdjustmentSemantics() {
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(QColor(50, 100, 150));
        ix::ColorDialog dialog(image);
        dialog.show();
        QCoreApplication::processEvents();
        auto *contrast = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("contrast"));
        auto *saturation = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("saturation"));
        auto *gamma = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("gamma"));
        auto *original = dialog.findChild<QLabel *>(QStringLiteral("colorOriginal"));
        QVERIFY(contrast && saturation && gamma && original);
        contrast->setValue(255); saturation->setValue(-255); gamma->setValue(3.5);
        QCOMPARE(dialog.values().contrast, 100.0);
        QCOMPARE(dialog.values().saturation, -100.0);
        QCOMPARE(dialog.values().gamma, 3.5);
        auto *reset = button(dialog, QStringLiteral("Standardwerte"));
        QVERIFY(reset); reset->click();
        QCOMPARE(dialog.values().contrast, 0.0);
        QCOMPARE(dialog.values().saturation, 0.0);
        QCOMPARE(dialog.values().gamma, 1.0);
        QTest::mouseClick(original, Qt::LeftButton, Qt::NoModifier, original->rect().center());
        QCOMPARE(dialog.values().red, 100);
        QCOMPARE(dialog.values().green, 50);
        QCOMPARE(dialog.values().blue, 0);
        QSignalSpy applied(&dialog, &ix::ColorDialog::applyRequested);
        auto *apply = button(dialog, QStringLiteral("Auf Bild anwenden"));
        QVERIFY(apply); apply->click();
        QCOMPARE(applied.size(), 1);
        QCOMPARE(image.pixelColor(0, 0), QColor(50, 100, 150));
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
        const auto neighbor = photo(QStringLiteral("timestamp-neighbor/unchanged.png"), Qt::green);
        const auto target = temporary_.filePath(QStringLiteral("timestamp-rename/target.png"));
        QVERIFY(!source.isEmpty());
        ix::MainWindow window(nullptr, temporary_.filePath(QStringLiteral("timestamp-journal")));
        window.openPath(source);
        QTRY_VERIFY(!window.isBusy() && window.document().path == source);
        window.dispatch(QStringLiteral("right"));
        QTRY_VERIFY(!window.isBusy() && window.document().modified);
        ix::CodecRegistry registry;
        int neighborDecodes = 0;
        QVERIFY(ix::ImageLoadCache::read(registry, neighbor, 0, [&] { ++neighborDecodes; }).ok());
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
        const auto decodedBeforeRename = neighborDecodes;
        QVERIFY(ix::ImageLoadCache::read(registry, neighbor, 0, [&] { ++neighborDecodes; }).ok());
        QCOMPARE(neighborDecodes, decodedBeforeRename);
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
    void batchPlanHashesOnlyEligibleReplacements() {
        const auto source = photo(QStringLiteral("plan-eligibility/source.png"), Qt::red);
        ix::PipelineJob job;
        job.inputs = {source};
        job.outputDirectory = temporary_.filePath(QStringLiteral("plan-eligibility/output"));
        job.namePattern = QStringLiteral("target");
        job.profile.steps = {{QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        QVERIFY(QDir().mkpath(job.outputDirectory));
        const auto target = job.outputDirectory + QStringLiteral("/target.png");
        QFile existing(target);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        QCOMPARE(existing.write(QByteArray(1024 * 1024, 'x')), qint64(1024 * 1024));
        existing.close();
        const auto original = ix::sha256File(target);
        auto plan = ix::BatchPipeline::plan(job);
        QCOMPARE(plan.size(), 1);
        QVERIFY(!plan.first().error.isEmpty());
        QVERIFY(plan.first().targetHash.isEmpty());
        job.overwrite = true;
        plan = ix::BatchPipeline::plan(job);
        QVERIFY2(plan.first().error.isEmpty(), qPrintable(plan.first().error));
        QCOMPARE(plan.first().targetHash, original);
        job.inputs.append(source); // collision must not hash/read the target again
        plan = ix::BatchPipeline::plan(job);
        QCOMPARE(plan.size(), 2);
        QCOMPARE(plan.first().targetHash, original);
        QVERIFY(!plan.last().error.isEmpty());
        QVERIFY(plan.last().targetHash.isEmpty());
        job.inputs = {temporary_.filePath(QStringLiteral("missing.png"))};
        plan = ix::BatchPipeline::plan(job);
        QVERIFY(!plan.first().error.isEmpty());
        QVERIFY(plan.first().targetHash.isEmpty());
        job.inputs = {source};
        job.profile.steps.last().parameters.insert(QStringLiteral("quality"), 1000);
        plan = ix::BatchPipeline::plan(job);
        QVERIFY(!plan.first().error.isEmpty());
        QVERIFY(plan.first().targetHash.isEmpty());
        QCOMPARE(ix::sha256File(target), original);
    }
    void batchPlanRejectsDirectoryTargetsBeforeProcessing() {
        const auto source = photo(QStringLiteral("plan-directory/source.png"), Qt::red);
        const auto original = ix::sha256File(source);
        ix::PipelineJob job;
        job.inputs = {source};
        job.outputDirectory = temporary_.filePath(QStringLiteral("plan-directory/output"));
        job.namePattern = QStringLiteral("target");
        job.overwrite = true;
        job.profile.steps = {{QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        const auto target = job.outputDirectory + QStringLiteral("/target.png");
        QVERIFY(QDir().mkpath(target));
        const auto plan = ix::BatchPipeline::plan(job);
        QCOMPARE(plan.size(), 1);
        QVERIFY(plan.first().replaces);
        QVERIFY(!plan.first().error.isEmpty());
        QVERIFY(plan.first().targetHash.isEmpty());
        QVERIFY(QFileInfo(target).isDir());
        QCOMPARE(ix::sha256File(source), original);
    }
    void cancelledBatchReviewDoesNotDecodeOrCreateOutput() {
        class CountingCodec final : public ix::ImageCodec {
          public:
            std::shared_ptr<std::atomic_int> reads = std::make_shared<std::atomic_int>(0);
            bool accepts(const QString &) const override { return true; }
            QList<ix::FormatCapability> capabilities() const override { return {}; }
            ix::ImageResult read(const QString &, int) const override { ++*reads; return {}; }
        };
        const auto source = photo(QStringLiteral("plan-cancel/source.png"), Qt::green);
        ix::PipelineJob job;
        job.inputs = {source};
        job.outputDirectory = temporary_.filePath(QStringLiteral("plan-cancel/output"));
        job.cacheRoot = temporary_.filePath(QStringLiteral("plan-cancel/journal"));
        job.profile.steps = {{QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        const auto plan = ix::BatchPipeline::plan(job);
        QCOMPARE(plan.size(), 1);
        std::atomic_bool cancel = true;
        QVERIFY(ix::BatchPipeline::plan(job, &cancel).isEmpty());
        const auto preview = ix::BatchPipeline::preview(job, QStringLiteral("missing-input.png"), cancel, plan);
        QCOMPARE(preview.error, QStringLiteral("Abgebrochen."));
        QVERIFY(preview.before.isNull() && preview.after.isNull());
        auto codec = std::make_shared<CountingCodec>();
        ix::CodecRegistry registry; registry.add(codec);
        const auto result = ix::BatchPipeline::run(job, plan, cancel, {}, &registry);
        QCOMPARE(result.size(), plan.size());
        QVERIFY(result.first().error.startsWith(QStringLiteral("Abgebrochen")));
        QCOMPARE(codec->reads->load(), 0);
        QVERIFY(!QFileInfo::exists(job.outputDirectory));
        QVERIFY(!QFileInfo::exists(job.cacheRoot));
    }
    void pipelineFiltersAndDirectRunRecoverAfterCancel() {
        const auto first = photo(QStringLiteral("filter-state/alpha.PNG"), Qt::blue);
        const auto second = photo(QStringLiteral("filter-state/beta.jpg"), Qt::red);
        ix::PipelineDialog dialog({first, second}, nullptr, temporary_.filePath(QStringLiteral("filter-state/journal")));
        auto *chain = dialog.findChild<QListWidget *>(QStringLiteral("pipelineChain"));
        QCOMPARE(chain->count(), 0);
        auto *item = new QListWidgetItem(QStringLiteral("PNG"), chain);
        item->setData(Qt::UserRole, QJsonObject{{QStringLiteral("type"), QStringLiteral("export")},
            {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("format"), QStringLiteral("png")}}}});
        auto *output = dialog.findChild<QLineEdit *>(QStringLiteral("pipelineOutput"));
        output->setText(temporary_.filePath(QStringLiteral("filter-state/output")));
        auto *start = dialog.findChild<QPushButton *>(QStringLiteral("pipelineStart"));
        auto *search = dialog.findChild<QLineEdit *>(QStringLiteral("pipelineSearch"));
        auto *format = dialog.findChild<QComboBox *>(QStringLiteral("pipelineFormat"));
        auto *inputs = dialog.findChild<QListWidget *>(QStringLiteral("pipelineInputs"));
        QVERIFY(start->isEnabled());
        search->setText(QStringLiteral("missing"));
        QVERIFY(!start->isEnabled());
        search->setText(QStringLiteral("ALPHA"));
        QVERIFY(start->isEnabled());
        QVERIFY(!inputs->item(0)->isHidden() && inputs->item(1)->isHidden());
        search->clear();
        for (int i = 1; i < format->count(); ++i)
            if (format->itemData(i).toStringList().contains(QStringLiteral("png"))) { format->setCurrentIndex(i); break; }
        QVERIFY(!inputs->item(0)->isHidden() && inputs->item(1)->isHidden());
        dialog.findChild<QLineEdit *>(QStringLiteral("pipelinePattern"))->setText(QStringLiteral("cancel_{name}_####"));
        start->click();
        start->click(); // Cancel automatic planning, then recover without manual review.
        QTRY_COMPARE_WITH_TIMEOUT(start->text(), ix::localized(QStringLiteral("Starten")), 10000);
        dialog.findChild<QLineEdit *>(QStringLiteral("pipelinePattern"))->setText(QStringLiteral("{name}_####"));
        search->setText(QStringLiteral("alpha"));
        QVERIFY(start->isEnabled());
        start->click();
        QTRY_COMPARE_WITH_TIMEOUT(start->text(), ix::localized(QStringLiteral("Starten")), 10000);
        QVERIFY(QFileInfo::exists(output->text() + QStringLiteral("/alpha_0001.png")));
        QVERIFY(!QFileInfo::exists(output->text() + QStringLiteral("/beta_0002.png")));
        const auto *status = dialog.findChild<QLabel *>(QStringLiteral("pipelineStatus"));
        const auto *results = dialog.findChild<QTableView *>(QStringLiteral("pipelinePlan"));
        QVERIFY(!status->text().isEmpty());
        dialog.findChild<QLineEdit *>(QStringLiteral("pipelinePattern"))->setText(QStringLiteral("next_####"));
        QVERIFY(status->text().isEmpty());
        QVERIFY(status->isHidden());
        QVERIFY(dialog.findChild<QProgressBar *>()->isHidden());
        QCOMPARE(results->model()->rowCount(), 0);
    }
    void fullscreenFitModesRespectWindowPreference_data() {
        QTest::addColumn<int>("fit");
        for (int fit = 0; fit < 6; ++fit) QTest::newRow(qPrintable(QString::number(fit))) << fit;
    }
    void fullscreenFitModesRespectWindowPreference() {
        QFETCH(int, fit);
        QSettings settings;
        QVariantMap saved;
        for (const auto &key : settings.allKeys()) saved.insert(key, settings.value(key));
        const auto restore = qScopeGuard([&] {
            settings.clear();
            for (auto it = saved.cbegin(); it != saved.cend(); ++it) settings.setValue(it.key(), it.value());
        });
        settings.remove(QStringLiteral("fullscreen/fit"));
        settings.setValue(QStringLiteral("fullscreen/upscale"), true);
        QCOMPARE(ix::ImageView::fullscreenFit(), ix::ImageView::Fit::Window);
        settings.setValue(QStringLiteral("fullscreen/fit"), fit);
        settings.setValue(QStringLiteral("fullscreen/screen"), QStringLiteral("disconnected-monitor"));
        settings.setValue(QStringLiteral("view/fit"), int(ix::ImageView::Fit::LargeOnly));
        ix::SettingsDialog options;
        auto *choice = options.findChild<QComboBox *>(QStringLiteral("fullscreen/fit"));
        auto *monitor = options.findChild<QComboBox *>(QStringLiteral("fullscreen/screen"));
        QVERIFY(choice && monitor);
        QCOMPARE(choice->count(), 6);
        QCOMPARE(choice->currentIndex(), fit);
        QCOMPARE(monitor->count(), QGuiApplication::screens().size() + 1);
        QCOMPARE(monitor->currentIndex(), 0);
        ix::MainWindow window;
        window.show();
        const auto path = photo(QStringLiteral("fullscreen-modes/image.png"), Qt::blue);
        window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && window.document().path == path);
        auto *view = window.findChild<ix::ImageView *>();
        view->setZoom(8);
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(window.isFullScreen());
        QCOMPARE(view->fit(), ix::ImageView::Fit(fit));
        window.dispatch(QStringLiteral("fullscreen"));
        QTRY_VERIFY(!window.isFullScreen());
        QCOMPARE(view->fit(), ix::ImageView::Fit::LargeOnly);
        QCOMPARE(view->visibleImageRect(), view->image().rect());
    }
    void batchDialogKeepsActionsVisibleOnSmallScreen() {
        const auto oldPalette = QApplication::palette();
        const auto oldFont = QApplication::font();
        const auto oldLanguage = ix::uiLanguage();
        const auto restore = qScopeGuard([&] {
            ix::setUiLanguage(oldLanguage);
            QApplication::setFont(oldFont);
            ix::applyInterfaceTheme(QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString());
            QApplication::setPalette(oldPalette);
        });
        for (const auto &language : {QStringLiteral("de"), QStringLiteral("en"), QStringLiteral("ko"), QStringLiteral("ja"), QStringLiteral("fr")}) {
          QVERIFY(ix::setUiLanguage(language));
          ix::configureUiFont();
          for (const auto &mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
            ix::applyInterfaceTheme(mode);
            ix::PipelineDialog dialog({});
            dialog.resize(800, 480);
            dialog.show();
            QCoreApplication::processEvents();
            QCOMPARE(dialog.size(), QSize(800, 480));
            QVERIFY(dialog.findChildren<QScrollArea *>().isEmpty());
            auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("pipelineTabs"));
            QVERIFY(tabs && tabs->count() == 2);
            for (const auto &id : {QStringLiteral("pipelineStart"), QStringLiteral("pipelineClose"),
                    QStringLiteral("pipelineChooseOutput"), QStringLiteral("pipelineSaveProfile"),
                    QStringLiteral("pipelineOutput"), QStringLiteral("pipelinePattern"),
                    QStringLiteral("pipelineOverwrite"), QStringLiteral("pipelineAllowLoss"), QStringLiteral("pipelineLog")}) {
                const auto *control = dialog.findChild<QWidget *>(id);
                QVERIFY2(control && control->isVisible(), qPrintable(language + QLatin1Char(':') + id));
                QVERIFY(dialog.rect().contains(QRect(control->mapTo(&dialog, QPoint()), control->size())));
                QVERIFY(!control->visibleRegion().isEmpty());
            }
            const auto *choose = dialog.findChild<QPushButton *>(QStringLiteral("pipelineChooseOutput"));
            const auto *save = dialog.findChild<QPushButton *>(QStringLiteral("pipelineSaveProfile"));
            QVERIFY(choose->icon().isNull());
            QVERIFY(qAbs(choose->width() - save->width()) <= 1);
            const auto *files = dialog.findChild<QListWidget *>(QStringLiteral("pipelineInputs"));
            const auto *steps = dialog.findChild<QListWidget *>(QStringLiteral("pipelineChain"));
            QVERIFY(files->mapTo(&dialog, QPoint()).y() + files->height() < steps->mapTo(&dialog, QPoint()).y());
            QCOMPARE(files->width(), steps->width());
            QVERIFY(dialog.grab().save(QStringLiteral(IX_WORKFLOW_OUTPUT "/pipeline-%1-%2.png").arg(mode, language)));
            tabs->setCurrentIndex(1);
            const auto *results = dialog.findChild<QTableView *>(QStringLiteral("pipelinePlan"));
            QVERIFY(results && results->isVisible() && !results->visibleRegion().isEmpty());
          }
        }
    }
    void pipelineResultsModelFormatsVisibleCells() {
        ix::PipelineResultModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        ix::PipelineFile entry;
        entry.input = QStringLiteral("/photos/한글.png");
        entry.output = QStringLiteral("/export/한글.webp");
        model.setEntries({entry}, false);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.columnCount(), 4);
        QCOMPARE(model.data(model.index(0, 0)).toString(), QStringLiteral("한글.png"));
        QCOMPARE(model.data(model.index(0, 1), Qt::ToolTipRole).toString(), entry.output);
        QCOMPARE(model.data(model.index(0, 3)).toString(), ix::localized(QStringLiteral("Bereit")));
        entry.error = QStringLiteral("error");
        model.setEntries({entry}, true);
        QCOMPARE(model.data(model.index(0, 3)).toString(), entry.error);
        QVERIFY(!model.data(QModelIndex()).isValid());
        QCOMPARE(model.rowCount(model.index(0, 0)), 0);
        model.setEntries({}, true);
        QCOMPARE(model.rowCount(), 0);
    }
    void pipelineSkipsUnusedResourcesAndContainsDecoderExceptions() {
        ix::PipelineJob job;
        job.outputDirectory = temporary_.filePath(QStringLiteral("skip-resources/output"));
        job.cacheRoot = temporary_.filePath(QStringLiteral("skip-resources/journal"));
        job.profile.steps = {{QStringLiteral("watermark"), {{QStringLiteral("image"), QStringLiteral("missing.png")}}},
                            {QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        ix::PipelineFile skipped;
        skipped.error = QStringLiteral("skip");
        std::atomic_bool cancel = false;
        auto result = ix::BatchPipeline::run(job, {skipped}, cancel);
        QCOMPARE(result.first().error, skipped.error);
        QVERIFY(!QFileInfo::exists(job.outputDirectory));
        QVERIFY(!QFileInfo::exists(job.cacheRoot));
        class ThrowingCodec final : public ix::ImageCodec {
          public:
            bool accepts(const QString &) const override { return true; }
            QList<ix::FormatCapability> capabilities() const override { return {}; }
            ix::ImageResult read(const QString &, int) const override { throw 7; }
        };
        ix::CodecRegistry registry;
        registry.add(std::make_shared<ThrowingCodec>());
        job.profile.steps.removeFirst();
        job.inputs = {photo(QStringLiteral("skip-resources/source.png"), Qt::red)};
        const auto plan = ix::BatchPipeline::plan(job);
        QVERIFY(plan.first().error.isEmpty());
        result = ix::BatchPipeline::run(job, plan, cancel, {}, &registry);
        QCOMPARE(result.size(), 1);
        QVERIFY(!result.first().error.isEmpty());
        QVERIFY(!QFileInfo::exists(result.first().output));
    }
    void pipelineReportCountsOnlyCommittedFiles() {
        ix::PipelineFile ok;
        ok.input = QStringLiteral("=source,\"한글.png");
        ok.output = QStringLiteral("out.webp");
        ok.inputBytes = 1000; ok.outputBytes = 600;
        auto failed = ok;
        failed.error = QStringLiteral("failed");
        failed.outputBytes = 0;
        const auto summary = ix::pipelineSummary({ok, failed});
        QCOMPARE(summary.succeeded, 1);
        QCOMPARE(summary.failed, 1);
        QCOMPARE(summary.inputBytes, 1000);
        QCOMPARE(summary.outputBytes, 600);
        QString path;
        QVERIFY(ix::writePipelineReport({ok, failed}, temporary_.path(), path).isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        QVERIFY(bytes.startsWith("\xef\xbb\xbfsource,target"));
        QVERIFY(bytes.contains("\"'=source,\"\""));
        QVERIFY(bytes.contains(",1000,600\r\n"));
        QString secondPath;
        QVERIFY(ix::writePipelineReport({ok}, temporary_.path(), secondPath).isEmpty());
        QVERIFY(path != secondPath);
        QVERIFY(!ix::writePipelineReport({}, temporary_.filePath(QStringLiteral("missing")), path).isEmpty());
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
        job.cacheRoot = temporary_.filePath(QStringLiteral("pipeline-changing/journal"));
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
        if (choice == QStringLiteral("custom") || choice == QStringLiteral("640 x 480")) {
            auto *presets = dialog.findChild<QComboBox *>(QStringLiteral("resizePresets"));
            QVERIFY(presets);
            const auto index = presets->findData(choice == QStringLiteral("custom") ? QSize(90, 60) : QSize(640, 480));
            QVERIFY(index >= 0);
            presets->setCurrentIndex(index);
            QVERIFY(QMetaObject::invokeMethod(presets, "activated", Q_ARG(int, index)));
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
        QVERIFY(chain && chain->count() == 0);
        auto *item = new QListWidgetItem(QStringLiteral("Resize"), chain);
        auto step = QJsonObject{{QStringLiteral("type"), QStringLiteral("resize")},
            {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("width"), 2048}, {QStringLiteral("height"), 2048}}}};
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
    void cancelledClassicBatchRetainsCacheAndDeferredOpen() {
        const auto original = photo(QStringLiteral("classic-cancel/first.png"), Qt::red);
        const auto requested = photo(QStringLiteral("classic-cancel/second.png"), Qt::blue);
        const auto neighbor = photo(QStringLiteral("classic-cancel/neighbor.png"), Qt::green);
        ix::MainWindow window;
        window.show(); window.openPath(original);
        QTRY_VERIFY(!window.isBusy() && window.document().path == original);
        ix::ImageLoadCache::configure(64, 8);
        const auto resetCache = qScopeGuard([] { ix::ImageLoadCache::clear(); });
        ix::CodecRegistry registry;
        int decodes = 0;
        QVERIFY(ix::ImageLoadCache::read(registry, neighbor, 0, [&] { ++decodes; }).ok());
        const int warmed = decodes;
        bool inspected = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            timer.stop(); inspected = true;
            const auto close = qScopeGuard([&] { dialog->reject(); });
            QVERIFY(window.isBusy());
            window.openPath(requested);
            QCOMPARE(window.document().path, original);
        });
        timer.start(10);
        window.findChild<QAction *>(QStringLiteral("legacyBatch"))->trigger();
        timer.stop(); QVERIFY(inspected);
        QTRY_VERIFY(!window.isBusy() && window.document().path == requested);
        QVERIFY(ix::ImageLoadCache::read(registry, neighbor, 0, [&] { ++decodes; }).ok());
        QCOMPARE(decodes, warmed);
        QVERIFY(window.close());
    }
    void classicBatchFollowsFilesWhenNamesAreReused_data() {
        QTest::addColumn<int>("active");
        QTest::addColumn<int>("editState");
        QTest::newRow("first-file-keeps-rating") << 2 << 0;
        QTest::newRow("second-file-keeps-identity") << 3 << 0;
        QTest::newRow("edited-file-keeps-undo") << 3 << 1;
        QTest::newRow("clean-file-keeps-redo") << 2 << 2;
    }
    void classicBatchFollowsFilesWhenNamesAreReused() {
        QFETCH(int, active);
        QFETCH(int, editState);
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto prior = qApp->property("ixStorageData");
        const auto restore = qScopeGuard([&] { qApp->setProperty("ixStorageData", prior); });
        qApp->setProperty("ixStorageData", folder.filePath(QStringLiteral("data")));
        const auto first = folder.filePath(QStringLiteral("2.png"));
        const auto second = folder.filePath(QStringLiteral("3.png"));
        QImage red(96, 64, QImage::Format_RGB32), blue = red;
        red.fill(Qt::red); blue.fill(Qt::blue);
        QVERIFY(red.save(first)); QVERIFY(blue.save(second));
        ix::ImageCatalog catalog;
        QVERIFY(catalog.setAnnotation(first, 1, QStringLiteral("red")).isEmpty());
        QVERIFY(catalog.setAnnotation(second, 5, QStringLiteral("blue")).isEmpty());
        ix::MainWindow window;
        window.show();
        const auto original = active == 2 ? first : second;
        window.openPath(original);
        QTRY_VERIFY(!window.isBusy() && window.document().path == original);
        if (editState) {
            window.dispatch(QStringLiteral("grayscale"));
            QTRY_VERIFY(!window.isBusy() && window.document().modified);
            if (editState == 2) {
                window.dispatch(QStringLiteral("undo"));
                QTRY_VERIFY(!window.isBusy() && !window.document().modified);
            }
        }
        const auto pixels = window.document().pixels;
        int stage = 0;
        bool completed = false, warned = false;
        QElapsedTimer elapsed; elapsed.start();
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (qobject_cast<QMessageBox *>(dialog)) { warned = true; dialog->accept(); return; }
            if (elapsed.elapsed() > 15000) { dialog->reject(); return; }
            if (stage == 0) {
                auto *files = dialog->findChild<QListWidget *>();
                if (!files) return;
                QVERIFY(window.isBusy());
                QVERIFY(!window.close());
                files->clear(); files->addItems({first, second});
                for (auto *combo : dialog->findChildren<QComboBox *>())
                    if (combo->itemText(0) == ix::localized(QStringLiteral("Batch-Konvertierung"))) {
                        QCOMPARE(combo->currentIndex(), 1);
                        combo->setCurrentIndex(1);
                    }
                for (auto *edit : dialog->findChildren<QLineEdit *>()) {
                    if (edit->text() == QStringLiteral("{name}_####")) edit->setText(QStringLiteral("#"));
                    else if (edit->parent() == dialog && edit->text().isEmpty()) edit->setText(folder.path());
                }
                auto *start = button(*dialog, QStringLiteral("Starten"));
                QVERIFY(start); stage = 1; start->click();
            } else if (button(*dialog, QStringLiteral("Starten")) &&
                       QFileInfo::exists(folder.filePath(QStringLiteral("1.png"))) && !QFileInfo::exists(second)) {
                completed = true; stage = 2; dialog->accept();
            }
        });
        timer.start(10);
        window.findChild<QAction *>(QStringLiteral("legacyBatch"))->trigger();
        timer.stop();
        QVERIFY(completed);
        QVERIFY(!warned);
        const auto expected = folder.filePath(active == 2 ? QStringLiteral("1.png") : QStringLiteral("2.png"));
        QTRY_VERIFY(!window.isBusy() && window.document().path == expected);
        QCOMPARE(window.document().pixels, pixels);
        QCOMPARE(window.document().pixels.cacheKey(), pixels.cacheKey());
        QCOMPARE(window.document().sourceRevision, ix::imageFileKey(expected));
        QCOMPARE(window.document().modified, editState == 1);
        QCOMPARE(catalog.record(folder.filePath(QStringLiteral("1.png"))).value(QStringLiteral("rating")).toInt(), 1);
        QCOMPARE(catalog.record(first).value(QStringLiteral("rating")).toInt(), 5);
        if (editState) {
            const auto command = editState == 1 ? QStringLiteral("undo") : QStringLiteral("redo");
            QVERIFY(window.findChild<QAction *>(command)->isEnabled());
            window.dispatch(command);
            QTRY_VERIFY(!window.isBusy());
            QCOMPARE(window.document().path, expected);
            QCOMPARE(window.document().sourceRevision, ix::imageFileKey(expected));
        }
        window.openPath(expected, 0, false);
        QTRY_VERIFY(!window.isBusy() && !window.document().modified);
        QVERIFY(window.close());
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
