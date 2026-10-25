// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "main_window.h"
#include "extension_dialog.h"
#include "action_search.h"
#include "crop_ratios.h"
#include "core/file_journal.h"
#include "core/directory_index_service.h"
#include "core/image_catalog.h"
#include "core/image_loading.h"
#include "core/jpeg_transform.h"
#include "core/localization.h"
#include "core/plugin_store.h"
#include "core/preview_service.h"
#include "core/processing.h"
#include "core/rotation.h"
#include "core/selection.h"
#include "dialogs.h"
#include "icons.h"
#include "library_dialog.h"
#include "pipeline_dialog.h"
#include "platform_support.h"
#include "preview_strip.h"
#include "resize_dialog.h"
#include "rotation_dialog.h"
#include "settings_dialog.h"
#include "tool_dialog.h"
#include <QCollator>
#include <QColorSpace>
#include <QScopeGuard>
#include <QtConcurrent>
#include <QtPrintSupport>
#include <QtWidgets>
#include <cmath>
#include <utility>
#include <stdexcept>
namespace ix {
namespace {
QKeySequence shortcut(QString s) {
    s.replace(QStringLiteral("Strg"), QStringLiteral("Ctrl"));
    s.replace(QStringLiteral("Entf"), QStringLiteral("Del"));
    s.replace(QStringLiteral("Eingabetaste"), QStringLiteral("Return"));
    s.replace(QStringLiteral("Leertaste"), QStringLiteral("Space"));
    s.replace(QStringLiteral("Rücktaste"), QStringLiteral("Backspace"));
    s.replace(QStringLiteral("Pos1"), QStringLiteral("Home"));
    s.replace(QStringLiteral("Ende"), QStringLiteral("End"));
    return QKeySequence::fromString(s, QKeySequence::PortableText);
}
const QSet<QString> globalCommands{QStringLiteral("open"),           QStringLiteral("paste"),
                                   QStringLiteral("clearClipboard"), QStringLiteral("new"),
                                   QStringLiteral("exit"),           QStringLiteral("settings"),
                                   QStringLiteral("minimize"),       QStringLiteral("onTop"),
                                   QStringLiteral("statusbar"),      QStringLiteral("toolbar"),
                                   QStringLiteral("menubar"),        QStringLiteral("titlebar"),
                                   QStringLiteral("batch"),          QStringLiteral("find"),
                                   QStringLiteral("thumbnails"),     QStringLiteral("slideshowDialog"),
                                   QStringLiteral("capture"),        QStringLiteral("refresh")};
const QSet<QString> pathCommands{QStringLiteral("reopen"),   QStringLiteral("rename"), QStringLiteral("move"),
                                 QStringLiteral("fileCopy"), QStringLiteral("delete"), QStringLiteral("hex"),
                                 QStringLiteral("hotfolder")};
const QSet<QString> selectionCommands{QStringLiteral("cut"),    QStringLiteral("cutOutside"),
                                      QStringLiteral("crop"),   QStringLiteral("saveSelection"),
                                      QStringLiteral("redEye"), QStringLiteral("jpegCrop")};
const QSet<QString> checks{
    QStringLiteral("grid"),      QStringLiteral("onTop"),          QStringLiteral("statusbar"),
    QStringLiteral("toolbar"),   QStringLiteral("menubar"),        QStringLiteral("titlebar"),
    QStringLiteral("lockZoom"),  QStringLiteral("lockScroll"),     QStringLiteral("smooth"),
    QStringLiteral("smoothFit"), QStringLiteral("center"),         QStringLiteral("sharpenDisplay"),
    QStringLiteral("hotfolder"), QStringLiteral("invertSelection")};
} // namespace
MainWindow::MainWindow(QWidget *p, QString journalRoot)
    : QMainWindow(p), journalRoot_(std::move(journalRoot)),
      directoryIndex_(new DirectoryIndexService(this)), zoom_(nullptr) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("üxzView"));
    setWindowIcon(QIcon(QStringLiteral(":/ixview.svg")));
    setAcceptDrops(true);
    resize(980, 662);
    view_ = new ImageView(this);
    auto *center = new QWidget(this);
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    centerLayout->addWidget(view_, 1);
    setCentralWidget(center);
    buildMenus();
    buildToolbar();
    buildExtraFeatures();
    for (int i = 0; i < 6; ++i) {
        auto *label = new QLabel;
        label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        label->setMargin(2);
        statusBar()->addWidget(label, i == 0 ? 3 : 2);
        status_.append(label);
    }
    statusBar()->setSizeGripEnabled(true);
    connect(view_, &ImageView::zoomChanged, this, [this](double value) {
        QSignalBlocker blocker(zoom_);
        QSignalBlocker editorBlocker(zoom_->lineEdit());
        zoom_->setEditText(QString::number(value * 100, 'f', 1));
        zoomEdited_ = false;
        zoom_->lineEdit()->setModified(false);
        updateStatus();
        if (toolbar_)
            refreshState();
    });
    connect(view_, &ImageView::selectionChanged, this, [this] {
        if (!view_->selection().isEmpty())
            lastSelection_ = view_->selection();
        refreshState();
        updateStatus();
    });
    connect(view_, &ImageView::navigate, this, &MainWindow::navigate);
    connect(view_, &ImageView::commandRequested, this, &MainWindow::dispatch);
    connect(view_, &ImageView::pageRequested, this, [this](int direction) {
        if (activity_.busy())
            return;
        if (document_.pageCount > 1)
            page(direction);
        else
            navigate(direction);
    });
    connect(view_, &ImageView::toggleFullscreen, this, &MainWindow::fullscreen);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::refreshState);
    connect(&worker_, &QFutureWatcherBase::finished, this, [this] {
        activity_.finish();
        setCursor(Qt::ArrowCursor);
        auto r = worker_.result();
        if (rawPreviewShown_ && !r.error.isEmpty())
            view_->setImage(document_.pixels);
        rawPreviewShown_ = false;
        if (!r.error.isEmpty()) {
            pendingNavigation_ = 0;
            showError(r.error);
            slideshow_.stop();
            stopAnimation();
            refreshState();
            return;
        }
        if (workMode_ == WorkMode::Selection) {
            view_->setSelection(r.selection.value_or(QRect()));
            refreshState();
            return;
        }
        if (workMode_ == WorkMode::Edit) {
            pushUndo();
            redo_.clear();
        }
        if (workMode_ == WorkMode::Undo) {
            redo_.append({document_, view_->selection()});
            undo_.removeLast();
        } else if (workMode_ == WorkMode::Redo) {
            pushUndo();
            redo_.removeLast();
        }
        auto selection = view_->selection();
        bool preserveSelection = (workMode_ == WorkMode::Edit || workMode_ == WorkMode::Display) &&
                                 r.document.pixels.size() == document_.pixels.size();
        installDocument(std::move(r.document), std::move(r.display), workMode_ == WorkMode::Load);
        if (r.selection.has_value())
            view_->setSelection(*r.selection);
        else if (preserveSelection)
            view_->setSelection(selection);
        refreshState();
        if (pendingNavigation_ != 0 && !deferredOpen_) {
            // Keep the steps cancellable until delivery: a newer explicit open
            // clears them; another arrow press can still accumulate them.
            QTimer::singleShot(0, this, [this] {
                if (!deferredOpen_ && pendingNavigation_ != 0)
                    navigate(std::exchange(pendingNavigation_, 0));
            });
        }
    });
    connect(&slideshow_.timer(), &QTimer::timeout, this, &MainWindow::advanceSlideshow);
    animationTimer_.setParent(this);
    animationTimer_.setObjectName(QStringLiteral("animationTimer"));
    animationTimer_.setSingleShot(true);
    connect(&animationTimer_, &QTimer::timeout, this, [this] {
        if (!animationPlaying_)
            return;
        if (activity_.busy() || deferredOpen_ || QApplication::activeModalWidget()) {
            animationTimer_.start(50);
            return;
        }
        int next = document_.page + 1;
        if (next >= document_.pageCount) {
            next = 0;
            if (document_.loopCount >= 0 && ++animationLoops_ > document_.loopCount) {
                stopAnimation();
                animationEnded_ = true;
                return;
            }
        }
        openPath(document_.path, next, false);
    });
    connect(&hotfolder_, &QFileSystemWatcher::directoryChanged, this, [this] { directoryDebounce_.start(); });
    directoryDebounce_.setSingleShot(true);
    directoryDebounce_.setInterval(180);
    connect(&directoryWatcher_, &QFileSystemWatcher::directoryChanged, this,
            [this] { directoryDebounce_.start(); });
    connect(&directoryDebounce_, &QTimer::timeout, this, [this] { refreshFiles(true); });
    connect(directoryIndex_, &DirectoryIndexService::failed, this, &MainWindow::showError);
    connect(directoryIndex_, &DirectoryIndexService::ready, this, [this](DirectorySnapshot snapshot) {
        QString current =
            !watchedFolder_.isEmpty() ? watchedFolder_ : QFileInfo(document_.path).absolutePath();
        if (document_.path.isEmpty() && watchedFolder_.isEmpty())
            return;
        if (snapshot.directory != current)
            return;
        indexReady_ = true;
        files_ = std::move(snapshot.files);
        filePositions_ = std::move(snapshot.positions);
        fileIndex_ = filePositions_.value(document_.path, -1);
        updatePreviews();
        updateStatus();
        refreshState();
        if (!watchedFolder_.isEmpty() && !activity_.busy() && !QApplication::activeModalWidget() &&
            !document_.modified && !snapshot.latest.isEmpty() && snapshot.latest != document_.path) {
            openPath(snapshot.latest, 0, false);
            return;
        }
        if (!activity_.busy() && pendingNavigation_) {
            int step = std::exchange(pendingNavigation_, 0);
            navigate(step);
        }
    });
    deferredOpenTimer_.setSingleShot(true);
    deferredOpenTimer_.setInterval(50);
    connect(&deferredOpenTimer_, &QTimer::timeout, this, [this] {
        if (!deferredOpen_)
            return;
        if (activity_.busy() || QApplication::activeModalWidget()) {
            deferredOpenTimer_.start();
            return;
        }
        auto request = std::exchange(deferredOpen_, std::nullopt);
        openPath(request->path, request->page, request->checkChanges);
    });
    QSettings settings;
    if (settings.value(QStringLiteral("window/rememberGeometry"), true).toBool())
        restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    // Old settings may contain a temporary fullscreen state. Restore the
    // ordinary window first so fullscreen always goes through fullscreen().
    setWindowState(windowState() & ~Qt::WindowFullScreen);
    applySettings(true);
    refreshState();
    updateStatus();
}
QStringList MainWindow::implementedCommands() const {
    QStringList result;
    for (auto it = actions_.begin(); it != actions_.end(); ++it)
        result << it.key();
    return result;
}
void MainWindow::buildMenus() {
    // Keep the verified reference ordering inside the window on every desktop.
    menuBar()->setNativeMenuBar(false);
    QFile file(QStringLiteral(":/menus.de.json"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Menu resource missing");
    menuData_ = QJsonDocument::fromJson(file.readAll()).object();
    for (auto value : menuData_.value(QStringLiteral("menus")).toArray()) {
        auto object = value.toObject();
        if (!QStringList{QStringLiteral("datei"), QStringLiteral("bearbeiten"), QStringLiteral("bild"),
                         QStringLiteral("optionen"), QStringLiteral("ansicht")}
                 .contains(object.value(QStringLiteral("id")).toString()))
            continue;
        auto *menu = menuBar()->addMenu(localized(object.value(QStringLiteral("label")).toString()));
        menu->setObjectName(object.value(QStringLiteral("id")).toString());
        populateMenu(menu, object);
    }
    auto *help = menuBar()->addMenu(IX_TEXT("&Hilfe"));
    auto *formats = help->addAction(IX_TEXT("Verfügbare Bildformate..."));
    connect(formats, &QAction::triggered, this, [this] { formatDialog(this, registry_); });
    auto *keys = help->addAction(IX_TEXT("Tastenkürzel..."));
    keys->setObjectName(QStringLiteral("keyboardHelp"));
    keys->setShortcut(QKeySequence(Qt::Key_F1));
    addAction(keys);
    connect(keys, &QAction::triggered, this, &MainWindow::showKeyboardHelp);
    auto *about = help->addAction(IX_TEXT("Über üxzView..."));
    about->setObjectName(QStringLiteral("about"));
    about->setShortcut(QKeySequence(Qt::Key_A));
    addAction(about);
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, IX_TEXT("Über üxzView"),
            IX_TEXT(
                "üxzView %1\n\nEntwickler: Michael Förtsch\n\nDieses Programm entstand mit Hilfe von Codex, "
                "Cline, Claude und Kimi K3.\n\nVon IrfanView und nomacs inspiriert.\n\nQuellcode: "
                "GPL-3.0-or-later.\nQt und "
                "Bibliotheken: siehe Lizenzinformationen im Programmverzeichnis.")
                .arg(QCoreApplication::applicationVersion()));
    });
    QFile bindings(QStringLiteral(":/shortcuts.de.json"));
    if (bindings.open(QIODevice::ReadOnly)) {
        const auto aliases =
            QJsonDocument::fromJson(bindings.readAll()).object().value(QStringLiteral("aliases")).toObject();
        for (auto it = aliases.begin(); it != aliases.end(); ++it) {
            if (auto *action = actions_.value(it.key())) {
                auto keys = action->shortcuts();
                for (auto value : it.value().toArray())
                    keys.append(QKeySequence::fromString(value.toString(), QKeySequence::PortableText));
                action->setShortcuts(keys);
            }
        }
    }
}
void MainWindow::showKeyboardHelp() {
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("keyboardHelpDialog"));
    dialog.setWindowTitle(IX_TEXT("üxzView - Tastenkürzel und Maus"));
    dialog.resize(820, 540);
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(0, 3);
    table->setObjectName(QStringLiteral("shortcutTable"));
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalHeaderLabels({IX_TEXT("Taste / Maus"), IX_TEXT("Aktion"), IX_TEXT("Kontext")});
    auto row = [table](QString key, QString action, QString context) {
        int n = table->rowCount();
        table->insertRow(n);
        table->setItem(n, 0, new QTableWidgetItem(key));
        table->setItem(n, 1, new QTableWidgetItem(action));
        table->setItem(n, 2, new QTableWidgetItem(context));
    };
    for (auto *action : actions_) {
        if (action->objectName() == QStringLiteral("exit")) {
            row(QStringLiteral("Esc"), action->text().section(QLatin1Char('\t'), 0, 0),
                IX_TEXT("Vollbild beenden; sonst Fenster schließen"));
            continue;
        }
        QStringList keys;
        for (auto key : action->shortcuts())
            keys.append(key.toString(QKeySequence::NativeText));
        if (!keys.isEmpty())
            row(keys.join(QStringLiteral(", ")), action->text(), IX_TEXT("Hauptfenster"));
    }
    QFile file(QStringLiteral(":/shortcuts.de.json"));
    if (file.open(QIODevice::ReadOnly)) {
        auto data = QJsonDocument::fromJson(file.readAll()).object();
        for (auto group : {QStringLiteral("contextKeys"), QStringLiteral("mouse")})
            for (auto item : data.value(group).toArray()) {
                auto o = item.toObject();
                row(o.value(QStringLiteral("key")).toString(), o.value(QStringLiteral("action")).toString(),
                    o.value(QStringLiteral("context")).toString());
            }
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}
void MainWindow::populateMenu(QMenu *menu, const QJsonObject &object) {
    bool separator = false;
    for (auto value : object.value(QStringLiteral("items")).toArray()) {
        auto item = value.toObject();
        if (item.value(QStringLiteral("separator")).toBool()) {
            separator = true;
            continue;
        }
        QString cmd = item.value(QStringLiteral("command")).toString(),
                dynamic = item.value(QStringLiteral("dynamic")).toString(),
                submenu = item.value(QStringLiteral("submenu")).toString();
        if (cmd.isEmpty() && dynamic.isEmpty() && submenu.isEmpty())
            continue;
        if (separator && !menu->isEmpty())
            menu->addSeparator();
        separator = false;
        QString label = localized(item.value(QStringLiteral("label")).toString());
        if (!dynamic.isEmpty()) {
            auto *child = menu->addMenu(label);
            connect(child, &QMenu::aboutToShow, this, [this, child, dynamic] { recentMenu(child, dynamic); });
            continue;
        }
        if (!submenu.isEmpty()) {
            for (auto m : menuData_.value(QStringLiteral("menus")).toArray())
                if (m.toObject().value(QStringLiteral("id")).toString() == submenu) {
                    auto *child = menu->addMenu(label);
                    child->setObjectName(submenu);
                    populateMenu(child, m.toObject());
                }
            continue;
        }
        QAction *action = new QAction(label, this);
        if (cmd == QStringLiteral("cropAspect")) {
            auto *ratios = new QMenu(label, menu);
            ratios->setObjectName(QStringLiteral("cropAspectMenu"));
            action->setMenu(ratios);
            connect(ratios, &QMenu::aboutToShow, this, [this, ratios] { populateCropMenu(ratios); });
        }
        {
            action->setObjectName(cmd);
            if (item.contains(QStringLiteral("originalId")))
                action->setProperty("referenceId", item.value(QStringLiteral("originalId")).toInt());
            action->setData(cmd);
            QString key = item.value(QStringLiteral("shortcut")).toString();
            if (!key.isEmpty() && cmd != QStringLiteral("exit"))
                action->setShortcut(shortcut(key));
            if (cmd == QStringLiteral("exit"))
                action->setText(label + QStringLiteral("\tEsc"));
            if (cmd == QStringLiteral("fullscreen"))
                action->setAutoRepeat(false);
            if (checks.contains(cmd))
                action->setCheckable(true);
            connect(action, &QAction::triggered, this, [this, action, cmd] {
                if (activity_.busy() || QApplication::activeModalWidget()) {
                    // QAction toggles before emitting triggered. A command that
                    // is blocked during loading must retain its prior checkmark.
                    if (action->isCheckable())
                        action->setChecked(!action->isChecked());
                    return;
                }
                dispatch(cmd);
            });
            if (!actions_.contains(cmd))
                actions_.insert(cmd, action);
            addAction(action);
        }
        menu->addAction(action);
    }
}
void MainWindow::populateCropMenu(QMenu *menu) {
    menu->clear();
    auto choose = [this](QSize ratio) {
        if (activity_.busy() || document_.empty())
            return;
        invertedSelection_ = false;
        if (auto *invert = actions_.value(QStringLiteral("invertSelection")))
            invert->setChecked(false);
        view_->setSelectionAspectRatio(ratio);
        view_->setFocus();
    };
    auto add = [this, menu, choose](const QString &label, QSize ratio) {
        auto *action = menu->addAction(label);
        action->setCheckable(true);
        auto current = view_->selectionAspectRatio();
        action->setChecked((!ratio.isValid() && !current.isValid()) ||
                           (ratio.isValid() && current.isValid() &&
                            qint64(ratio.width()) * current.height() == qint64(ratio.height()) * current.width()));
        action->setProperty("cropRatio", ratio);
        action->setEnabled(!activity_.busy() && !document_.empty());
        connect(action, &QAction::triggered, this, [choose, ratio] { choose(ratio); });
    };
    add(IX_TEXT("Freie Auswahl"), {});
    menu->addSeparator();
    auto ratios = standardCropRatios();
    QStringList custom;
    if (parseCropRatios(QSettings().value(QStringLiteral("crop/customRatios")).toStringList().join(QLatin1Char('\n')), custom))
        for (const auto &label : custom) {
            auto ratio = cropRatioSize(label);
            bool duplicate = false;
            for (const auto &existing : ratios)
                duplicate |= qint64(existing.width()) * ratio.height() == qint64(existing.height()) * ratio.width();
            if (!duplicate)
                ratios.append(ratio);
        }
    for (const auto &ratio : ratios)
        add(cropRatioLabel(ratio), ratio);
    menu->addSeparator();
    menu->addAction(actions_.value(QStringLiteral("crop")));
    auto *settings = menu->addAction(IX_TEXT("Eigene Seitenverhältnisse …"));
    connect(settings, &QAction::triggered, this, [this] { dispatch(QStringLiteral("cropSettings")); });
}
void MainWindow::buildToolbar() {
    toolbar_ = addToolBar(IX_TEXT("Symbolleiste"));
    toolbar_->setObjectName(QStringLiteral("mainToolbar"));
    toolbar_->setMovable(false);
    toolbar_->setFloatable(false);
    toolbar_->setIconSize({24, 24});
    auto add = [&](const QString &cmd, const QString &icon, const QString &description) {
        auto *action = actions_.value(cmd);
        if (!action)
            return;
        action->setProperty("ixIcon", icon);
        action->setIcon(uiIcon(icon, palette()));
        auto tip = description;
        if (!action->shortcuts().isEmpty())
            tip +=
                QStringLiteral(" (%1)").arg(action->shortcuts().first().toString(QKeySequence::NativeText));
        action->setToolTip(tip);
        action->setStatusTip(description);
        toolbar_->addAction(action);
        if (auto *widget = toolbar_->widgetForAction(action)) {
            widget->setAccessibleName(description);
            widget->setAccessibleDescription(tip);
        }
    };
    add(QStringLiteral("open"), QStringLiteral("open"), IX_TEXT("Bilddatei öffnen"));
    add(QStringLiteral("slideshowDialog"), QStringLiteral("slideshow"), IX_TEXT("Diashow starten"));
    add(QStringLiteral("saveAs"), QStringLiteral("save"), IX_TEXT("Bild unter neuem Namen speichern"));
    add(QStringLiteral("print"), QStringLiteral("print"), IX_TEXT("Bild drucken"));
    add(QStringLiteral("delete"), QStringLiteral("trash"),
        IX_TEXT("Datei in den internen Papierkorb verschieben"));
    toolbar_->addSeparator();
    add(QStringLiteral("cut"), QStringLiteral("cut"), IX_TEXT("Markierten Bereich ausschneiden"));
    add(QStringLiteral("copy"), QStringLiteral("copy"), IX_TEXT("Bild oder Markierung kopieren"));
    add(QStringLiteral("paste"), QStringLiteral("paste"), IX_TEXT("Bild aus der Zwischenablage einfügen"));
    add(QStringLiteral("undo"), QStringLiteral("undo"), IX_TEXT("Letzte Bildbearbeitung rückgängig machen"));
    add(QStringLiteral("info"), QStringLiteral("image-info"),
        IX_TEXT("Bildinformationen und Metadaten anzeigen"));
    toolbar_->addSeparator();
    add(QStringLiteral("zoomOut"), QStringLiteral("zoom-out"), IX_TEXT("Verkleinern (herauszoomen)"));
    zoom_ = new QComboBox;
    zoom_->setObjectName(QStringLiteral("zoom"));
    zoom_->setEditable(true);
    zoom_->lineEdit()->installEventFilter(this);
    zoom_->setFixedWidth(66);
    zoom_->addItems({QStringLiteral("10"), QStringLiteral("25"), QStringLiteral("50"), QStringLiteral("75"),
                     QStringLiteral("100"), QStringLiteral("150"), QStringLiteral("200"),
                     QStringLiteral("400")});
    zoom_->setEditText(QStringLiteral("100.0"));
    toolbar_->addWidget(zoom_);
    // QComboBox completion can reset QLineEdit::isModified() while the user
    // types a preset. Track user edits independently of programmatic display.
    connect(zoom_->lineEdit(), &QLineEdit::textEdited, this, [this] { zoomEdited_ = true; });
    connect(zoom_->lineEdit(), &QLineEdit::editingFinished, this, &MainWindow::commitZoomEdit);
    connect(zoom_, &QComboBox::activated, this, [this] {
        if (!activity_.busy())
            view_->setZoom(zoom_->currentText().toDouble() / 100);
    });
    add(QStringLiteral("zoomIn"), QStringLiteral("zoom-in"), IX_TEXT("Vergrößern (hineinzoomen)"));
    toolbar_->addSeparator();
    add(QStringLiteral("previous"), QStringLiteral("previous-image"), IX_TEXT("Vorheriges Bild öffnen"));
    add(QStringLiteral("next"), QStringLiteral("next-image"), IX_TEXT("Nächstes Bild öffnen"));
    auto *prev =
        toolbar_->addAction(uiIcon(QStringLiteral("previous-page"), palette()), IX_TEXT("Vorherige Seite"));
    prev->setObjectName(QStringLiteral("previousPage"));
    prev->setProperty("ixIcon", QStringLiteral("previous-page"));
    prev->setToolTip(IX_TEXT("Vorherige Seite innerhalb der mehrseitigen Datei"));
    connect(prev, &QAction::triggered, this, [this] { page(-1); });
    auto *next =
        toolbar_->addAction(uiIcon(QStringLiteral("next-page"), palette()), IX_TEXT("Nächste Seite"));
    next->setObjectName(QStringLiteral("nextPage"));
    next->setProperty("ixIcon", QStringLiteral("next-page"));
    next->setToolTip(IX_TEXT("Nächste Seite innerhalb der mehrseitigen Datei"));
    connect(next, &QAction::triggered, this, [this] { page(1); });
    toolbar_->addSeparator();
    add(QStringLiteral("settings"), QStringLiteral("settings"), IX_TEXT("Einstellungen öffnen"));
}
void MainWindow::reconcileFilePath() {
    if (document_.path.isEmpty())
        return;
    QString resolved = document_.path;
    if (!QFileInfo::exists(resolved)) {
        // Follow committed batch renames and undone moves without assuming that
        // an existing pathname still names the document we have in memory.
        for (const auto &value : FileJournal(journalRoot_).entries()) {
            const auto entry = value.toObject();
            const auto source = entry.value(QStringLiteral("source")).toString();
            const auto target = entry.value(QStringLiteral("target")).toString();
            if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("move") &&
                entry.value(QStringLiteral("state")).toString() == QStringLiteral("committed") &&
                source == resolved)
                resolved = target;
            else if (entry.value(QStringLiteral("state")).toString() == QStringLiteral("undone") &&
                     target == resolved)
                resolved = source;
        }
        if (!QFileInfo::exists(resolved))
            resolved.clear();
    }
    const auto revision = resolved.isEmpty() ? QString() : imageFileKey(resolved);
    if (resolved == document_.path && revision == document_.sourceRevision)
        return;
    const auto before = document_.path;
    document_.path = resolved;
    for (auto *history : {&undo_, &redo_})
        for (auto &entry : *history) {
            entry.path = resolved;
            entry.modified = true;
        }
    if (resolved.isEmpty()) {
        document_.modified = true;
        document_.sourceRevision.clear();
    } else {
        if (resolved != before)
            ImageCatalog().relocate(before, resolved);
        if (!document_.modified) {
            // A failed reload must leave the older pixels explicitly dirty.
            document_.modified = true;
            updateStatus();
            ImageLoadCache::clear();
            openPath(resolved, 0, false);
            return;
        }
        stopAnimation();
        QMessageBox::warning(this, IX_TEXT("Datei auf Datenträger geändert"),
                             IX_TEXT("Die Datei wurde auf dem Datenträger geändert. Ihre ungespeicherten "
                                     "Änderungen bleiben erhalten. Verwenden Sie Speichern unter oder "
                                     "laden Sie die Datei neu."));
    }
    updateStatus();
    refreshState();
}
void MainWindow::recentMenu(QMenu *menu, const QString &key) {
    menu->clear();
    for (const auto &path : QSettings().value(QStringLiteral("files/") + key).toStringList()) {
        auto *a = menu->addAction(path);
        connect(a, &QAction::triggered, this, [this, path] { openPath(path); });
    }
}
void MainWindow::buildExtraFeatures() {
    auto *viewMenu = menuBar()->findChild<QMenu *>(QStringLiteral("ansicht"));
    auto *options = menuBar()->findChild<QMenu *>(QStringLiteral("optionen"));
    options->setToolTipsVisible(true);
    auto *fileMenu = menuBar()->findChild<QMenu *>(QStringLiteral("datei"));
    auto *displayOptions = viewMenu->findChild<QMenu *>(QStringLiteral("anzeige-optionen"));
    auto *info = new QAction(IX_TEXT("Informationen anzeigen (Vollbildmodus)"), viewMenu);
    info->setObjectName(QStringLiteral("fullscreenInfoOptions"));
    const int displayIndex = displayOptions ? viewMenu->actions().indexOf(displayOptions->menuAction()) : -1;
    viewMenu->insertAction(viewMenu->actions().value(displayIndex + 1), info);
    connect(info, &QAction::triggered, this, [this] {
        if (fullscreenInfoDialog(this)) {
            fullscreenInfo_ = fullscreenInfoOptions();
            updateStatus();
        }
    });
    auto *before = actions_.value(QStringLiteral("fullscreen"));
    auto *strip = new QAction(IX_TEXT("Vorschaubilderleiste"), viewMenu);
    viewMenu->insertSeparator(before);
    viewMenu->insertAction(before, strip);
    viewMenu->insertSeparator(before);
    strip->setObjectName(QStringLiteral("previewStripToggle"));
    strip->setCheckable(true);
    connect(strip, &QAction::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("view/previewStrip"), enabled);
        if (enabled && !previewStrip_) {
            previewStrip_ = new PreviewStrip(this);
            centralWidget()->layout()->addWidget(previewStrip_);
            connect(previewStrip_, &PreviewStrip::openRequested, this,
                    [this](const QString &path) { openPath(path); });
        }
        if (previewStrip_) {
            previewStrip_->setVisible(enabled && !isFullScreen());
            if (enabled) {
                previewStrip_->applySettings();
                updatePreviews();
            } else
                previewStrip_->stop();
        }
    });
    auto *about = findChild<QAction *>(QStringLiteral("about"));
    about->setProperty("ixIcon", QStringLiteral("about"));
    about->setIcon(uiIcon(QStringLiteral("about"), palette()));
    about->setToolTip(IX_TEXT("Über üxzView (A)"));
    toolbar_->addAction(about);
    auto *search = viewMenu->addAction(IX_TEXT("Befehlssuche anzeigen"));
    search->setObjectName(QStringLiteral("actionSearchToggle"));
    search->setCheckable(true);
    connect(search, &QAction::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("view/actionSearch"), enabled);
        if (enabled && !actionSearch_) {
            QList<QAction *> commands;
            for (auto *action : actions_)
                commands.append(action);
            auto *spacer = new QWidget(toolbar_);
            spacer->setObjectName(QStringLiteral("actionSearchSpacer"));
            spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            searchSpacerAction_ = toolbar_->addWidget(spacer);
            actionSearch_ = new ActionSearch(commands, toolbar_);
            searchWidgetAction_ = toolbar_->addWidget(actionSearch_);
            connect(actionSearch_, &ActionSearch::finished, view_, [this] { view_->setFocus(); });
        }
        if (searchWidgetAction_) {
            searchSpacerAction_->setVisible(enabled);
            searchWidgetAction_->setVisible(enabled);
        }
    });
    auto *journal = new QAction(IX_TEXT("Dateijournal / interner Papierkorb …"), fileMenu);
    journal->setObjectName(QStringLiteral("fileJournal"));
    connect(journal, &QAction::triggered, this, [this] {
        if (activity_.busy())
            return;
        journalDialog(this, journalRoot_);
        reconcileFilePath();
        ImageLoadCache::clear();
        refreshFiles(true);
    });
    auto *undo = new QAction(IX_TEXT("Letzte Dateioperation rückgängig"), fileMenu);
    undo->setObjectName(QStringLiteral("fileUndo"));
    connect(undo, &QAction::triggered, this, [this] {
        if (activity_.busy())
            return;
        auto error = undoFileOperation(this, {}, journalRoot_);
        if (error.isEmpty())
            reconcileFilePath();
        if (!error.isEmpty())
            showError(error);
        ImageLoadCache::clear();
        refreshFiles(true);
    });
    const auto fileActions = fileMenu->actions();
    auto *afterDelete = fileActions.value(fileActions.indexOf(actions_.value(QStringLiteral("delete"))) + 1);
    fileMenu->insertSeparator(afterDelete);
    fileMenu->insertAction(afterDelete, undo);
    fileMenu->insertAction(afterDelete, journal);
    auto *legacy = new QAction(IX_TEXT("Klassische Stapel-Umbenennung …"), fileMenu);
    legacy->setObjectName(QStringLiteral("legacyBatch"));
    fileMenu->insertAction(fileActions.value(fileActions.indexOf(actions_.value(QStringLiteral("batch"))) + 1),
                           legacy);
    connect(legacy, &QAction::triggered, this, [this] {
        if (activity_.busy())
            return;
        batchDialog(this, document_.path.isEmpty() ? QStringList() : QStringList{document_.path}, journalRoot_);
        reconcileFilePath();
        ImageLoadCache::clear();
        refreshFiles(true);
    });
    auto *plugins = options->addAction(IX_TEXT("Erweiterungen verwalten …"));
    plugins->setObjectName(QStringLiteral("extensionManager"));
    connect(plugins, &QAction::triggered, this, [this] {
        if (activity_.busy()) return;
        activity_.begin(DocumentActivity::Phase::Files);
        auto release = qScopeGuard([this] { activity_.finish(); refreshState(); });
        slideshow_.stop();
        stopAnimation();
        extensionManagerDialog(this);
    });
    auto *tools = menuBar()->findChild<QMenu *>(QStringLiteral("bild"))->addMenu(IX_TEXT("Funktionserweiterungen"));
    for (const auto &entry : {std::pair{QStringLiteral("zxing-cpp"), IX_TEXT("QR-/Barcodes lesen …")},
                              std::pair{QStringLiteral("tesseract-ocr"), IX_TEXT("Text erkennen …")}}) {
        auto *action = tools->addAction(entry.second);
        const auto id = entry.first;
        action->setObjectName(id);
        action->setData(id);
        actions_.insert(id, action);
        connect(action, &QAction::triggered, this, [this, id] {
            if (activity_.busy() || document_.empty()) return;
            activity_.begin(DocumentActivity::Phase::Files);
            auto release = qScopeGuard([this] { activity_.finish(); refreshState(); });
            slideshow_.stop();
            stopAnimation();
            refreshState();
            imageToolDialog(this, id, document_.pixels, view_->selection());
        });
    }
    connect(view_, &ImageView::recognitionMenuRequested, this, [this](QPoint position) {
        if (activity_.busy() || document_.empty()) return;
        QMenu menu(this);
        menu.setObjectName(QStringLiteral("imageRecognitionMenu"));
        menu.addAction(actions_.value(QStringLiteral("zxing-cpp")));
        menu.addAction(actions_.value(QStringLiteral("tesseract-ocr")));
        menu.exec(position);
    });
    strip->setChecked(QSettings().value(QStringLiteral("view/previewStrip"), false).toBool());
    search->setChecked(QSettings().value(QStringLiteral("view/actionSearch"), false).toBool());
}
void MainWindow::updateRecent(const QString &key, const QString &path) {
    QSettings s;
    auto list = s.value(QStringLiteral("files/") + key).toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 15)
        list.removeLast();
    s.setValue(QStringLiteral("files/") + key, list);
}
void MainWindow::showError(const QString &error) {
    QMessageBox::warning(this, QStringLiteral("üxzView"), localized(error));
}
void MainWindow::beginWork(std::function<WorkResult()> fn, WorkMode mode) {
    if (activity_.busy())
        return;
    activity_.begin(mode == WorkMode::Load || mode == WorkMode::Page ? DocumentActivity::Phase::Loading
                                                                     : DocumentActivity::Phase::Editing);
    workMode_ = mode;
    setCursor(Qt::WaitCursor);
    refreshState();
    worker_.setFuture(QtConcurrent::run([fn = std::move(fn)] {
        try {
            return fn();
        } catch (const std::exception &e) {
            return WorkResult{{}, {}, QString::fromUtf8(e.what())};
        } catch (...) {
            return WorkResult{{}, {}, IX_TEXT("Bildoperation fehlgeschlagen.")};
        }
    }));
}
void MainWindow::openPath(const QString &path, int pageIndex, bool checkChanges) {
    if (path.isEmpty())
        return;
    // A new explicit choice supersedes queued steps from the previous folder.
    // Automatic animation frames (checkChanges=false) must retain those steps.
    if (checkChanges)
        pendingNavigation_ = 0;
    if (activity_.busy() || QApplication::activeModalWidget()) {
        deferredOpen_ = DeferredOpen{path, pageIndex, checkChanges};
        deferredOpenTimer_.start();
        return;
    }
    // Otherwise a previous modal/busy request can fire 50 ms later and replace
    // the image the user has just chosen while the window was already idle.
    deferredOpenTimer_.stop();
    deferredOpen_.reset();
    if (checkChanges && !confirmDiscard(true))
        return;
    animationTimer_.stop();
    if (path != document_.path) {
        animationLoops_ = 0;
        animationPlaying_ = true;
        animationEnded_ = false;
    }
    bool manage = QSettings().value(QStringLiteral("color/manage"), true).toBool(), sharp = displaySharpen_;
    auto registry = registry_;
    bool rawPreview = QSettings().value(QStringLiteral("performance/rawPreview"), true).toBool();
    bool same = path == document_.path;
    const int generation = ++loadGeneration_;
    QPointer<MainWindow> self(this);
    beginWork(
        [=] {
            const ImageLoadRequest request{path, pageIndex, manage, sharp, rawPreview};
            auto result = loadImage(registry, request, [self, generation](QImage preview) {
                if (!self) return;
                QMetaObject::invokeMethod(self, [self, preview = std::move(preview), generation] {
                    if (self && self->activity_.workerActive() && self->loadGeneration_ == generation) {
                        self->view_->setImage(preview);
                        self->rawPreviewShown_ = true;
                        self->status_[0]->setText(IX_TEXT("RAW-Vorschau – Original wird entwickelt …"));
                    }
                }, Qt::QueuedConnection);
            });
            return WorkResult{std::move(result.document), std::move(result.display), std::move(result.error)};
        },
        same && pageIndex != document_.page && !document_.modified ? WorkMode::Page : WorkMode::Load);
}
void MainWindow::installDocument(ImageDocument d, QImage display, bool clearHistory) {
    if (clearHistory) {
        undo_.clear();
        redo_.clear();
        lastSelection_ = {};
    }
    document_ = std::move(d);
    double z = view_->zoom();
    auto fit = view_->fit();
    auto browsingFit = view_->browsingFit();
    int sx = view_->horizontalScrollBar()->value(), sy = view_->verticalScrollBar()->value();
    view_->setImage(display.isNull() ? document_.pixels : display);
    if (isFullScreen() && clearHistory)
        view_->setFit(QSettings().value(QStringLiteral("fullscreen/upscale"), false).toBool()
                          ? ImageView::Fit::Window
                          : ImageView::Fit::LargeOnly);
    else if (lockZoom_ || (!clearHistory && fit == ImageView::Fit::Original))
        view_->setZoom(z);
    else
        view_->setFit(browsingFit);
    if (lockScroll_) {
        view_->horizontalScrollBar()->setValue(sx);
        view_->verticalScrollBar()->setValue(sy);
    }
    if (!document_.path.isEmpty()) {
        updateRecent(QStringLiteral("recent"), document_.path);
        QSettings().setValue(QStringLiteral("files/directory"), QFileInfo(document_.path).absolutePath());
    }
    refreshFiles();
    refreshState();
    updateStatus();
    view_->setFocus(Qt::OtherFocusReason);
    if (document_.animated && !document_.modified && animationPlaying_)
        animationTimer_.start(document_.delayMs);
    else if (!document_.animated)
        stopAnimation();
}
void MainWindow::updatePreviews() {
    if (previewStrip_ && !previewStrip_->isHidden())
        previewStrip_->setFiles(files_, fileIndex_);
}
void MainWindow::refreshFiles(bool force) {
    if (document_.path.isEmpty() && watchedFolder_.isEmpty()) {
        directoryIndex_->cancel();
        files_.clear();
        filePositions_.clear();
        fileIndex_ = -1;
        indexedDirectory_.clear();
        indexReady_ = false;
        updatePreviews();
        return;
    }
    const auto directory =
        !watchedFolder_.isEmpty() ? watchedFolder_ : QFileInfo(document_.path).absolutePath();
    QSettings settings;
    int sorting = settings.value(QStringLiteral("navigation/sort"), 0).toInt();
    bool reverse = settings.value(QStringLiteral("navigation/reverse"), false).toBool();
    const bool same = directory == indexedDirectory_ && sorting == indexedSort_ && reverse == indexedReverse_;
    if (!force && same) {
        fileIndex_ = filePositions_.value(document_.path, -1);
        updatePreviews();
        return;
    }
    if (!same) {
        files_.clear();
        filePositions_.clear();
        fileIndex_ = -1;
    }
    indexedDirectory_ = directory;
    indexedSort_ = sorting;
    indexedReverse_ = reverse;
    if (!directoryWatcher_.directories().isEmpty())
        directoryWatcher_.removePaths(directoryWatcher_.directories());
    directoryWatcher_.addPath(directory);
    indexReady_ = false;
    const bool latest = !watchedFolder_.isEmpty();
    directoryIndex_->request({directory, registry_.extensions(), sorting, reverse, latest});
}
void MainWindow::navigate(int direction) {
    if (activity_.busy()) {
        if (activity_.workerActive() && (workMode_ == WorkMode::Load || workMode_ == WorkMode::Page))
            pendingNavigation_ = std::clamp(pendingNavigation_ + direction, -10000, 10000);
        return;
    }
    if (!indexReady_) {
        pendingNavigation_ = std::clamp(pendingNavigation_ + direction, -10000, 10000);
        return;
    }
    if (files_.isEmpty())
        return;
    direction = std::clamp(direction + std::exchange(pendingNavigation_, 0), -10000, 10000);
    int index = fileIndex_ + direction;
    if (QSettings().value(QStringLiteral("navigation/wrap"), true).toBool())
        index = (index % files_.size() + files_.size()) % files_.size();
    if (index >= 0 && index < files_.size())
        openPath(files_.at(index));
}
void MainWindow::page(int direction) {
    if (activity_.busy() || QApplication::activeModalWidget())
        return;
    if (document_.modified && !confirmDiscard(true))
        return;
    int index = document_.page + direction;
    if (index >= 0 && index < document_.pageCount)
        openPath(document_.path, index, false);
}
void MainWindow::pushUndo() {
    undo_.append({document_, view_->selection()});
    qint64 bytes = 0;
    for (const auto &d : undo_)
        bytes += d.pixels.sizeInBytes();
    while (undo_.size() > 1 && (bytes > 128LL * 1024 * 1024 || undo_.size() > 12)) {
        bytes -= undo_.front().pixels.sizeInBytes();
        undo_.removeFirst();
    }
}
void MainWindow::restoreHistory(bool redo) {
    const auto &history = redo ? redo_ : undo_;
    if (history.isEmpty())
        return;
    stopAnimation();
    const auto entry = history.constLast();
    const bool manage = QSettings().value(QStringLiteral("color/manage"), true).toBool();
    const bool sharp = displaySharpen_;
    beginWork([entry, manage, sharp] {
        return WorkResult{entry, displayImage(entry.pixels, manage, sharp), {}, entry.selection};
    }, redo ? WorkMode::Redo : WorkMode::Undo);
}
void MainWindow::operation(std::function<QImage(const QImage &)> fn) {
    if (document_.empty() || activity_.busy())
        return;
    if (document_.pixels.pixelFormat().typeInterpretation() == QPixelFormat::FloatingPoint &&
        QMessageBox::warning(this, IX_TEXT("HDR-Bild bearbeiten"),
                             IX_TEXT("Diese Operation verarbeitet maximal 16 Bit pro Kanal. HDR-Werte "
                                     "außerhalb 0–1 werden begrenzt. Fortfahren?"),
                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    stopAnimation();
    ImageDocument d = document_;
    bool manage = QSettings().value(QStringLiteral("color/manage"), true).toBool(), sharp = displaySharpen_;
    beginWork(
        [d, fn, manage, sharp]() mutable {
            QImage result = fn(d.pixels);
            if (result.isNull())
                return WorkResult{{}, {}, IX_TEXT("Bildoperation lieferte kein Bild.")};
            d.pixels = std::move(result);
            d.modified = true;
            auto display = displayImage(d.pixels, manage, sharp);
            return WorkResult{std::move(d), std::move(display), {}};
        },
        WorkMode::Edit);
}
void MainWindow::effect(std::function<QImage(const QImage &)> fn) {
    const auto selected = view_->selection();
    const bool inverted = invertedSelection_;
    operation([fn = std::move(fn), selected, inverted](const QImage &source) {
        return applySelectionEffect(source, selected, inverted, fn);
    });
}
void MainWindow::refreshState() {
    // The current document remains usable as a visual reference during a load.
    // Command entry points guard concurrent mutations; avoid flashing every
    // toolbar icon into its disabled palette on each navigation/animation frame.
    if (activity_.workerActive() && (workMode_ == WorkMode::Load || workMode_ == WorkMode::Page))
        return;
    for (auto *action : findChildren<QAction *>()) {
        QString key = action->data().toString();
        if (key.isEmpty())
            continue;
        bool enabled = globalCommands.contains(key) || !document_.empty();
        if (pathCommands.contains(key))
            enabled = !document_.path.isEmpty();
        if (selectionCommands.contains(key))
            enabled = enabled && !view_->selection().isEmpty();
        if (key == QStringLiteral("undo"))
            enabled = !undo_.isEmpty();
        if (key == QStringLiteral("redo"))
            enabled = !redo_.isEmpty();
        if (key == QStringLiteral("paste"))
            enabled =
                QApplication::clipboard()->mimeData() && (QApplication::clipboard()->mimeData()->hasImage() ||
                                                          QApplication::clipboard()->mimeData()->hasUrls());
        if (key == QStringLiteral("restoreSelection"))
            enabled = !document_.empty() && !lastSelection_.isEmpty();
        if (key == QStringLiteral("maximizeSelection"))
            enabled = !document_.empty() && !view_->selection().isEmpty();
        if (key == QStringLiteral("cropViewport"))
            enabled = !document_.empty() && (view_->horizontalScrollBar()->maximum() > 0 ||
                                             view_->verticalScrollBar()->maximum() > 0);
        if (key == QStringLiteral("animation"))
            enabled = document_.animated;
        if (key == QStringLiteral("jpegTransform") || key == QStringLiteral("jpegCrop"))
            enabled = enabled && !document_.path.isEmpty() &&
                      (document_.format == "jpeg" || document_.format == "jpg") && !document_.modified;
        if (key == QStringLiteral("extract"))
            enabled = document_.pageCount > 1;
        action->setEnabled(enabled && !activity_.busy());
    }
    for (auto *a : toolbar_->actions()) {
        if (a->objectName() == QStringLiteral("previousPage")) {
            a->setVisible(document_.pageCount > 1);
            a->setEnabled(!activity_.busy() && document_.page > 0);
        }
        if (a->objectName() == QStringLiteral("nextPage")) {
            a->setVisible(document_.pageCount > 1);
            a->setEnabled(!activity_.busy() && document_.page + 1 < document_.pageCount);
        }
    }
    zoom_->setEnabled(!activity_.busy() && !document_.empty());
}
void MainWindow::updateStatus() {
    view_->setInformationOverlay(isFullScreen() ? fullscreenInfoText(document_, fullscreenInfo_, view_->zoom())
                                              : QString(), fullscreenInfo_.corner);
    setWindowTitle((document_.modified ? QStringLiteral("*") : QString()) +
                   (document_.path.isEmpty() ? (document_.empty() ? QString() : IX_TEXT("Zwischenablage - "))
                                             : QFileInfo(document_.path).fileName() + QStringLiteral(" - ")) +
                   QStringLiteral("üxzView"));
    if (status_.isEmpty())
        return;
    if (document_.empty()) {
        status_[0]->clear();
        for (int i = 1; i < status_.size(); ++i)
            status_[i]->clear();
        return;
    }
    status_[0]->setText(QStringLiteral("%1 x %2 x %3 BPP")
                            .arg(document_.pixels.width())
                            .arg(document_.pixels.height())
                            .arg(document_.pixels.depth()));
    status_[1]->setText(fileIndex_ < 0 ? QString()
                                       : QStringLiteral("%1 / %2").arg(fileIndex_ + 1).arg(files_.size()));
    status_[2]->setText(QStringLiteral("%1 %").arg(view_->zoom() * 100, 0, 'f', 1));
    status_[3]->setText(QStringLiteral("%1 KB / %2 MB")
                            .arg(document_.fileBytes / 1024.0, 0, 'f', 1)
                            .arg(document_.pixels.sizeInBytes() / 1048576.0, 0, 'f', 2));
    status_[4]->setText(
        document_.path.isEmpty()
            ? QString()
            : QFileInfo(document_.path).lastModified().toString(QStringLiteral("dd.MM.yyyy HH:mm")));
    auto s = view_->selection();
    status_[5]->setText(
        s.isEmpty() ? (document_.pageCount > 1
                           ? IX_TEXT("Seite %1/%2").arg(document_.page + 1).arg(document_.pageCount)
                           : QString())
                    : QStringLiteral("%1,%2; %3 x %4").arg(s.x()).arg(s.y()).arg(s.width()).arg(s.height()));
}
void MainWindow::refreshToolbarIcons() {
    if (!toolbar_)
        return;
    const auto colors = palette();
    const auto background = colors.color(QPalette::Window);
    const auto ink = colors.color(QPalette::WindowText);
    auto blend = [&](qreal amount) {
        return QColor::fromRgbF(background.redF() * (1 - amount) + ink.redF() * amount,
                                background.greenF() * (1 - amount) + ink.greenF() * amount,
                                background.blueF() * (1 - amount) + ink.blueF() * amount).name();
    };
    toolbar_->setPalette(colors);
    if (zoom_)
        zoom_->setPalette(colors);
    if (actionSearch_)
        actionSearch_->setPalette(colors);
    toolbar_->setStyleSheet(
        QStringLiteral("QToolBar#mainToolbar { border: none; spacing: 2px; padding: 4px; background: %4; }"
                       "QToolBar#mainToolbar QToolButton { border: 1px solid transparent; border-radius: 4px; "
                       "padding: 4px; background: transparent; }"
                       "QToolBar#mainToolbar QToolButton:hover { background: %1; }"
                       "QToolBar#mainToolbar QToolButton:pressed, QToolBar#mainToolbar QToolButton:checked "
                       "{ background: %2; }"
                       "QToolBar#mainToolbar QToolButton:focus { border-color: palette(highlight); }"
                       "QToolBar#mainToolbar::separator { background: %3; width: 1px; margin: 7px 5px; }"
                       "QToolBar#mainToolbar QLineEdit#actionSearch { border: 1px solid %3; "
                       "border-radius: 4px; padding: 5px 7px; background: %5; color: %6; }"
                       "QToolBar#mainToolbar QLineEdit#actionSearch:focus { border-color: palette(highlight); }")
            .arg(blend(.07), blend(.14), blend(.22), background.name(),
                 colors.color(QPalette::Base).name(), colors.color(QPalette::Text).name()));
    for (auto *action : toolbar_->actions()) {
        auto icon = action->property("ixIcon").toString();
        if (!icon.isEmpty())
            action->setIcon(uiIcon(icon, palette()));
    }
}
void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        refreshToolbarIcons();
}
void MainWindow::applySettings(bool applyFit) {
    QSettings s;
    view_->setDisplayCacheEnabled(s.value(QStringLiteral("performance/imageCacheMiB"), 192).toInt() > 0);
    ImageLoadCache::configure(s.value(QStringLiteral("performance/imageCacheMiB"), 192).toInt(),
                              s.value(QStringLiteral("performance/previewCacheMiB"), 16).toInt(),
                              s.value(QStringLiteral("performance/rawPreview"), true).toBool());
    const int iconSize = std::clamp(s.value(QStringLiteral("toolbar/iconSize"), 24).toInt(), 20, 32);
    toolbar_->setIconSize({iconSize, iconSize});
    toolbar_->setToolButtonStyle(s.value(QStringLiteral("toolbar/labels"), false).toBool()
                                     ? Qt::ToolButtonTextUnderIcon
                                     : Qt::ToolButtonIconOnly);
    refreshToolbarIcons();
    if (auto *toggle = findChild<QAction *>(QStringLiteral("previewStripToggle")))
        toggle->setChecked(s.value(QStringLiteral("view/previewStrip"), false).toBool());
    if (auto *toggle = findChild<QAction *>(QStringLiteral("actionSearchToggle")))
        toggle->setChecked(s.value(QStringLiteral("view/actionSearch"), false).toBool());
    lockZoom_ = s.value(QStringLiteral("view/lockZoom"), false).toBool();
    lockScroll_ = s.value(QStringLiteral("view/lockScroll"), false).toBool();
    displaySharpen_ = s.value(QStringLiteral("view/sharpenDisplay"), false).toBool();
    if (applyFit && !isFullScreen())
        view_->setFit(ImageView::Fit(
            std::clamp(s.value(QStringLiteral("view/fit"), int(ImageView::Fit::LargeOnly)).toInt(), 0, 5)));
    view_->setSmooth(s.value(QStringLiteral("view/smooth"), true).toBool());
    auto check = [&](QString cmd, bool value) {
        if (auto *a = actions_.value(cmd))
            a->setChecked(value);
    };
    check(QStringLiteral("lockZoom"), lockZoom_);
    check(QStringLiteral("lockScroll"), lockScroll_);
    check(QStringLiteral("sharpenDisplay"), displaySharpen_);
    check(QStringLiteral("smooth"), s.value(QStringLiteral("view/smooth"), true).toBool());
    check(QStringLiteral("smoothFit"), s.value(QStringLiteral("view/smooth"), true).toBool());
    check(QStringLiteral("center"), s.value(QStringLiteral("view/center"), true).toBool());
    check(QStringLiteral("statusbar"), s.value(QStringLiteral("window/statusbar"), true).toBool());
    check(QStringLiteral("toolbar"), s.value(QStringLiteral("window/toolbar"), true).toBool());
    check(QStringLiteral("menubar"), s.value(QStringLiteral("window/menubar"), true).toBool());
    check(QStringLiteral("titlebar"), !windowFlags().testFlag(Qt::FramelessWindowHint));
    slideshow_.setInterval(int(s.value(QStringLiteral("slideshow/seconds"), 5).toDouble() * 1000));
    configureCaptureShortcut();
    applyWindowAppearance(isFullScreen());
}
void MainWindow::applyWindowAppearance(bool fullscreen) {
    const QSettings settings;
    fullscreenInfo_ = fullscreenInfoOptions();
    view_->setInformationOverlay(fullscreen ? fullscreenInfoText(document_, fullscreenInfo_, view_->zoom())
                                           : QString(), fullscreenInfo_.corner);
    view_->background = settings.value(fullscreen ? QStringLiteral("fullscreen/background")
                                                : QStringLiteral("view/background"), QColor(Qt::black)).value<QColor>();
    view_->viewport()->setAttribute(Qt::WA_OpaquePaintEvent, view_->background.alpha() == 255);
    view_->setCentered(fullscreen || settings.value(QStringLiteral("view/center"), true).toBool());
    menuBar()->setVisible(!fullscreen && settings.value(QStringLiteral("window/menubar"), true).toBool());
    toolbar_->setVisible(!fullscreen && settings.value(QStringLiteral("window/toolbar"), true).toBool());
    statusBar()->setVisible(!fullscreen && settings.value(QStringLiteral("window/statusbar"), true).toBool());
    if (previewStrip_) {
        previewStrip_->setVisible(!fullscreen && settings.value(QStringLiteral("view/previewStrip"), false).toBool());
        if (!previewStrip_->isHidden()) {
            previewStrip_->applySettings();
            updatePreviews();
        }
    }
    view_->viewport()->update();
}
void MainWindow::displayCurrent() {
    if (document_.empty())
        return;
    auto d = document_;
    bool manage = QSettings().value(QStringLiteral("color/manage"), true).toBool(), sharp = displaySharpen_;
    beginWork([=] { return WorkResult{d, displayImage(d.pixels, manage, sharp), {}}; }, WorkMode::Display);
}
void MainWindow::stopAnimation() {
    animationTimer_.stop();
    animationPlaying_ = false;
    animationEnded_ = false;
    animationLoops_ = 0;
}
void MainWindow::fullscreen() {
    // Commit an actual pending edit before saving the window's zoom state.
    if (zoom_->lineEdit()->hasFocus())
        view_->setFocus();
    WindowTransition transition(this);
    if (isFullScreen()) {
        view_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        view_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        view_->viewport()->unsetCursor();
        view_->setProperty("hidePointer", false);
        applyWindowAppearance(false);
        if (windowWasMaximized_)
            showMaximized();
        else
            showNormal();
        layout()->activate();
        // Fit against the restored client and its visible bars/preview strip.
        view_->setFit(windowBrowsingFit_);
        if (windowFit_ == ImageView::Fit::Original)
            view_->setZoom(windowZoom_);
        view_->horizontalScrollBar()->setValue(windowScroll_.x());
        view_->verticalScrollBar()->setValue(windowScroll_.y());
    } else {
        windowFit_ = view_->fit();
        windowBrowsingFit_ = view_->browsingFit();
        windowGeometry_ = saveGeometry();
        windowZoom_ = view_->zoom();
        windowScroll_ = {view_->horizontalScrollBar()->value(), view_->verticalScrollBar()->value()};
        windowWasMaximized_ = isMaximized();
        applyWindowAppearance(true);
        view_->setFrameStyle(QFrame::NoFrame);
        view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view_->setSelection({});
        if (QSettings().value(QStringLiteral("fullscreen/hideCursor"), true).toBool())
            view_->viewport()->setCursor(Qt::BlankCursor);
        view_->setProperty("hidePointer",
                           QSettings().value(QStringLiteral("fullscreen/hideCursor"), true).toBool());
        showFullScreen();
        layout()->activate();
        view_->setFit(QSettings().value(QStringLiteral("fullscreen/upscale"), false).toBool()
                          ? ImageView::Fit::Window
                          : ImageView::Fit::LargeOnly);
    }
    updateStatus();
}
void MainWindow::configureCaptureShortcut() {
    if (auto *action = actions_.value(QStringLiteral("capture"))) {
        const auto sequence = QKeySequence::fromString(
            QSettings()
                .value(QStringLiteral("capture/shortcut"), QStringLiteral("Ctrl+Shift+F12"))
                .toString(),
            QKeySequence::PortableText);
        const bool global = QSettings().value(QStringLiteral("capture/globalShortcut"), false).toBool();
        const auto error = configureGlobalCaptureShortcut(this, global ? sequence : QKeySequence(),
                                                          [this] { dispatch(QStringLiteral("capture")); });
        action->setShortcut(global && error.isEmpty() ? QKeySequence() : sequence);
        action->setToolTip(error.isEmpty() ? sequence.toString(QKeySequence::NativeText) : error);
    }
}
void MainWindow::advanceSlideshow() {
    if (activity_.busy() || deferredOpen_ || QApplication::activeModalWidget() || !indexReady_ || files_.isEmpty())
        return;
    QSettings settings;
    const auto next = slideshow_.next(files_, fileIndex_,
                                      settings.value(QStringLiteral("slideshow/random"), false).toBool(),
                                      settings.value(QStringLiteral("slideshow/repeat"), true).toBool());
    if (next.finished) {
        slideshow_.stop();
        if (slideshowFullscreen_ && isFullScreen())
            fullscreen();
        slideshowFullscreen_ = false;
    } else if (!next.path.isEmpty())
        openPath(next.path);
}
void MainWindow::startSlideshow(bool dialog) {
    if (slideshow_.active()) {
        slideshow_.stop();
        if (slideshowFullscreen_ && isFullScreen())
            fullscreen();
        slideshowFullscreen_ = false;
        return;
    }
    if (dialog) {
        if (document_.empty()) {
            auto p = QFileDialog::getOpenFileName(this, IX_TEXT("Slideshow – erstes Bild"), {},
                                                  registry_.openFilter());
            if (p.isEmpty())
                return;
            openPath(p);
        }
        bool ok = false;
        double secs = QInputDialog::getDouble(
            this, IX_TEXT("Slideshow"), IX_TEXT("Automatischer Bildwechsel nach (Sekunden):"),
            QSettings().value(QStringLiteral("slideshow/seconds"), 5).toDouble(), .1, 3600, 1, &ok);
        if (!ok)
            return;
        QSettings().setValue(QStringLiteral("slideshow/seconds"), secs);
        slideshow_.setInterval(int(secs * 1000));
    }
    if (!document_.empty() || activity_.busy()) {
        slideshowFullscreen_ =
            QSettings().value(QStringLiteral("slideshow/fullscreen"), true).toBool() && !isFullScreen();
        if (slideshowFullscreen_)
            fullscreen();
        slideshow_.start();
    }
}
void MainWindow::dispatch(const QString &cmd) {
    if (cmd == QStringLiteral("next") || cmd == QStringLiteral("previous")) {
        if (!QApplication::activeModalWidget() && (!document_.empty() || activity_.workerActive()))
            navigate(cmd == QStringLiteral("next") ? 1 : -1);
        return;
    }
    if (activity_.busy() || QApplication::activeModalWidget())
        return;
    if (cmd == QStringLiteral("open")) {
        auto p = QFileDialog::getOpenFileName(this, IX_TEXT("Öffnen"),
                                              QSettings().value(QStringLiteral("files/directory")).toString(),
                                              registry_.openFilter());
        if (!p.isEmpty())
            openPath(p);
        return;
    }
    if (cmd == QStringLiteral("exit")) {
        if (isFullScreen())
            fullscreen();
        else
            close();
        return;
    }
    if (cmd == QStringLiteral("new")) {
        if (!confirmDiscard())
            return;
        ImageDocument d;
        if (newImageDialog(this, d)) {
            stopAnimation();
            installDocument(std::move(d));
        }
        return;
    }
    if (cmd == QStringLiteral("settings") || cmd == QStringLiteral("cropSettings")) {
        SettingsDialog dialog(this);
        if (cmd == QStringLiteral("cropSettings"))
            dialog.selectPage(QStringLiteral("cropSettingsPage"));
        if (dialog.exec() == QDialog::Accepted) {
            applySettings(true);
            refreshFiles(true);
            displayCurrent();
        }
        return;
    }
    if (cmd == QStringLiteral("cropAspect")) {
        if (!document_.empty())
            findChild<QMenu *>(QStringLiteral("cropAspectMenu"))->popup(QCursor::pos());
        return;
    }
    if (cmd == QStringLiteral("minimize")) {
        showMinimized();
        return;
    }
    if (cmd == QStringLiteral("onTop")) {
        bool top = actions_[cmd]->isChecked();
        setWindowFlag(Qt::WindowStaysOnTopHint, top);
        show();
        return;
    }
    if (cmd == QStringLiteral("statusbar") || cmd == QStringLiteral("toolbar") ||
        cmd == QStringLiteral("menubar")) {
        bool visible = actions_[cmd]->isChecked();
        if (cmd == QStringLiteral("statusbar"))
            statusBar()->setVisible(visible);
        else if (cmd == QStringLiteral("toolbar"))
            toolbar_->setVisible(visible);
        else
            menuBar()->setVisible(visible);
        QSettings().setValue(QStringLiteral("window/") + cmd, visible);
        return;
    }
    if (cmd == QStringLiteral("titlebar")) {
        setWindowFlag(Qt::FramelessWindowHint, !actions_[cmd]->isChecked());
        show();
        return;
    }
    if (cmd == QStringLiteral("batch")) {
        PipelineDialog dialog(document_.path.isEmpty() ? QStringList() : QStringList{document_.path}, this, journalRoot_);
        dialog.exec();
        ImageLoadCache::clear();
        reconcileFilePath();
        refreshFiles(true);
        return;
    }
    if (cmd == QStringLiteral("find")) {
        QString dir = QFileDialog::getExistingDirectory(this, IX_TEXT("Dateien suchen"));
        if (dir.isEmpty())
            return;
        QDir folder(dir);
        QStringList patterns;
        for (auto &ext : registry_.extensions())
            patterns << QStringLiteral("*.") + ext;
        QStringList matches;
        for (auto &file : folder.entryList(patterns, QDir::Files))
            matches << folder.filePath(file);
        libraryDialog(this, matches, [this](const QString &path) { openPath(path); }, journalRoot_);
        reconcileFilePath();
        return;
    }
    if (cmd == QStringLiteral("thumbnails")) {
        if (files_.isEmpty()) {
            dispatch(QStringLiteral("find"));
            return;
        }
        libraryDialog(this, files_, [this](const QString &path) { openPath(path); }, journalRoot_);
        reconcileFilePath();
        return;
    }
    if (cmd == QStringLiteral("slideshow") || cmd == QStringLiteral("slideshowDialog")) {
        startSlideshow(cmd == QStringLiteral("slideshowDialog"));
        return;
    }
    if (cmd == QStringLiteral("clearClipboard")) {
        QApplication::clipboard()->clear();
        refreshState();
        return;
    }
    if (cmd == QStringLiteral("paste")) {
        auto *cb = QApplication::clipboard();
        if (cb->mimeData() && cb->mimeData()->hasUrls()) {
            auto urls = cb->mimeData()->urls();
            if (!urls.isEmpty())
                openPath(urls.first().toLocalFile());
            return;
        }
        auto image = cb->image();
        if (!image.isNull() && confirmDiscard()) {
            if (!safeSize(image.size())) {
                showError(IX_TEXT("Zwischenablagebild zu groß."));
                return;
            }
            ImageDocument d;
            d.pixels = image;
            d.modified = true;
            installDocument(std::move(d));
        }
        return;
    }
    if (cmd == QStringLiteral("capture")) {
        if (!confirmDiscard())
            return;
        activity_.begin(DocumentActivity::Phase::Capture);
        refreshState();
        hide();
        QTimer::singleShot(
            std::clamp(QSettings().value(QStringLiteral("capture/delayMs"), 350).toInt(), 0, 10000), this,
            [this] {
                auto release = qScopeGuard([this] {
                    activity_.finish();
                    refreshState();
                });
                auto *screen = QSettings().value(QStringLiteral("capture/currentScreen"), true).toBool()
                                   ? QGuiApplication::screenAt(QCursor::pos())
                                   : QGuiApplication::primaryScreen();
                if (!screen)
                    screen = QGuiApplication::primaryScreen();
                auto img = screen ? screen->grabWindow(0).toImage() : QImage();
                show();
                if (img.isNull()) {
                    QString reason = IX_TEXT("Screenshot konnte nicht erstellt werden.");
#ifdef Q_OS_MACOS
                    reason += IX_TEXT(" Bitte die macOS-Berechtigung zur Bildschirmaufnahme prüfen.");
#elif defined(Q_OS_LINUX)
                    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland")))
                        reason += IX_TEXT(" Die globale Bildschirmaufnahme benötigt unter Wayland ein "
                                          "Portal; dieser Ablauf ist noch nicht implementiert.");
#endif
                    showError(reason);
                    return;
                }
                ImageDocument d;
                d.pixels = img;
                d.modified = true;
                installDocument(std::move(d));
            });
        return;
    }
    if (cmd == QStringLiteral("refresh")) {
        ImageLoadCache::clear();
        refreshFiles(true);
        refreshFiles();
        if (!document_.path.isEmpty())
            openPath(document_.path);
        return;
    }
    if (document_.empty())
        return;
    if (cmd == QStringLiteral("reopen")) {
        openPath(document_.path);
        return;
    }
    if (cmd == QStringLiteral("save") || cmd == QStringLiteral("saveAs") ||
        cmd == QStringLiteral("saveSelection")) {
        saveImage(cmd == QStringLiteral("saveSelection"), cmd != QStringLiteral("save"));
        return;
    }
    if (cmd == QStringLiteral("rename") || cmd == QStringLiteral("move") ||
        cmd == QStringLiteral("fileCopy") || cmd == QStringLiteral("delete")) {
        fileOperation(cmd);
        return;
    }
    if (cmd == QStringLiteral("undo") || cmd == QStringLiteral("redo")) {
        restoreHistory(cmd == QStringLiteral("redo"));
        return;
    }
    if (cmd == QStringLiteral("jpegTransform") || cmd == QStringLiteral("jpegCrop")) {
        if (document_.path.isEmpty() || document_.modified) {
            showError(IX_TEXT("Die verlustfreie JPEG-Operation verarbeitet die gespeicherte "
                              "Originaldatei. Bitte Änderungen zuerst speichern."));
            return;
        }
        if (document_.metadata &&
            document_.metadata->tags.value(QStringLiteral("Exif.Image.Orientation"), QStringLiteral("1")) !=
                QStringLiteral("1")) {
            showError(IX_TEXT("Verlustfreie Operation für JPEGs mit EXIF-Ausrichtung ungleich 1 ist "
                              "noch nicht unterstützt."));
            return;
        }
        JpegTransform transform = JpegTransform::Crop;
        auto crop = view_->selection();
        if (cmd == QStringLiteral("jpegTransform")) {
            bool ok = false;
            QStringList options{IX_TEXT("Links drehen"), IX_TEXT("Rechts drehen"), IX_TEXT("180 Grad drehen"),
                                IX_TEXT("Horizontal spiegeln"), IX_TEXT("Vertikal spiegeln")};
            auto chosen = QInputDialog::getItem(this, IX_TEXT("JPG - verlustfreie Rotation"),
                                                IX_TEXT("Transformation:"), options, 0, false, &ok);
            if (!ok)
                return;
            transform = JpegTransform(options.indexOf(chosen));
        }
        QString target = QFileDialog::getSaveFileName(
            this, IX_TEXT("Verlustfreies JPEG speichern"),
            QFileInfo(document_.path)
                .dir()
                .filePath(QFileInfo(document_.path).completeBaseName() + QStringLiteral("_lossless.jpg")),
            QStringLiteral("JPEG (*.jpg *.jpeg)"));
        if (target.isEmpty())
            return;
        auto source = document_.path;
        auto registry = registry_;
        bool manage = QSettings().value(QStringLiteral("color/manage"), true).toBool();
        beginWork(
            [=] {
                auto error = transformJpeg(source, target, transform, crop);
                if (!error.isEmpty())
                    return WorkResult{{}, {}, error};
                auto r = registry.read(target);
                if (!r.ok())
                    return WorkResult{{}, {}, r.error};
                return WorkResult{r.document, displayImage(r.document.pixels, manage, false), {}};
            },
            WorkMode::Load);
        return;
    }
    if (cmd == QStringLiteral("firstPage") || cmd == QStringLiteral("lastPage")) {
        if (document_.pageCount > 1)
            openPath(document_.path, cmd == QStringLiteral("firstPage") ? 0 : document_.pageCount - 1);
        return;
    }
    if (cmd == QStringLiteral("focusZoom")) {
        if (toolbar_->isVisible()) {
            zoom_->setFocus();
            zoom_->lineEdit()->selectAll();
        }
        return;
    }
    if (cmd == QStringLiteral("copyFilename")) {
        if (!document_.path.isEmpty())
            QApplication::clipboard()->setText(document_.path);
        return;
    }
    if (cmd == QStringLiteral("newWindow")) {
        if (!document_.path.isEmpty()) {
            auto *other = new MainWindow;
            other->setAttribute(Qt::WA_DeleteOnClose);
            other->show();
            other->openPath(document_.path);
        }
        return;
    }
    if (cmd == QStringLiteral("resize")) {
        ResizeDialog dialog(document_.pixels.size(), document_.dpiX(), this);
        if (dialog.exec() == QDialog::Accepted) {
            auto o = dialog.options();
            operation([o](const QImage &s) { return resample(s, o); });
        }
        return;
    }
    if (cmd == QStringLiteral("left") || cmd == QStringLiteral("right")) {
        operation([cmd](const QImage &s) { return rotate(s, cmd == QStringLiteral("left") ? -90 : 90); });
        return;
    }
    if (cmd == QStringLiteral("rotate")) {
        stopAnimation();
        RotationDialog dialog(document_.pixels, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        auto o = dialog.options();
        auto d = document_;
        if (o.sharpen && d.pixels.pixelFormat().typeInterpretation() == QPixelFormat::FloatingPoint &&
            QMessageBox::warning(this, IX_TEXT("HDR-Bild schärfen"),
                                 IX_TEXT("Schärfen verarbeitet maximal 16 Bit pro Kanal. HDR-Werte "
                                         "außerhalb 0–1 werden begrenzt. Fortfahren?"),
                                 QMessageBox::Yes | QMessageBox::Cancel,
                                 QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        bool manage = QSettings().value(QStringLiteral("color/manage"), true).toBool(),
             sharp = displaySharpen_;
        beginWork(
            [d, o, manage, sharp]() mutable {
                auto rotated = rotateImage(d.pixels, o);
                d.pixels = std::move(rotated.pixels);
                d.modified = true;
                auto display = displayImage(d.pixels, manage, sharp);
                return WorkResult{std::move(d), std::move(display), {}, rotated.selection};
            },
            WorkMode::Edit);
        return;
    }
    if (cmd == QStringLiteral("horizontal") || cmd == QStringLiteral("vertical")) {
        operation([cmd](const QImage &s) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            auto result = s.flipped(cmd == QStringLiteral("horizontal") ? Qt::Horizontal : Qt::Vertical);
#else
            auto result = s.mirrored(cmd == QStringLiteral("horizontal"), cmd == QStringLiteral("vertical"));
#endif
            inheritImageProperties(s, result);
            return result;
        });
        return;
    }
    if (cmd == QStringLiteral("selection")) {
        auto r = view_->selection();
        if (selectionDialog(this, document_.pixels.size(), r))
            view_->setSelection(r);
        return;
    }
    if (cmd == QStringLiteral("restoreSelection")) {
        view_->setSelection(lastSelection_);
        return;
    }
    if (cmd == QStringLiteral("maximizeSelection")) {
        view_->setSelection(centeredMaximalSelection(document_.pixels.size(), view_->selection().size()));
        return;
    }
    if (cmd == QStringLiteral("invertSelection")) {
        invertedSelection_ = !invertedSelection_;
        if (auto *a = actions_.value(cmd))
            a->setChecked(invertedSelection_);
        return;
    }
    if (cmd == QStringLiteral("cropViewport")) {
        if (view_->horizontalScrollBar()->maximum() == 0 && view_->verticalScrollBar()->maximum() == 0)
            return;
        auto r = view_->visibleImageRect();
        if (r.isEmpty())
            return;
        operation([r](const QImage &s) {
            auto result = s.copy(r);
            inheritImageProperties(s, result);
            return result;
        });
        return;
    }
    if (cmd == QStringLiteral("autoSelect")) {
        stopAnimation();
        auto d = document_;
        beginWork([d] { return WorkResult{d, {}, {}, autoCropRect(d.pixels)}; }, WorkMode::Selection);
        return;
    }
    if (cmd == QStringLiteral("autoCrop")) {
        operation([](const QImage &s) {
            auto image = s.copy(autoCropRect(s));
            inheritImageProperties(s, image);
            return image;
        });
        return;
    }
    if (cmd == QStringLiteral("copy")) {
        auto image =
            view_->selection().isEmpty() ? document_.pixels : document_.pixels.copy(view_->selection());
        QApplication::clipboard()->setImage(image);
        return;
    }
    if (cmd == QStringLiteral("cut") || cmd == QStringLiteral("cutOutside") ||
        cmd == QStringLiteral("crop")) {
        auto r = view_->selection();
        if (r.isEmpty())
            return;
        if (cmd == QStringLiteral("cut"))
            QApplication::clipboard()->setImage(document_.pixels.copy(r));
        QColor background = view_->background;
        operation([=](const QImage &s) {
            if (cmd == QStringLiteral("crop")) {
                auto image = s.copy(r);
                inheritImageProperties(s, image);
                return image;
            }
            QImage out = s.convertToFormat(s.depth() > 32 ? QImage::Format_RGBA64 : QImage::Format_ARGB32);
            QPainter p(&out);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            if (cmd == QStringLiteral("cut"))
                p.fillRect(r, background);
            else {
                QRegion region(s.rect());
                region -= r;
                p.setClipRegion(region);
                p.fillRect(s.rect(), background);
            }
            p.end();
            inheritImageProperties(s, out);
            return out;
        });
        return;
    }
    if (cmd == QStringLiteral("grid")) {
        view_->grid = actions_[cmd]->isChecked();
        view_->viewport()->update();
        return;
    }
    if (cmd == QStringLiteral("clear")) {
        if (confirmDiscard()) {
            stopAnimation();
            installDocument({});
        }
        return;
    }
    if (cmd == QStringLiteral("captureViewport")) {
        if (!confirmDiscard())
            return;
        ImageDocument d;
        d.pixels = view_->screenshot();
        d.modified = true;
        installDocument(std::move(d));
        return;
    }
    if (cmd == QStringLiteral("canvas")) {
        int l = 10, r = 10, t = 10, b = 10;
        QColor c = Qt::white;
        if (canvasDialog(this, l, r, t, b, c))
            operation([=](const QImage &s) { return canvas(s, l, r, t, b, c); });
        return;
    }
    if (cmd.startsWith(QStringLiteral("channel"))) {
        int channelIndex = cmd.right(1).toInt();
        effect([channelIndex](const QImage &s) { return channel(s, channelIndex); });
        return;
    }
    if (cmd.startsWith(QStringLiteral("negative"))) {
        int channelIndex = cmd.endsWith(QStringLiteral("All")) ? -1 : cmd.right(1).toInt();
        effect([channelIndex](const QImage &s) { return negative(s, channelIndex); });
        return;
    }
    if (cmd.startsWith(QStringLiteral("swap"))) {
        int a = cmd.mid(4, 1).toInt(), b = cmd.mid(5, 1).toInt();
        effect([a, b](const QImage &s) { return swapChannels(s, a, b); });
        return;
    }
    if (cmd == QStringLiteral("cycleBRG") || cmd == QStringLiteral("cycleGBR")) {
        effect([cmd](const QImage &s) {
            return cmd == QStringLiteral("cycleBRG") ? swapChannels(swapChannels(s, 0, 2), 1, 2)
                                                     : swapChannels(swapChannels(s, 0, 1), 1, 2);
        });
        return;
    }
    if (cmd == QStringLiteral("grayscale")) {
        effect(grayscale);
        return;
    }
    if (cmd == QStringLiteral("colors")) {
        ColorDialog dialog(document_.pixels, this);
        connect(&dialog, &ColorDialog::applyRequested, this, [this, &dialog](ColorAdjust values) {
            dialog.setEnabled(false);
            effect([=](const QImage &s) { return adjustColors(s, values); });
        });
        connect(&worker_, &QFutureWatcherBase::finished, &dialog, [this, &dialog] {
            dialog.setEnabled(true);
            dialog.setImage(document_.pixels);
        });
        if (dialog.exec() == QDialog::Accepted) {
            auto values = dialog.values();
            effect([=](const QImage &s) { return adjustColors(s, values); });
        }
        return;
    }
    if (cmd == QStringLiteral("autoCorrect")) {
        effect(autoCorrect);
        return;
    }
    if (cmd == QStringLiteral("sharpen")) {
        effect([](const QImage &s) { return convolve(s, {0, -1, 0, -1, 5, -1, 0, -1, 0}, 3); });
        return;
    }
    if (cmd == QStringLiteral("redEye")) {
        auto r = view_->selection();
        operation([r](const QImage &s) {
            auto result = s.convertToFormat(QImage::Format_RGBA64);
            for (int y = r.top(); y <= r.bottom(); ++y) {
                auto *row = reinterpret_cast<QRgba64 *>(result.scanLine(y));
                for (int x = r.left(); x <= r.right(); ++x) {
                    auto p = row[x];
                    if (p.red() > 1.5 * (p.green() + p.blue()) / 2)
                        row[x] =
                            QRgba64::fromRgba64((p.green() + p.blue()) / 2, p.green(), p.blue(), p.alpha());
                }
            }
            inheritImageProperties(s, result);
            return result;
        });
        return;
    }
    if (cmd == QStringLiteral("increaseDepth")) {
        bool ok = false;
        auto choice = QInputDialog::getItem(
            this, IX_TEXT("Farbtiefe erhöhen"), IX_TEXT("Farbtiefe:"),
            {QStringLiteral("24 BPP"), QStringLiteral("32 BPP (Alpha)"), IX_TEXT("64 BPP (16 Bit je Kanal)")},
            0, false, &ok);
        if (ok)
            operation([choice](const QImage &s) {
                auto img =
                    s.convertToFormat(choice.startsWith(QStringLiteral("24"))   ? QImage::Format_RGB888
                                      : choice.startsWith(QStringLiteral("32")) ? QImage::Format_ARGB32
                                                                                : QImage::Format_RGBA64);
                inheritImageProperties(s, img);
                return img;
            });
        return;
    }
    if (cmd == QStringLiteral("reduceDepth")) {
        bool ok = false;
        auto choice =
            QInputDialog::getItem(this, IX_TEXT("Farbtiefe reduzieren"), IX_TEXT("Farbtiefe:"),
                                  {QStringLiteral("24 BPP"), IX_TEXT("256 Farben (8 BPP)"),
                                   IX_TEXT("256 Graustufen (8 BPP)"), IX_TEXT("Schwarz/Weiß (1 BPP)")},
                                  0, false, &ok);
        if (ok)
            operation([choice](const QImage &s) {
                auto fmt = choice.startsWith(QStringLiteral("24"))    ? QImage::Format_RGB888
                           : choice.startsWith(IX_TEXT("256 Farben")) ? QImage::Format_Indexed8
                           : choice.startsWith(IX_TEXT("256 Grau"))   ? QImage::Format_Grayscale8
                                                                      : QImage::Format_Mono;
                auto image = s.convertToFormat(fmt, Qt::AutoColor | Qt::DiffuseDither);
                inheritImageProperties(s, image);
                return image;
            });
        return;
    }
    if (cmd == QStringLiteral("info")) {
        imageInformation(this, document_);
        return;
    }
    if (cmd == QStringLiteral("histogram")) {
        histogramDialog(this, document_.pixels);
        return;
    }
    if (cmd == QStringLiteral("first")) {
        if (!files_.isEmpty())
            openPath(files_.constFirst());
        return;
    }
    if (cmd == QStringLiteral("last")) {
        if (!files_.isEmpty())
            openPath(files_.constLast());
        return;
    }
    if (cmd == QStringLiteral("random")) {
        if (!files_.isEmpty())
            openPath(files_.at(QRandomGenerator::global()->bounded(files_.size())));
        return;
    }
    if (cmd == QStringLiteral("zoomIn")) {
        view_->setZoom(view_->zoom() * 1.25);
        return;
    }
    if (cmd == QStringLiteral("zoomOut")) {
        view_->setZoom(view_->zoom() * .8);
        return;
    }
    if (cmd == QStringLiteral("lockZoom")) {
        lockZoom_ = actions_[cmd]->isChecked();
        QSettings().setValue(QStringLiteral("view/lockZoom"), lockZoom_);
        return;
    }
    if (cmd == QStringLiteral("lockScroll")) {
        lockScroll_ = actions_[cmd]->isChecked();
        QSettings().setValue(QStringLiteral("view/lockScroll"), lockScroll_);
        return;
    }
    if (cmd == QStringLiteral("original") || cmd == QStringLiteral("fitOriginal") ||
        cmd == QStringLiteral("noFit")) {
        view_->setFit(ImageView::Fit::Original);
        QSettings().setValue(QStringLiteral("view/fit"), int(ImageView::Fit::Original));
        if (cmd == QStringLiteral("fitOriginal")) {
            auto target = document_.pixels.size() + QSize(20, 110);
            target = target.boundedTo(screen()->availableGeometry().size());
            resize(target);
        }
        return;
    }
    if (cmd.startsWith(QStringLiteral("fit"))) {
        auto fit = cmd == QStringLiteral("fitWindow") || cmd == QStringLiteral("fitScreen")
                       ? ImageView::Fit::Window
                   : cmd == QStringLiteral("fitWidth") || cmd == QStringLiteral("fitScreenWidth")
                       ? ImageView::Fit::Width
                   : cmd == QStringLiteral("fitHeight") || cmd == QStringLiteral("fitScreenHeight")
                       ? ImageView::Fit::Height
                   : cmd == QStringLiteral("fitShort") || cmd == QStringLiteral("fitScreenShort")
                       ? ImageView::Fit::ShortSide
                       : ImageView::Fit::LargeOnly;
        if (cmd.startsWith(QStringLiteral("fitScreen")))
            resize(screen()->availableGeometry().size().boundedTo(document_.pixels.size() + QSize(20, 110)));
        view_->setFit(fit);
        QSettings().setValue(QStringLiteral("view/fit"), int(fit));
        return;
    }
    if (cmd == QStringLiteral("smooth") || cmd == QStringLiteral("smoothFit") ||
        cmd == QStringLiteral("center")) {
        QSettings().setValue(cmd == QStringLiteral("center") ? QStringLiteral("view/center")
                                                             : QStringLiteral("view/smooth"),
                             actions_[cmd]->isChecked());
        applySettings();
        return;
    }
    if (cmd == QStringLiteral("sharpenDisplay")) {
        displaySharpen_ = actions_[cmd]->isChecked();
        QSettings().setValue(QStringLiteral("view/sharpenDisplay"), displaySharpen_);
        displayCurrent();
        return;
    }
    if (cmd == QStringLiteral("fullscreen")) {
        fullscreen();
        return;
    }
    if (cmd == QStringLiteral("animation")) {
        if (animationPlaying_) {
            animationTimer_.stop();
            animationPlaying_ = false;
            return;
        }
        if (!confirmDiscard(true) || !document_.animated)
            return;
        animationPlaying_ = true;
        if (animationEnded_) {
            animationEnded_ = false;
            animationLoops_ = 0;
            openPath(document_.path, 0, false);
        } else
            animationTimer_.start(document_.delayMs);
        return;
    }
    if (cmd == QStringLiteral("hotfolder")) {
        if (!watchedFolder_.isEmpty()) {
            hotfolder_.removePath(watchedFolder_);
            watchedFolder_.clear();
        } else {
            watchedFolder_ = QFileInfo(document_.path).absolutePath();
            hotfolder_.addPath(watchedFolder_);
        }
        actions_[cmd]->setChecked(!watchedFolder_.isEmpty());
        return;
    }
    if (cmd == QStringLiteral("extract")) {
        QString directory = QFileDialog::getExistingDirectory(this, IX_TEXT("Extrahiere alle Bilder"));
        if (directory.isEmpty())
            return;
        QString path = document_.path;
        int count = document_.pageCount;
        auto registry = registry_;
        QProgressDialog progress(IX_TEXT("Bilder werden extrahiert …"), IX_TEXT("Abbrechen"), 0, count, this);
        progress.setWindowModality(Qt::ApplicationModal);
        auto cancel = std::make_shared<std::atomic_bool>(false);
        QFutureWatcher<QString> watcher;
        connect(&progress, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
        connect(&watcher, &QFutureWatcherBase::finished, &progress, &QDialog::accept);
        watcher.setFuture(QtConcurrent::run([=] {
            for (int i = 0; i < count && !cancel->load(); ++i) {
                auto image = registry.read(path, i);
                if (!image.ok())
                    return image.error;
                auto name = QDir(directory).filePath(QStringLiteral("%1_%2.png")
                                                         .arg(QFileInfo(path).completeBaseName())
                                                         .arg(i + 1, 5, 10, QLatin1Char('0')));
                if (QFileInfo::exists(name))
                    return IX_TEXT("Zieldatei existiert bereits: ") + name;
                auto error = registry.save(image.document, name, QByteArray("png"), 95, true);
                if (!error.isEmpty())
                    return error;
            }
            return QString();
        }));
        progress.exec();
        cancel->store(true);
        watcher.waitForFinished();
        if (!watcher.result().isEmpty())
            showError(watcher.result());
        return;
    }
    if (cmd == QStringLiteral("print")) {
        QPrinter printer;
        QPrintDialog dialog(&printer, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        QPainter painter(&printer);
        if (!painter.isActive()) {
            showError(IX_TEXT("Drucker konnte nicht geöffnet werden."));
            return;
        }
        QRect page = printer.pageLayout().paintRectPixels(printer.resolution());
        QSize size = document_.pixels.size().scaled(page.size(), Qt::KeepAspectRatio);
        QRect target(QPoint((page.width() - size.width()) / 2, (page.height() - size.height()) / 2), size);
        painter.drawImage(target, document_.pixels);
        return;
    }
    if (cmd == QStringLiteral("text")) {
        bool ok = false;
        QString text =
            QInputDialog::getMultiLineText(this, IX_TEXT("Text einfügen"), IX_TEXT("Text:"), {}, &ok);
        if (!ok || text.isEmpty())
            return;
        QFont font = QFontDialog::getFont(&ok, QFont(QStringLiteral("Arial"), 24), this);
        if (!ok)
            return;
        QColor color = QColorDialog::getColor(Qt::white, this, IX_TEXT("Textfarbe"));
        if (!color.isValid())
            return;
        QRect area = view_->selection().isEmpty() ? document_.pixels.rect() : view_->selection();
        operation([=](const QImage &s) {
            auto image = s.convertToFormat(s.depth() > 32 ? QImage::Format_RGBA64 : QImage::Format_ARGB32);
            QPainter painter(&image);
            painter.setFont(font);
            painter.setPen(color);
            painter.drawText(area, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
            painter.end();
            inheritImageProperties(s, image);
            return image;
        });
        return;
    }
    if (cmd == QStringLiteral("hex")) {
        QFile file(document_.path);
        if (!file.open(QIODevice::ReadOnly)) {
            showError(file.errorString());
            return;
        }
        auto bytes = file.read(1024 * 1024);
        QString text;
        for (int i = 0; i < bytes.size(); i += 16) {
            auto line = bytes.mid(i, 16);
            text += QStringLiteral("%1  %2\n")
                        .arg(i, 8, 16, QLatin1Char('0'))
                        .arg(QString::fromLatin1(line.toHex(' ')));
        }
        QDialog d(this);
        d.setWindowTitle(IX_TEXT("HEX-Viewer (erste 1 MiB)"));
        d.resize(700, 550);
        auto *v = new QVBoxLayout(&d);
        auto *edit = new QPlainTextEdit(text);
        edit->setReadOnly(true);
        edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        v->addWidget(edit);
        d.exec();
        return;
    }
    if (cmd == QStringLiteral("compare")) {
        QString path =
            QFileDialog::getOpenFileName(this, IX_TEXT("Bilder vergleichen"),
                                         QFileInfo(document_.path).absolutePath(), registry_.openFilter());
        if (path.isEmpty())
            return;
        auto *other = new MainWindow;
        other->setAttribute(Qt::WA_DeleteOnClose);
        other->resize(size());
        other->show();
        other->openPath(path);
        return;
    }
    showError(IX_TEXT("Unbekannte Aktion: ") + cmd);
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if (activity_.busy()) {
        e->ignore();
        return;
    }
    if (!confirmDiscard()) {
        e->ignore();
        return;
    }
    QSettings s;
    if (s.value(QStringLiteral("window/rememberGeometry"), true).toBool())
        s.setValue(QStringLiteral("window/geometry"),
                   isFullScreen() && !windowGeometry_.isEmpty() ? windowGeometry_ : saveGeometry());
    s.setValue(QStringLiteral("view/fit"),
               int(isFullScreen() ? windowBrowsingFit_ : view_->browsingFit()));
    slideshow_.stop();
    stopAnimation();
    deferredOpenTimer_.stop();
    deferredOpen_.reset();
    e->accept();
}
void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *e) {
    auto urls = e->mimeData()->urls();
    if (!urls.isEmpty())
        openPath(urls.first().toLocalFile());
}
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (zoom_ && object == zoom_->lineEdit() && event->type() == QEvent::FocusOut)
        commitZoomEdit();
    if (zoom_ && object == zoom_->lineEdit() &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape) {
            if (event->type() == QEvent::ShortcutOverride) {
                key->accept();
                return true;
            }
            if (key->key() != Qt::Key_Escape)
                commitZoomEdit();
            zoomEdited_ = false;
            zoom_->setEditText(QString::number(view_->zoom() * 100, 'f', 1));
            zoom_->lineEdit()->setModified(false);
            view_->setFocus();
            key->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(object, event);
}
void MainWindow::commitZoomEdit() {
    if (!std::exchange(zoomEdited_, false))
        return;
    zoom_->lineEdit()->setModified(false);
    if (activity_.busy())
        return;
    bool ok = false;
    double value = QLocale().toDouble(zoom_->currentText(), &ok);
    if (!ok)
        value = zoom_->currentText().toDouble(&ok);
    if (ok && value > 0)
        view_->setZoom(value / 100);
}
void MainWindow::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        if (isFullScreen())
            fullscreen();
        else if (slideshow_.active())
            slideshow_.stop();
        else
            close();
        return;
    }
    if (view_->handleKey(e)) {
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal) {
        dispatch(QStringLiteral("zoomIn"));
        return;
    }
    QMainWindow::keyPressEvent(e);
}
} // namespace ix
