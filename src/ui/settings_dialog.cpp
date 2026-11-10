// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_dialog.h"
#include "option_controls.h"
#include "dialog_layout.h"
#include "crop_ratio_editor.h"
#include "core/localization.h"
#include "core/app_storage.h"
#include "core/preview_service.h"
#include "core/codec_registry.h"
#include "core/save_formats.h"
#include "worker_wait.h"
#include "image_view.h"
#include "main_window.h"
#include "fullscreen_info.h"
#include "file_navigation.h"
#include "folder_traversal.h"
#include <QtWidgets>
#include <QtConcurrent>
namespace ix {
SettingsDialog::SettingsDialog(QWidget *parent, QString storageHome) : QDialog(parent) {
    storageHome_ = storageHome.isEmpty() ? AppStorage::applicationHome() : storageHome;
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(IX_TEXT("Einstellungen"));
    auto readableFont = font();
    if (readableFont.pointSizeF() > 0 && readableFont.pointSizeF() < 10) {
        readableFont.setPointSizeF(10);
        setFont(readableFont);
    }
    setStyleSheet(QStringLiteral(
        "QCheckBox::indicator { width: 18px; height: 18px; }"
        "QSpinBox::up-button, QSpinBox::down-button, "
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 22px; }"
        "QComboBox::drop-down { width: 24px; }"
        "QComboBox::down-arrow { width: 8px; height: 5px; }"));
    const auto available = screen()->availableGeometry().size();
    resize(QSize(760, 460).boundedTo(available - QSize(32, 48)));
    QSettings settings;
    auto *layout = dialogLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    auto *body = new QHBoxLayout;
    body->setSpacing(12);
    categories_ = new QListWidget;
    categories_->setObjectName(QStringLiteral("settingsCategories"));
    categories_->setAccessibleName(IX_TEXT("Einstellungsbereiche"));
    categories_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categories_->setFrameShape(QFrame::NoFrame);
    categories_->setSpacing(1);
    categories_->setStyleSheet(QStringLiteral(
        "QListWidget { background: palette(alternate-base); border: 0; border-radius: 6px; padding: 4px; }"
        "QListWidget::item { border-radius: 4px; padding-left: 8px; padding-right: 8px; }"
        "QListWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); }"));
    pages_ = new QStackedWidget;
    pages_->setObjectName(QStringLiteral("settingsPages"));
    body->addWidget(categories_);
    body->addWidget(pages_, 1);
    layout->addLayout(body, 1);
    connect(categories_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    auto page = [this](const QString &label) {
        auto *widget = new QWidget;
        auto *outer = new QVBoxLayout(widget);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(spacing::section);
        auto *form = dialogFormLayout();
        form->setVerticalSpacing(4);
        form->setHorizontalSpacing(12);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        outer->addLayout(form);
        outer->addStretch();
        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setWidget(widget);
        pages_->addWidget(scroll);
        auto *item = new QListWidgetItem(label);
        item->setSizeHint({categories_->fontMetrics().horizontalAdvance(label) + 16,
                           optionControlHeight(categories_->fontMetrics())});
        categories_->addItem(item);
        return form;
    };
    auto check = [&](QFormLayout *form, const QString &key, const QString &label, bool fallback) {
        auto *box = new OptionCheckBox(label);
        box->setAccessibleName(label);
        box->setObjectName(key);
        box->setChecked(settings.value(key, fallback).toBool());
        checks_.insert(key, box);
        form->addRow(box);
        return box;
    };
    auto number = [&](QFormLayout *form, const QString &key, const QString &label, int fallback, int minimum,
                      int maximum) {
        auto *box = new QSpinBox;
        box->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        box->setObjectName(key);
        box->setRange(minimum, maximum);
        box->setValue(settings.value(key, fallback).toInt());
        integers_.insert(key, box);
        form->addRow(label, box);
        return box;
    };
    auto dependentField = [](QCheckBox *toggle, QFormLayout *form, QWidget *field) {
        auto *label = form->labelForField(field);
        auto update = [field, label](bool enabled) {
            field->setEnabled(enabled);
            if (label) label->setEnabled(enabled);
        };
        update(toggle->isChecked());
        QObject::connect(toggle, &QCheckBox::toggled, field, update);
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
    background->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
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
    theme_ = new QComboBox;
    theme_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    theme_->setObjectName(QStringLiteral("ui/theme"));
    theme_->addItem(IX_TEXT("Systemdesign"), QStringLiteral("system"));
    theme_->addItem(IX_TEXT("Hell"), QStringLiteral("light"));
    theme_->addItem(IX_TEXT("Dunkel"), QStringLiteral("dark"));
    theme_->setCurrentIndex(std::max(0, theme_->findData(
        settings.value(QStringLiteral("ui/theme"), QStringLiteral("system")))));
    surface->addRow(IX_TEXT("Farbschema:"), theme_);
    language_ = new QComboBox;
    language_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    language_->setObjectName(QStringLiteral("ui/language"));
    for (const auto &entry :
         QList<QPair<QString, QString>>{{QStringLiteral("Deutsch"), QStringLiteral("de")},
                                        {QStringLiteral("English"), QStringLiteral("en")},
                                        {QStringLiteral("한국어"), QStringLiteral("ko")},
                                        {QStringLiteral("日本語"), QStringLiteral("ja")},
                                        {QStringLiteral("Français"), QStringLiteral("fr")}})
        language_->addItem(entry.first, entry.second);
    language_->setCurrentIndex(std::max(0, language_->findData(uiLanguage())));
    surface->addRow(IX_TEXT("Sprache:"), language_);
    check(surface, QStringLiteral("window/toolbar"), IX_TEXT("Symbolleiste anzeigen"), true);
    check(surface, QStringLiteral("window/statusbar"), IX_TEXT("Statusleiste anzeigen"), true);
    check(surface, QStringLiteral("window/menubar"), IX_TEXT("Menüleiste anzeigen"), true);
    check(surface, QStringLiteral("view/actionSearch"),
          IX_TEXT("Befehlssuche anzeigen"), false);
    iconSize_ = new QComboBox;
    iconSize_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
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
    // Dedicated page keeps the display controls roomy without a nested frame.
    auto *previews = page(IX_TEXT("Vorschauen"));
    previews->parentWidget()->setObjectName(QStringLiteral("previewSettingsPage"));
    auto *enabled = check(previews, QStringLiteral("view/previewStrip"),
                          IX_TEXT("Nachbarbilder unter dem aktuellen Bild anzeigen"), false);
    auto *previous =
        number(previews, QStringLiteral("preview/previous"), IX_TEXT("Vorherige Bilder:"), 2, 1, 5);
    auto *next = number(previews, QStringLiteral("preview/next"), IX_TEXT("Kommende Bilder:"), 2, 1, 5);
    dependentField(enabled, previews, previous);
    dependentField(enabled, previews, next);
    previewBackground_ = new QComboBox;
    previewBackground_->setObjectName(QStringLiteral("preview/background"));
    previewBackground_->addItem(IX_TEXT("Wie Oberfläche"), QStringLiteral("theme"));
    previewBackground_->addItem(IX_TEXT("Wie Bildhintergrund"), QStringLiteral("image"));
    previewBackground_->addItem(IX_TEXT("Transparent"), QStringLiteral("transparent"));
    previewBackground_->setCurrentIndex(std::max(0, previewBackground_->findData(
        settings.value(QStringLiteral("preview/background"), QStringLiteral("theme")))));
    previews->addRow(IX_TEXT("Hintergrund der Vorschaubilder:"), previewBackground_);
    dependentField(enabled, previews, previewBackground_);
    auto *navigation = page(IX_TEXT("Navigation"));
    sort_ = new QComboBox;
    sort_->setObjectName(QStringLiteral("navigation/sort"));
    sort_->addItems({IX_TEXT("Name (natürlich: 2 vor 10)"), IX_TEXT("Name (alphabetisch)"),
                     IX_TEXT("Änderungsdatum"), IX_TEXT("Dateigröße")});
    sort_->setCurrentIndex(std::clamp(settings.value(QStringLiteral("navigation/sort"), 0).toInt(), 0, 3));
    navigation->addRow(IX_TEXT("Dateien sortieren nach:"), sort_);
    unreadableFiles_ = new QComboBox;
    unreadableFiles_->setObjectName(QStringLiteral("navigation/unreadableFiles"));
    unreadableFiles_->addItem(IX_TEXT("Überspringen"), int(UnreadableFiles::Skip));
    unreadableFiles_->addItem(IX_TEXT("Meldung mit Dateiname"), int(UnreadableFiles::Message));
    unreadableFiles_->addItem(IX_TEXT("Schwarze Anzeige"), int(UnreadableFiles::Black));
    unreadableFiles_->setCurrentIndex(int(unreadableFilesMode(
        settings.value(QStringLiteral("navigation/unreadableFiles"), 0).toInt())));
    navigation->addRow(IX_TEXT("Unlesbare Dateien:"), unreadableFiles_);
    afterDelete_ = new QComboBox;
    afterDelete_->setObjectName(QStringLiteral("navigation/afterDelete"));
    afterDelete_->addItem(IX_TEXT("Schwarze Anzeige"), int(AfterDelete::StayEmpty));
    afterDelete_->addItem(IX_TEXT("Nächstes Bild öffnen"), int(AfterDelete::OpenNext));
    // The items are intentionally not in enum order, so look the row up by its
    // stored value instead of mistaking the value for the row index.
    afterDelete_->setCurrentIndex(qMax(0, afterDelete_->findData(int(afterDeleteMode(
        settings.value(QStringLiteral("navigation/afterDelete"), int(AfterDelete::StayEmpty)).toInt())))));
    navigation->addRow(IX_TEXT("Nach dem Löschen:"), afterDelete_);
    check(navigation, QStringLiteral("navigation/reverse"), IX_TEXT("Absteigend sortieren"), false);
    check(navigation, QStringLiteral("navigation/wrap"),
          IX_TEXT("Nach dem letzten Bild wieder beim ersten beginnen"), true);
    check(navigation, QStringLiteral("navigation/askToSave"),
          IX_TEXT("Beim Bildwechsel und Schließen nach ungespeicherten Änderungen fragen"), false);
    check(navigation, QStringLiteral("view/lockZoom"), IX_TEXT("Zoomfaktor beim Bildwechsel beibehalten"),
          false);
    check(navigation, QStringLiteral("view/lockScroll"),
          IX_TEXT("Scrollposition beim Bildwechsel beibehalten"), false);
    auto *controls = page(IX_TEXT("Steuerung"));
    controls->parentWidget()->setObjectName(QStringLiteral("controlsSettingsPage"));
    auto *mouseLabel = new QLabel(IX_TEXT("Maus"));
    mouseLabel->setObjectName(QStringLiteral("controlsMouseLabel"));
    QFont mouseFont = mouseLabel->font();
    mouseFont.setBold(true);
    mouseLabel->setFont(mouseFont);
    controls->addRow(mouseLabel);
    doubleClick_ = new QComboBox;
    doubleClick_->setObjectName(QStringLiteral("controls/doubleClick"));
    doubleClick_->addItem(IX_TEXT("Vollbild umschalten"), int(DoubleClickAction::Fullscreen));
    doubleClick_->addItem(IX_TEXT("Vergrößern/Verkleinern"), int(DoubleClickAction::Zoom));
    doubleClick_->setCurrentIndex(
        qMax(0, doubleClick_->findData(int(doubleClickMode(
                     settings.value(QStringLiteral("controls/doubleClick"),
                                    int(DoubleClickAction::Fullscreen))
                         .toInt())))));
    controls->addRow(IX_TEXT("Doppelklick-Aktion:"), doubleClick_);
    auto *keyboardLabel = new QLabel(IX_TEXT("Tastatur"));
    keyboardLabel->setObjectName(QStringLiteral("controlsKeyboardLabel"));
    QFont keyboardFont = keyboardLabel->font();
    keyboardFont.setBold(true);
    keyboardLabel->setFont(keyboardFont);
    controls->addRow(keyboardLabel);
    arrowKeys_ = new QComboBox;
    arrowKeys_->setObjectName(QStringLiteral("controls/arrowKeys"));
    arrowKeys_->addItem(IX_TEXT("Links/Rechts"), int(ArrowNavigation::LeftRight));
    arrowKeys_->addItem(IX_TEXT("Hoch/Runter"), int(ArrowNavigation::UpDown));
    arrowKeys_->addItem(IX_TEXT("Links/Rechts und Hoch/Runter"), int(ArrowNavigation::Both));
    arrowKeys_->setCurrentIndex(qMax(0, arrowKeys_->findData(int(arrowNavigationMode(
                                          settings.value(QStringLiteral("controls/arrowKeys"),
                                                         int(ArrowNavigation::LeftRight))
                                              .toInt())))));
    controls->addRow(IX_TEXT("Bildnavigation mit Pfeiltasten:"), arrowKeys_);
    auto *fullscreen = page(IX_TEXT("Vollbild"));
    fullscreenFit_ = new QComboBox;
    fullscreenFit_->setObjectName(QStringLiteral("fullscreen/fit"));
    const QStringList fullscreenFitLabels{IX_TEXT("Originalgröße (100 %)"), IX_TEXT("Alle Bilder an Bildschirm anpassen"),
        IX_TEXT("Nur große Bilder verkleinern"), IX_TEXT("An Bildschirmbreite anpassen"),
        IX_TEXT("An Bildschirmhöhe anpassen"), IX_TEXT("Kurze Bildseite an Bildschirm anpassen")};
    for (int i = 0; i < fullscreenFitLabels.size(); ++i) fullscreenFit_->addItem(fullscreenFitLabels[i], i);
    fullscreenFit_->setCurrentIndex(int(ImageView::fullscreenFit()));
    fullscreen->addRow(IX_TEXT("Bildanpassung:"), fullscreenFit_);
    fullscreenScreen_ = new QComboBox;
    fullscreenScreen_->setObjectName(QStringLiteral("fullscreen/screen"));
    fullscreenScreen_->addItem(IX_TEXT("Aktueller Bildschirm"), QString());
    int screenNumber = 0;
    for (auto *screen : QGuiApplication::screens()) {
        const auto size = screen->geometry().size();
        fullscreenScreen_->addItem(QStringLiteral("%1: %2 (%3 × %4)").arg(++screenNumber)
            .arg(screen->name()).arg(size.width()).arg(size.height()), screen->name());
    }
    fullscreenScreen_->setCurrentIndex(std::max(0, fullscreenScreen_->findData(
        settings.value(QStringLiteral("fullscreen/screen"), QString()))));
    fullscreen->addRow(IX_TEXT("Bildschirm:"), fullscreenScreen_);
    check(fullscreen, QStringLiteral("fullscreen/hideCursor"), IX_TEXT("Mauszeiger ausblenden"), true);
    fullscreenBackground_ =
        settings.value(QStringLiteral("fullscreen/background"), QColor(Qt::black)).value<QColor>();
    auto *fullColor = new QPushButton;
    fullColor->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
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
    seconds_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
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
    captureShortcut_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    captureShortcut_->setMaximumWidth(220);
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
    auto *memory = number(performance, QStringLiteral("performance/memoryLimitMiB"),
                          IX_TEXT("Bildpuffer-Budget (Neustart):"), 0, 0, 1048576);
    memory->setSuffix(QStringLiteral(" MiB"));
    memory->setSpecialValueText(IX_TEXT("Automatisch (%1 MiB)")
        .arg(calculateImageMemoryLimit(physicalMemoryBytes()) / MiB));
    auto *swapEnabled = check(performance, QStringLiteral("performance/swapEnabled"), IX_TEXT("Große Bildpuffer auf Datenträger auslagern"), true);
    auto *swap = number(performance, QStringLiteral("performance/swapLimitMiB"), IX_TEXT("Auslagerungsbudget:"), 65536, 0, 1048576);
    swap->setSuffix(QStringLiteral(" MiB"));
    dependentField(swapEnabled, performance, swap);
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
    clear->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
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
    auto *storage = page(IX_TEXT("Speichern"));
    storage->parentWidget()->setObjectName(QStringLiteral("saveSettingsPage"));
    savePreference_ = new QComboBox;
    savePreference_->setObjectName(QStringLiteral("save/preferOriginal"));
    savePreference_->addItem(IX_TEXT("Ursprüngliches Dateiformat bevorzugen"), true);
    savePreference_->addItem(IX_TEXT("Festgelegtes Dateiformat bevorzugen"), false);
    savePreference_->setCurrentIndex(settings.value(QStringLiteral("save/preferOriginal"), true).toBool() ? 0 : 1);
    storage->addRow(IX_TEXT("Standardformat:"), savePreference_);
    saveFormat_ = new QComboBox;
    saveFormat_->setObjectName(QStringLiteral("save/format"));
    const auto storedFormat = canonicalSaveFormat(settings.value(QStringLiteral("save/format"), QStringLiteral("png")).toByteArray());
    const auto loadSaveFormats = [this, storedFormat] {
        if (saveFormatsLoaded_) return;
        saveFormatsLoaded_ = true;
        for (const auto &format : writableSaveFormats(CodecRegistry()))
            saveFormat_->addItem(format.label, QString::fromLatin1(format.id));
        saveFormat_->setCurrentIndex(saveFormat_->findData(QString::fromLatin1(storedFormat)));
    };
    const int savePageIndex = pages_->count() - 1;
    connect(categories_, &QListWidget::currentRowChanged, this, [loadSaveFormats, savePageIndex](int index) {
        if (index == savePageIndex) loadSaveFormats();
    });
    saveFormat_->setPlaceholderText(IX_TEXT("Format nicht verfügbar"));
    storage->addRow(IX_TEXT("Format:"), saveFormat_);
    auto updateSaveFormat = [this, storage] {
        const bool enabled = !savePreference_->currentData().toBool();
        saveFormat_->setEnabled(enabled);
        storage->labelForField(saveFormat_)->setEnabled(enabled);
    };
    connect(savePreference_, &QComboBox::currentIndexChanged, this, [loadSaveFormats, updateSaveFormat] {
        loadSaveFormats(); updateSaveFormat();
    });
    updateSaveFormat();
    auto *storageDivider = new QFrame;
    storageDivider->setFrameShape(QFrame::HLine);
    storage->addRow(storageDivider);
    portable_ = new OptionCheckBox(IX_TEXT("Portabelmodus verwenden (nach Neustart)"));
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
    const int controlHeight = optionControlHeight(fontMetrics());
    for (auto *combo : findChildren<QComboBox *>()) {
        combo->setFocusPolicy(Qt::StrongFocus);
        combo->setMinimumHeight(controlHeight);
        combo->setMinimumWidth(160);
        combo->setMaximumWidth(340);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        combo->setItemDelegate(new OptionItemDelegate(combo));
        combo->installEventFilter(this);
    }
    for (auto *spin : findChildren<QAbstractSpinBox *>()) {
        spin->setFocusPolicy(Qt::StrongFocus);
        spin->setMinimumHeight(controlHeight);
        spin->setMinimumWidth(96);
        spin->setMaximumWidth(200);
        spin->setAccelerated(true);
        spin->installEventFilter(this);
    }
    captureShortcut_->setMinimumHeight(controlHeight);
    for (auto *edit : findChildren<QLineEdit *>())
        edit->setMinimumHeight(edit->isReadOnly() ? controlHeight : controlHeight - 4);
    for (auto *button : findChildren<QPushButton *>()) button->setMinimumHeight(controlHeight);
    categories_->setFixedWidth(std::max(140, categories_->sizeHintForColumn(0) + 24));
    categories_->setCurrentRow(0);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Cancel)->setText(IX_TEXT("Abbrechen"));
    for (auto *button : buttons->buttons()) {
        button->setMinimumHeight(controlHeight);
        button->setMinimumWidth(96);
    }
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
bool SettingsDialog::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::Wheel) {
        auto *field = qobject_cast<QWidget *>(object);
        if (field && !field->hasFocus()) {
            event->ignore();
            return true;
        }
    }
    return QDialog::eventFilter(object, event);
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
    if (saveFormatsLoaded_ && !savePreference_->currentData().toBool() && saveFormat_->currentIndex() < 0) {
        QMessageBox::warning(this, IX_TEXT("Speichern"), IX_TEXT("Bitte ein verfügbares Ausgabeformat wählen."));
        selectPage(QStringLiteral("saveSettingsPage"));
        return;
    }
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
    values.insert(QStringLiteral("fullscreen/fit"), fullscreenFit_->currentData());
    values.insert(QStringLiteral("fullscreen/screen"), fullscreenScreen_->currentData());
    values.insert(QStringLiteral("navigation/sort"), sort_->currentIndex());
    values.insert(QStringLiteral("navigation/unreadableFiles"), unreadableFiles_->currentData());
    values.insert(QStringLiteral("navigation/afterDelete"), afterDelete_->currentData());
    values.insert(QStringLiteral("controls/doubleClick"), doubleClick_->currentData());
    values.insert(QStringLiteral("controls/arrowKeys"), arrowKeys_->currentData());
    values.insert(QStringLiteral("toolbar/iconSize"), iconSize_->currentData());
    values.insert(QStringLiteral("preview/background"), previewBackground_->currentData());
    values.insert(QStringLiteral("slideshow/seconds"), seconds_->value());
    values.insert(QStringLiteral("view/background"), background_);
    values.insert(QStringLiteral("fullscreen/background"), fullscreenBackground_);
    values.insert(QStringLiteral("ui/language"), language_->currentData());
    values.insert(QStringLiteral("ui/theme"), theme_->currentData());
    values.insert(QStringLiteral("save/preferOriginal"), savePreference_->currentData());
    if (saveFormat_->currentIndex() >= 0) values.insert(QStringLiteral("save/format"), saveFormat_->currentData());
    values.insert(QStringLiteral("capture/shortcut"), shortcut.toString(QKeySequence::PortableText));
    const bool switchMode = portable_->isChecked() != AppStorage::portableRequested(storageHome_);
    if (switchMode) {
        if (settings.status() != QSettings::NoError || !settings.isWritable()) {
            QMessageBox::warning(this, IX_TEXT("Speichern"), IX_TEXT("Einstellungen konnten nicht gespeichert werden."));
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
            waitForGuiWorker(watcher, progress, true);
            error = watcher.result();
        } else
            error = AppStorage::disablePortable(storageHome_);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, IX_TEXT("Speichern"), error);
            return;
        }
    }
    for (auto it = values.begin(); it != values.end(); ++it)
        settings.setValue(it.key(), it.value());
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        QMessageBox::warning(this, IX_TEXT("Speichern"), IX_TEXT("Einstellungen konnten nicht gespeichert werden."));
        return;
    }
    if (switchMode)
        QMessageBox::information(this, IX_TEXT("Speichern"), IX_TEXT("Der Speichermodus wird nach einem Neustart wirksam."));
    QDialog::accept();
    if (auto *owner = qobject_cast<MainWindow *>(parentWidget()))
        owner->applyControlSettings();
}
} // namespace ix
