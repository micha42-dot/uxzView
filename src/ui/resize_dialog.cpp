// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "resize_dialog.h"
#include "resize_filters.h"
#include "core/localization.h"
#include "platform_support.h"
#include <QtWidgets>
namespace ix {
ResizeDialog::ResizeDialog(QSize original, double dpi, QWidget *parent)
    : QDialog(parent), model_(original, dpi) {
    setWindowTitle(IX_TEXT("Größe ändern"));
    setFont(referenceFont(8));
    setObjectName(QStringLiteral("resizeDialog"));
    setFixedSize(552, 400);
    auto label = [&](QString text, int x, int y, int w = 120) {
        auto *v = new QLabel(text, this);
        v->setGeometry(x, y, w, 20);
        return v;
    };
    auto spin = [&](QString name, int x, int y, int w = 52, int decimals = 0) {
        auto *v = new QDoubleSpinBox(this);
        v->setObjectName(name);
        v->setGeometry(x, y, w, 22);
        v->setDecimals(decimals);
        // Commit complete keyboard input before sync() reformats linked fields.
        v->setKeyboardTracking(false);
        v->setRange(decimals ? 0.01 : 1, 100000);
        v->setButtonSymbols(QAbstractSpinBox::NoButtons);
        return v;
    };
    auto group = [&](QString text, int x, int y, int w, int h) {
        auto *v = new QGroupBox(text, this);
        v->setGeometry(x, y, w, h);
        return v;
    };
    group({}, 7, 8, 238, 64);
    label(IX_TEXT("Aktuelle Größe:  %1 x %2 Pixel").arg(original.width()).arg(original.height()), 16, 16,
          225);
    result_ = label({}, 16, 42, 225);
    absoluteGroup_ = group({}, 7, 75, 238, 72);
    absolute_ = new QRadioButton(IX_TEXT("Neue Größe:"), this);
    absolute_->setGeometry(15, 77, 130, 20);
    absolute_->setChecked(true);
    label(IX_TEXT("Breite:"), 18, 102, 40);
    width_ = spin(QStringLiteral("width"), 57, 101);
    label(IX_TEXT("Höhe:"), 128, 102, 40);
    height_ = spin(QStringLiteral("height"), 170, 101);
    label(IX_TEXT("Einheit:"), 18, 126, 42);
    auto *units = new QButtonGroup(this);
    units->setObjectName(QStringLiteral("resizeUnits"));
    QStringList unitNames{IX_TEXT("Pixel"), QStringLiteral("cm"), QStringLiteral("inch")};
    for (int i = 0; i < 3; ++i) {
        auto *r = new QRadioButton(unitNames[i], this);
        r->setGeometry(66 + i * 52, 124, 53, 20);
        units->addButton(r, i);
        if (i == 0)
            r->setChecked(true);
    }
    connect(units, &QButtonGroup::idClicked, this, [=, this](int i) {
        model_.setUnit(ResizeModel::Unit(i));
        sync();
        updateMode();
    });
    percentGroup_ = group({}, 7, 150, 238, 48);
    percent_ = new QRadioButton(IX_TEXT("Prozentsatz vom Original:"), this);
    percent_->setGeometry(15, 151, 220, 20);
    label(IX_TEXT("Breite:"), 18, 174, 40);
    percentW_ = spin(QStringLiteral("percentWidth"), 57, 173, 50, 2);
    label(IX_TEXT("%    Höhe:"), 108, 174, 61);
    percentH_ = spin(QStringLiteral("percentHeight"), 170, 173, 50, 2);
    label(QStringLiteral("%"), 222, 174, 20);
    mpGroup_ = group({}, 7, 201, 238, 45);
    megapixel_ = new QRadioButton(IX_TEXT("Größe in MegaPixel setzen:"), this);
    megapixel_->setGeometry(15, 201, 225, 20);
    mp_ = spin(QStringLiteral("megapixels"), 57, 223, 54, 2);
    label(IX_TEXT("MegaPixel"), 115, 222);
    auto *modes = new QButtonGroup(this);
    modes->addButton(absolute_);
    modes->addButton(percent_);
    modes->addButton(megapixel_);
    connect(modes, &QButtonGroup::buttonClicked, this, [this] {
        if (megapixel_->isChecked()) {
            model_.setMegapixels(mp_->value());
            sync();
        }
        updateMode();
    });
    const auto useAbsoluteSize = [this] {
        absolute_->setChecked(true);
        updateMode();
        sync();
    };
    group({}, 7, 253, 238, 102);
    proportional_ = new QCheckBox(IX_TEXT("Proportional"), this);
    proportional_->setObjectName(QStringLiteral("proportional"));
    proportional_->setGeometry(14, 260, 210, 20);
    proportional_->setChecked(true);
    sharpen_ = new QCheckBox(IX_TEXT("Schärfen nach Resample anwenden"), this);
    sharpen_->setGeometry(14, 282, 226, 20);
    adaptDpi_ = new QCheckBox(IX_TEXT("DPI auf neue Größe anpassen (proport.)"), this);
    adaptDpi_->setGeometry(14, 304, 230, 20);
    label(QStringLiteral("DPI:"), 14, 330, 40);
    dpi_ = spin(QStringLiteral("dpi"), 57, 329, 50);
    dpi_->setValue(dpi);
    label(IX_TEXT("(Automatik für cm/inch)"), 111, 330, 135);
    group(IX_TEXT("Einige Standard-Dimensionen: (Pixel):"), 252, 3, 293, 228);
    custom_ = new QComboBox(this);
    custom_->setGeometry(260, 25, 100, 23);
    custom_->addItem(IX_TEXT("Neue Größe:"));
    QSettings settings;
    custom_->addItems(settings.value(QStringLiteral("resize/presets")).toStringList());
    label(IX_TEXT("(verwendet 'Proportional')"), 365, 26, 132);
    const QList<QSize> sizes{{640, 480},   {800, 600},   {1024, 768}, {1280, 720},
                             {1920, 1080}, {3840, 2160}, {7680, 4320}};
    const QStringList names{QStringLiteral("640 x 480"),         QStringLiteral("800 x 600"),
                            QStringLiteral("1024 x 768"),        QStringLiteral("1280 x 720 (HD)"),
                            QStringLiteral("1920 x 1080 (FHD)"), QStringLiteral("3840 x 2160 (4K)"),
                            QStringLiteral("7680 x 4320 (8K)")};
    auto *presets = new QButtonGroup(this);
    for (int i = 0; i < sizes.size(); ++i) {
        int col = i < 4 ? 0 : 1, row = i < 4 ? i : i - 4;
        auto *r = new QRadioButton(names[i], this);
        r->setGeometry(260 + col * 119, 49 + row * 20, col ? 168 : 123, 20);
        presets->addButton(r, i);
    }
    connect(presets, &QButtonGroup::idClicked, this, [=, this](int i) {
        // Screen buttons share this group but have automatically assigned
        // negative IDs; they must not index the fixed-dimension table.
        if (i < 0 || i >= sizes.size())
            return;
        model_.preset(sizes[i], model_.proportional);
        useAbsoluteSize();
    });
    auto preset = [&](QString text, int y, bool keep) {
        auto *r = new QRadioButton(text, this);
        r->setGeometry(260, y, 230, 20);
        presets->addButton(r);
        connect(r, &QRadioButton::clicked, this, [=, this] {
            model_.preset(screen()->availableGeometry().size(), keep);
            useAbsoluteSize();
        });
    };
    preset(IX_TEXT("An Bildschirm anpassen"), 131, true);
    preset(IX_TEXT("Bildschirmgröße (nicht proportional)"), 151, false);
    auto button = [&](QString text, int x, int y, std::function<void()> f) {
        auto *b = new QPushButton(text, this);
        b->setGeometry(x, y, 106, 23);
        connect(b, &QPushButton::clicked, this, std::move(f));
    };
    button(IX_TEXT("Halb"), 260, 176, [this, useAbsoluteSize] {
        model_.scale(.5);
        useAbsoluteSize();
    });
    button(IX_TEXT("Doppelt"), 377, 176, [this, useAbsoluteSize] {
        model_.scale(2);
        useAbsoluteSize();
    });
    button(IX_TEXT("Seiten vertauschen"), 260, 204, [this, useAbsoluteSize] {
        model_.swapSides();
        useAbsoluteSize();
    });
    button(IX_TEXT("Zu Standards hinzuf."), 377, 204, [this] {
        QString size = QStringLiteral("%1 x %2").arg(model_.size().width()).arg(model_.size().height());
        if (custom_->findText(size) < 0)
            custom_->addItem(size);
    });
    connect(custom_, &QComboBox::activated, this, [this, useAbsoluteSize](int i) {
        if (i == 0)
            return;
        auto parts = custom_->currentText().split(QStringLiteral(" x "));
        if (parts.size() == 2) {
            model_.preset(QSize(parts[0].toInt(), parts[1].toInt()), model_.proportional);
            useAbsoluteSize();
        }
    });
    group(IX_TEXT("Methode:"), 252, 232, 293, 76);
    filter_ = new QComboBox(this);
    filter_->setObjectName(QStringLiteral("filter"));
    filter_->setGeometry(260, 254, 277, 25);
    filter_->addItems(resizeFilterLabels());
    filter_->setCurrentIndex(5);
    filter_->setItemData(int(ResampleFilter::Nearest),
        IX_TEXT("Ohne Glättung: Jeder Pixel bleibt unverändert. Für gleichmäßige Pixelblöcke 200 %, 300 % oder andere ganze Vielfache wählen."),
        Qt::ToolTipRole);
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] { updateMode(); });
    fast_ = new QCheckBox(IX_TEXT("Schnellen Filter fürs Verkleinern verwenden"), this);
    fast_->setGeometry(260, 320, 235, 20);
    fast_->setChecked(true);
    gamma_ = new QCheckBox(IX_TEXT("Gamma bei Resample verbessern (siehe Hilfe)"), this);
    gamma_->setGeometry(260, 341, 235, 20);
    auto *ok = new QPushButton(QStringLiteral("OK"), this);
    ok->setGeometry(133, 369, 102, 23);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, &ResizeDialog::accept);
    auto *cancel = new QPushButton(IX_TEXT("Abbrechen"), this);
    cancel->setGeometry(260, 369, 102, 23);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto bind = [this](QDoubleSpinBox *box, auto fn) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, fn](double value) {
            if (syncing_)
                return;
            (model_.*fn)(value);
            sync();
        });
    };
    bind(width_, &ResizeModel::setWidth);
    bind(height_, &ResizeModel::setHeight);
    bind(percentW_, &ResizeModel::setPercentWidth);
    bind(percentH_, &ResizeModel::setPercentHeight);
    bind(mp_, &ResizeModel::setMegapixels);
    bind(dpi_, &ResizeModel::setDpi);
    connect(proportional_, &QCheckBox::toggled, this, [this](bool v) { model_.proportional = v; });
    connect(adaptDpi_, &QCheckBox::toggled, this, [this, units](bool v) {
        // Physical dimensions already determine pixels through manual DPI.
        // Automatic DPI preserves print size and therefore uses pixel inputs.
        if (v) {
            model_.setUnit(ResizeModel::Unit::Pixel);
            units->button(0)->setChecked(true);
        }
        model_.adaptDpi = v;
        sync();
        updateMode();
    });
    sync();
    {
        QSignalBlocker block(mp_);
        mp_->setValue(4.0);
    }
    updateMode();
    width_->setFocus();
    width_->selectAll();
}
void ResizeDialog::sync() {
    syncing_ = true;
    int dec = model_.unit() == ResizeModel::Unit::Pixel ? 0 : 2;
    width_->setDecimals(dec);
    height_->setDecimals(dec);
    width_->setValue(model_.width());
    height_->setValue(model_.height());
    percentW_->setValue(model_.percentWidth());
    percentH_->setValue(model_.percentHeight());
    if (megapixel_->isChecked())
        mp_->setValue(double(model_.size().width()) * model_.size().height() / 1e6);
    dpi_->setValue(model_.dpi());
    result_->setText(
        IX_TEXT("Neue Größe:     %1 x %2 Pixel").arg(model_.size().width()).arg(model_.size().height()));
    syncing_ = false;
}
void ResizeDialog::updateMode() {
    width_->setEnabled(absolute_->isChecked());
    height_->setEnabled(absolute_->isChecked());
    percentW_->setEnabled(percent_->isChecked());
    percentH_->setEnabled(percent_->isChecked());
    mp_->setEnabled(megapixel_->isChecked());
    dpi_->setEnabled(!model_.adaptDpi);
    if (auto *units = findChild<QButtonGroup *>(QStringLiteral("resizeUnits")))
        for (int id : {1, 2}) units->button(id)->setEnabled(!model_.adaptDpi);
    const bool smooth = filter_->currentIndex() != int(ResampleFilter::Nearest);
    fast_->setEnabled(smooth);
    gamma_->setEnabled(smooth);
    sharpen_->setEnabled(smooth);
}
ResizeOptions ResizeDialog::options() const {
    ResizeOptions o;
    o.size = model_.size();
    o.dpi = model_.dpi();
    o.filter = ResampleFilter(filter_->currentIndex());
    const bool smooth = o.filter != ResampleFilter::Nearest;
    o.sharpen = sharpen_->isChecked() && smooth;
    o.fastDownscale = fast_->isChecked() && smooth;
    o.linearLight = gamma_->isChecked() && smooth;
    return o;
}
void ResizeDialog::accept() {
    if (!safeSize(model_.size())) {
        QMessageBox::warning(this, IX_TEXT("Größe ändern"),
                             IX_TEXT("Die gewünschte Größe überschreitet das Speicherlimit von 512 MiB."));
        return;
    }
    QStringList presets;
    for (int i = 1; i < custom_->count(); ++i)
        presets << custom_->itemText(i);
    QSettings().setValue(QStringLiteral("resize/presets"), presets);
    QDialog::accept();
}
} // namespace ix
