// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_dialog.h"
#include "crop_ratio_editor.h"
#include "core/localization.h"
#include "core/app_storage.h"
#include "core/preview_service.h"
#include "image_view.h"
#include "fullscreen_info.h"
#include <QtWidgets>
#include <QtConcurrent>
namespace ix {
SettingsDialog::SettingsDialog(QWidget *parent, QString storageHome) : QDialog(parent) {
    storageHome_ = storageHome.isEmpty() ? AppStorage::applicationHome() : storageHome;
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(IX_TEXT("Einstellungen"));
    resize(860, 580);
    QSettings settings;
    auto *layout = new QVBoxLayout(this);
    auto *body = new QHBoxLayout;
    categories_ = new QListWidget;
    categories_->setObjectName(QStringLiteral("settingsCategories"));
    categories_->setAccessibleName(IX_TEXT("Einstellungsbereiche"));
    categories_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categories_->setSpacing(2);
    pages_ = new QStackedWidget;
    pages_->setObjectName(QStringLiteral("settingsPages"));
    body->addWidget(categories_);
    body->addWidget(pages_, 1);
    layout->addLayout(body, 1);
    connect(categories_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    auto page = [this](const QString &label) {
        auto *widget = new QWidget;
        auto *outer = new QVBoxLayout(widget);
        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        outer->addLayout(form);
        outer->addStretch();
        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setWidget(widget);
        pages_->addWidget(scroll);
        categories_->addItem(label);
        return form;
    };
    auto check = [&](QFormLayout *form, const QString &key, const QString &label, bool fallback) {
        auto *box = new QCheckBox(label);
        box->setObjectName(key);
        box->setChecked(settings.value(key, fallback).toBool());
        checks_.insert(key, box);
        form->addRow(box);
        return box;
    };
    auto number = [&](QFormLayout *form, const QString &key, const QString &label, int fallback, int minimum,
                      int maximum) {
        auto *box = new QSpinBox;
        box->setObjectName(key);
        box->setRange(minimum, maximum);
        box->setValue(settings.value(key, fallback).toInt());
        integers_.insert(key, box);
        form->addRow(label, box);
        return box;
    };
    auto *display = page(IX_TEXT("Anzeige"));
    fit_ = new QComboBox;
    fit_->setObjectName(QStringLiteral("view/fit"));
    const QStringList fitLabels{IX_TEXT("Originalgröße (100 %)"),        IX_TEXT("An Fenster anpassen"),
                                IX_TEXT("Nur große Bilder verkleinern"), IX_TEXT("An Fensterbreite anpassen"),
                                IX_TEXT("An Fensterhöhe anpassen"),      IX_TEXT("Kurze Seite anpassen")};
    for (int i = 0; i < fitLabels.size(); ++i)
        fit_->addItem(fitLabels[i], i);
    fit_->setCurrentIndex(
        std::clamp(settings.value(QStringLiteral("view/fit"), int(ImageView::Fit::LargeOnly)).toInt(), 0, 5));
    display->addRow(IX_TEXT("Bildanpassung beim Öffnen:"), fit_);
    check(display, QStringLiteral("view/center"), IX_TEXT("Bilder in der Bildfläche zentrieren"), true);
    check(display, QStringLiteral("view/smooth"), IX_TEXT("Vergrößerte und verkleinerte Anzeige glätten"),
          true);
    check(display, QStringLiteral("color/manage"),
          IX_TEXT("Eingebettete ICC-Profile für die Anzeige nach sRGB umrechnen"), true);
    check(display, QStringLiteral("view/sharpenDisplay"), IX_TEXT("Anzeige nachschärfen"), false);
    background_ = settings.value(QStringLiteral("view/background"), QColor(Qt::black)).value<QColor>();
    auto *background = new QPushButton;
    background->setObjectName(QStringLiteral("view/background"));
    auto updateColor = [this, background] {
        background->setText(IX_TEXT("Hintergrundfarbe: %1 …").arg(background_.name().toUpper()));
    };
    updateColor();
    display->addRow(background);
    connect(background, &QPushButton::clicked, this, [this, updateColor] {
        auto selected = QColorDialog::getColor(background_, this, IX_TEXT("Hintergrund der Bildfläche"));
        if (selected.isValid()) {
            background_ = selected;
            updateColor();
        }
    });
    auto *surface = page(IX_TEXT("Oberfläche"));
    language_ = new QComboBox;
    language_->setObjectName(QStringLiteral("ui/language"));
    for (const auto &entry :
         QList<QPair<QString, QString>>{{QStringLiteral("Deutsch"), QStringLiteral("de")},
                                        {QStringLiteral("English"), QStringLiteral("en")},
                                        {QStringLiteral("한국어"), QStringLiteral("ko")},
                                        {QStringLiteral("日本語"), QStringLiteral("ja")},
                                        {QStringLiteral("Français"), QStringLiteral("fr")}})
        language_->addItem(entry.first, entry.second);
    language_->setCurrentIndex(std::max(
        0, language_->findData(settings.value(QStringLiteral("ui/language"), QStringLiteral("de")))));
    surface->addRow(IX_TEXT("Sprache (nach Neustart):"), language_);
    check(surface, QStringLiteral("window/toolbar"), IX_TEXT("Symbolleiste anzeigen"), true);
    check(surface, QStringLiteral("window/statusbar"), IX_TEXT("Statusleiste anzeigen"), true);
    check(surface, QStringLiteral("window/menubar"), IX_TEXT("Menüleiste anzeigen"), true);
    check(surface, QStringLiteral("view/actionSearch"),
          IX_TEXT("Befehlssuche anzeigen"), false);
    iconSize_ = new QComboBox;
    iconSize_->setObjectName(QStringLiteral("toolbar/iconSize"));
    for (int size : {20, 24, 28, 32})
        iconSize_->addItem(QStringLiteral("%1 px").arg(size), size);
    iconSize_->setCurrentIndex(
        std::max(0, iconSize_->findData(settings.value(QStringLiteral("toolbar/iconSize"), 24).toInt())));
    surface->addRow(IX_TEXT("Symbolgröße:"), iconSize_);
    check(surface, QStringLiteral("toolbar/labels"), IX_TEXT("Beschriftungen unter den Symbolen anzeigen"),
          false);
    check(surface, QStringLiteral("window/rememberGeometry"), IX_TEXT("Fenstergröße und Position merken"),
          true);
    auto *previews = page(IX_TEXT("Vorschauen"));
    auto *enabled = check(previews, QStringLiteral("view/previewStrip"),
                          IX_TEXT("Nachbarbilder unter dem aktuellen Bild anzeigen"), false);
    auto *previous =
        number(previews, QStringLiteral("preview/previous"), IX_TEXT("Vorherige Bilder:"), 2, 1, 5);
    auto *next = number(previews, QStringLiteral("preview/next"), IX_TEXT("Kommende Bilder:"), 2, 1, 5);
    connect(enabled, &QCheckBox::toggled, previous, &QWidget::setEnabled);
    connect(enabled, &QCheckBox::toggled, next, &QWidget::setEnabled);
    previous->setEnabled(enabled->isChecked());
    next->setEnabled(enabled->isChecked());
    previewBackground_ = new QComboBox;
    previewBackground_->setObjectName(QStringLiteral("preview/background"));
    previewBackground_->addItem(IX_TEXT("Systemdesign"), QStringLiteral("theme"));
    previewBackground_->addItem(IX_TEXT("Wie Bildhintergrund"), QStringLiteral("image"));
    previewBackground_->addItem(IX_TEXT("Transparent"), QStringLiteral("transparent"));
    previewBackground_->setCurrentIndex(std::max(0, previewBackground_->findData(
        settings.value(QStringLiteral("preview/background"), QStringLiteral("theme")))));
    previews->addRow(IX_TEXT("Hintergrund der Vorschaubilder:"), previewBackground_);
    connect(enabled, &QCheckBox::toggled, previewBackground_, &QWidget::setEnabled);
    previewBackground_->setEnabled(enabled->isChecked());
    auto *navigation = page(IX_TEXT("Navigation"));
    sort_ = new QComboBox;
    sort_->setObjectName(QStringLiteral("navigation/sort"));
    sort_->addItems({IX_TEXT("Name (natürlich: 2 vor 10)"), IX_TEXT("Name (alphabetisch)"),
                     IX_TEXT("Änderungsdatum"), IX_TEXT("Dateigröße")});
    sort_->setCurrentIndex(std::clamp(settings.value(QStringLiteral("navigation/sort"), 0).toInt(), 0, 3));
    navigation->addRow(IX_TEXT("Dateien sortieren nach:"), sort_);
    check(navigation, QStringLiteral("navigation/reverse"), IX_TEXT("Absteigend sortieren"), false);
    check(navigation, QStringLiteral("navigation/wrap"),
          IX_TEXT("Nach dem letzten Bild wieder beim ersten beginnen"), true);
    check(navigation, QStringLiteral("navigation/askToSave"),
          IX_TEXT("Beim Bildwechsel nachfragen, ob Änderungen gespeichert werden sollen"), false);
    check(navigation, QStringLiteral("view/lockZoom"), IX_TEXT("Zoomfaktor beim Bildwechsel beibehalten"),
          false);
    check(navigation, QStringLiteral("view/lockScroll"),
          IX_TEXT("Scrollposition beim Bildwechsel beibehalten"), false);
    auto *fullscreen = page(IX_TEXT("Vollbild"));
    check(fullscreen, QStringLiteral("fullscreen/upscale"),
          IX_TEXT("Kleine Bilder auf Bildschirmgröße vergrößern"), false);
    check(fullscreen, QStringLiteral("fullscreen/hideCursor"), IX_TEXT("Mauszeiger ausblenden"), true);
    fullscreenBackground_ =
        settings.value(QStringLiteral("fullscreen/background"), QColor(Qt::black)).value<QColor>();
    auto *fullColor = new QPushButton;
    fullColor->setObjectName(QStringLiteral("fullscreen/background"));
    auto updateFullColor = [this, fullColor] {
        fullColor->setText(IX_TEXT("Hintergrundfarbe: %1 …").arg(fullscreenBackground_.name().toUpper()));
    };
    updateFullColor();
    fullscreen->addRow(fullColor);
    fullscreenInfo_ = new FullscreenInfoEditor;
    fullscreen->addRow(fullscreenInfo_);
    connect(fullColor, &QPushButton::clicked, this, [this, updateFullColor] {
        auto color = QColorDialog::getColor(fullscreenBackground_, this, IX_TEXT("Vollbild-Hintergrund"));
        if (color.isValid()) {
            fullscreenBackground_ = color;
            updateFullColor();
        }
    });
    auto *slideshow = page(IX_TEXT("Diashow"));
    seconds_ = new QDoubleSpinBox;
    seconds_->setObjectName(QStringLiteral("slideshow/seconds"));
    seconds_->setRange(.1, 3600);
    seconds_->setDecimals(1);
    seconds_->setSuffix(QStringLiteral(" s"));
    seconds_->setValue(settings.value(QStringLiteral("slideshow/seconds"), 5).toDouble());
    slideshow->addRow(IX_TEXT("Anzeigedauer je Bild:"), seconds_);
    check(slideshow, QStringLiteral("slideshow/fullscreen"), IX_TEXT("Diashow im Vollbild starten"), true);
    check(slideshow, QStringLiteral("slideshow/repeat"), IX_TEXT("Nach dem letzten Bild wiederholen"), true);
    check(slideshow, QStringLiteral("slideshow/random"), IX_TEXT("Zufällige Reihenfolge"), false);
    auto *capture = page(IX_TEXT("Screenshots"));
    captureShortcut_ = new QKeySequenceEdit(QKeySequence::fromString(
        settings.value(QStringLiteral("capture/shortcut"), QStringLiteral("Ctrl+Shift+F12")).toString(),
        QKeySequence::PortableText));
    captureShortcut_->setObjectName(QStringLiteral("capture/shortcut"));
    captureShortcut_->setMaximumSequenceLength(1);
    capture->addRow(IX_TEXT("Tastenkombination:"), captureShortcut_);
#ifdef Q_OS_WIN
    check(capture, QStringLiteral("capture/globalShortcut"),
          IX_TEXT("Tastenkürzel auch bei anderen aktiven Anwendungen verwenden"), false);
#endif
    number(capture, QStringLiteral("capture/delayMs"), IX_TEXT("Verzögerung vor Aufnahme (ms):"), 350, 0,
           10000);
    check(capture, QStringLiteral("capture/currentScreen"),
          IX_TEXT("Bildschirm unter dem Mauszeiger aufnehmen"), true);
    auto *performance = page(IX_TEXT("Leistung"));
    auto *imageCache = number(performance, QStringLiteral("performance/imageCacheMiB"),
                              IX_TEXT("Cache für geladene Bilder:"), 192, 0, 512);
    imageCache->setSuffix(QStringLiteral(" MiB"));
    imageCache->setSpecialValueText(IX_TEXT("Aus"));
    auto *previewCache = number(performance, QStringLiteral("performance/previewCacheMiB"),
                                IX_TEXT("Cache für Miniaturen:"), 16, 0, 64);
    previewCache->setSuffix(QStringLiteral(" MiB"));
    previewCache->setSpecialValueText(IX_TEXT("Aus"));
    check(performance, QStringLiteral("performance/rawPreview"),
          IX_TEXT("Eingebettete RAW-Vorschauen verwenden (auch für Miniaturen)"), true);
    number(performance, QStringLiteral("batch/parallelism"),
           IX_TEXT("Parallele Dateien beim Start einer neuen Batchkette:"), 2, 1, 4);
    auto *clear = new QPushButton(IX_TEXT("Bild- und Miniaturcaches jetzt leeren"));
    clear->setObjectName(QStringLiteral("clearImageCaches"));
    performance->addRow(clear);
    auto *cleared = new QLabel;
    cleared->setWordWrap(true);
    cleared->hide();
    performance->addRow(cleared);
    connect(clear, &QPushButton::clicked, this, [cleared] {
        ImageLoadCache::clear();
        cleared->setText(IX_TEXT("Caches geleert."));
        cleared->show();
    });
    auto *crop = page(IX_TEXT("Freistellen"));
    crop->parentWidget()->setObjectName(QStringLiteral("cropSettingsPage"));
    cropRatios_ = new CropRatioEditor(settings.value(QStringLiteral("crop/customRatios")).toStringList());
    crop->addRow(IX_TEXT("Eigene Seitenverhältnisse:"), cropRatios_);
    auto *storage = page(IX_TEXT("Speicherorte"));
    portable_ = new QCheckBox(IX_TEXT("Portabelmodus verwenden (nach Neustart)"));
    portable_->setObjectName(QStringLiteral("storage/portable"));
    portable_->setChecked(AppStorage::portableRequested(storageHome_));
    storage->addRow(portable_);
    storage->addRow(IX_TEXT("Aktiver Speichermodus:"), new QLabel(AppStorage::isPortable() ? IX_TEXT("Portabelmodus") : IX_TEXT("Benutzerprofil")));
    auto *storageRow = new QWidget;
    auto *storageLayout = new QHBoxLayout(storageRow);
    storageLayout->setContentsMargins(0, 0, 0, 0);
    auto *storagePath = new QLineEdit(QDir::toNativeSeparators(AppStorage::dataRoot()));
    storagePath->setObjectName(QStringLiteral("storage/dataRoot"));
    storagePath->setReadOnly(true);
    storagePath->setCursorPosition(0);
    storagePath->setAccessibleName(IX_TEXT("Datenordner"));
    storageLayout->addWidget(storagePath, 1);

    auto *openStorage = new QPushButton(IX_TEXT("Öffnen"));
    openStorage->setObjectName(QStringLiteral("openStorageFolder"));
    storageLayout->addWidget(openStorage);
    storage->addRow(IX_TEXT("Datenordner:"), storageRow);
    connect(openStorage, &QPushButton::clicked, this, [] {
        QDir().mkpath(AppStorage::dataRoot());
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppStorage::dataRoot()));
    });
    categories_->setFixedWidth(std::max(130, categories_->sizeHintForColumn(0) + 24));
    categories_->setCurrentRow(0);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Cancel)->setText(IX_TEXT("Abbrechen"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
void SettingsDialog::selectPage(const QString &objectName) {
    if (auto *page = findChild<QWidget *>(objectName))
        for (int i = 0; i < pages_->count(); ++i)
            if (pages_->widget(i) == page || pages_->widget(i)->isAncestorOf(page)) {
                categories_->setCurrentRow(i);
                return;
            }
}
void SettingsDialog::accept() {
    const auto cropRatios = cropRatios_->ratios();
    const auto shortcut = captureShortcut_->keySequence();
    if (!shortcut.isEmpty() && parentWidget()) {
        for (auto *action : parentWidget()->findChildren<QAction *>()) {
            if (action->data().toString() == QStringLiteral("capture"))
                continue;
            for (const auto &existing : action->shortcuts())
                if (shortcut.matches(existing) != QKeySequence::NoMatch ||
                    existing.matches(shortcut) != QKeySequence::NoMatch) {
                    QMessageBox::warning(
                        this, IX_TEXT("Tastenkombination"),
                        IX_TEXT("Dieses Tastenkürzel wird bereits verwendet: %1").arg(action->text()));
                    return;
                }
        }
    }
    QSettings settings;
    QVariantMap values;
    for (const auto &key : settings.allKeys())
        values.insert(key, settings.value(key));
    fullscreenInfo_->store(values);
    values.insert(QStringLiteral("crop/customRatios"), cropRatios);
    for (auto it = checks_.begin(); it != checks_.end(); ++it)
        values.insert(it.key(), it.value()->isChecked());
    for (auto it = integers_.begin(); it != integers_.end(); ++it)
        values.insert(it.key(), it.value()->value());
    values.insert(QStringLiteral("view/fit"), fit_->currentData());
    values.insert(QStringLiteral("navigation/sort"), sort_->currentIndex());
    values.insert(QStringLiteral("toolbar/iconSize"), iconSize_->currentData());
    values.insert(QStringLiteral("preview/background"), previewBackground_->currentData());
    values.insert(QStringLiteral("slideshow/seconds"), seconds_->value());
    values.insert(QStringLiteral("view/background"), background_);
    values.insert(QStringLiteral("fullscreen/background"), fullscreenBackground_);
    values.insert(QStringLiteral("ui/language"), language_->currentData());
    values.insert(QStringLiteral("capture/shortcut"), shortcut.toString(QKeySequence::PortableText));
    const bool switchMode = portable_->isChecked() != AppStorage::portableRequested(storageHome_);
    if (switchMode) {
        if (settings.status() != QSettings::NoError || !settings.isWritable()) {
            QMessageBox::warning(this, IX_TEXT("Speicherorte"), IX_TEXT("Einstellungen konnten nicht gespeichert werden."));
            return;
        }
        QString error;
        if (portable_->isChecked()) {
            const auto home = storageHome_, source = AppStorage::dataRoot();
            std::atomic_bool cancel = false;
            QProgressDialog progress(IX_TEXT("Portables Profil wird kopiert …"), IX_TEXT("Abbrechen"), 0, 0, this);
            progress.setWindowModality(Qt::WindowModal);
            progress.setMinimumDuration(0);
            connect(&progress, &QProgressDialog::canceled, this, [&cancel] { cancel.store(true); });
            QFutureWatcher<QString> watcher;
            connect(&watcher, &QFutureWatcherBase::finished, &progress, &QDialog::accept);
            watcher.setFuture(QtConcurrent::run([home, source, values, &cancel] {
                return AppStorage::preparePortable(home, values, source, cancel);
            }));
            progress.exec();
            // Cancel closes QProgressDialog immediately; keep the event loop
            // responsive until the worker has discarded its staging directory.
            if (!watcher.isFinished()) {
                QEventLoop loop;
                connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
                if (!watcher.isFinished())
                    loop.exec();
            }
            error = watcher.result();
        } else
            error = AppStorage::disablePortable(storageHome_);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, IX_TEXT("Speicherorte"), error);
            return;
        }
    }
    for (auto it = values.begin(); it != values.end(); ++it)
        settings.setValue(it.key(), it.value());
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        QMessageBox::warning(this, IX_TEXT("Speicherorte"), IX_TEXT("Einstellungen konnten nicht gespeichert werden."));
        return;
    }
    if (switchMode)
        QMessageBox::information(this, IX_TEXT("Speicherorte"), IX_TEXT("Der Speichermodus wird nach einem Neustart wirksam."));
    QDialog::accept();
}
} // namespace ix
