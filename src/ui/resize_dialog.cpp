// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "resize_dialog.h"
#include "dialog_layout.h"
#include "resize_filters.h"
#include "core/localization.h"
#include "platform_support.h"
#include <QtWidgets>
namespace ix {
ResizeDialog::ResizeDialog(QSize original, double dpi, QWidget *parent)
    : QDialog(parent), model_(original, dpi) {
    setWindowTitle(IX_TEXT("Größe ändern"));
    setObjectName(QStringLiteral("resizeDialog"));
    auto *outer = dialogLayout(this);
    auto *summary = new QHBoxLayout;
    summary->setSpacing(16);
    auto *current = new QLabel(IX_TEXT("Aktuelle Größe:  %1 x %2 Pixel").arg(original.width()).arg(original.height()));
    current->setWordWrap(true);
    result_ = new QLabel;
    result_->setWordWrap(true);
    summary->addWidget(current, 1);
    summary->addWidget(result_, 1);
    outer->addLayout(summary);
    auto *body = dialogBody(outer);
    auto *columns = new QHBoxLayout(body);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(16);
    auto *left = new QVBoxLayout;
    auto *right = new QVBoxLayout;
    left->setSpacing(12); right->setSpacing(12);
    columns->addLayout(left, 1); columns->addLayout(right, 1);
    auto spin = [&](const QString &name, int decimals = 0) {
        auto *box = new QDoubleSpinBox;
        box->setObjectName(name);
        box->setDecimals(decimals);
        box->setKeyboardTracking(false);
        box->setRange(decimals ? .01 : 1, 100000);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setMinimumWidth(80);
        return box;
    };
    auto section = [&](QVBoxLayout *column, const QString &title = QString()) {
        auto *group = new QGroupBox(title);
        auto *layout = new QVBoxLayout(group);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);
        column->addWidget(group);
        return group;
    };
    auto dimensions = [&](QGroupBox *group, QDoubleSpinBox *width, QDoubleSpinBox *height) {
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        row->addWidget(new QLabel(IX_TEXT("Breite:")));
        row->addWidget(width, 1);
        row->addWidget(new QLabel(IX_TEXT("Höhe:")));
        row->addWidget(height, 1);
        static_cast<QVBoxLayout *>(group->layout())->addLayout(row);
    };
    absoluteGroup_ = section(left);
    absolute_ = new QRadioButton(IX_TEXT("Neue Größe:"));
    absolute_->setChecked(true);
    absoluteGroup_->layout()->addWidget(absolute_);
    width_ = spin(QStringLiteral("width")); height_ = spin(QStringLiteral("height"));
    dimensions(absoluteGroup_, width_, height_);
    auto *unitRow = new QHBoxLayout;
    unitRow->setSpacing(8);
    unitRow->addWidget(new QLabel(IX_TEXT("Einheit:")));
    auto *units = new QButtonGroup(this);
    units->setObjectName(QStringLiteral("resizeUnits"));
    const QStringList unitNames{IX_TEXT("Pixel"), QStringLiteral("cm"), QStringLiteral("inch")};
    for (int i = 0; i < 3; ++i) {
        auto *radio = new QRadioButton(unitNames[i]);
        units->addButton(radio, i);
        unitRow->addWidget(radio);
        if (!i) radio->setChecked(true);
    }
    unitRow->addStretch();
    static_cast<QVBoxLayout *>(absoluteGroup_->layout())->addLayout(unitRow);
    connect(units, &QButtonGroup::idClicked, this, [this](int value) {
        model_.setUnit(ResizeModel::Unit(value)); sync(); updateMode();
    });
    percentGroup_ = section(left);
    percent_ = new QRadioButton(IX_TEXT("Prozentsatz vom Original:"));
    percentGroup_->layout()->addWidget(percent_);
    percentW_ = spin(QStringLiteral("percentWidth"), 2); percentH_ = spin(QStringLiteral("percentHeight"), 2);
    percentW_->setSuffix(QStringLiteral(" %")); percentH_->setSuffix(QStringLiteral(" %"));
    dimensions(percentGroup_, percentW_, percentH_);
    mpGroup_ = section(left);
    megapixel_ = new QRadioButton(IX_TEXT("Größe in MegaPixel setzen:"));
    mpGroup_->layout()->addWidget(megapixel_);
    mp_ = spin(QStringLiteral("megapixels"), 2);
    mpGroup_->layout()->addWidget(mp_);
    auto *modes = new QButtonGroup(this);
    modes->addButton(absolute_); modes->addButton(percent_); modes->addButton(megapixel_);
    connect(modes, &QButtonGroup::buttonClicked, this, [this] {
        if (megapixel_->isChecked()) { model_.setMegapixels(mp_->value()); sync(); }
        updateMode();
    });
    const auto useAbsoluteSize = [this] { absolute_->setChecked(true); updateMode(); sync(); };
    proportional_ = new QCheckBox(IX_TEXT("Proportional"));
    proportional_->setObjectName(QStringLiteral("proportional"));
    proportional_->setChecked(true);
    left->addWidget(proportional_);
    sharpen_ = new QCheckBox(IX_TEXT("Schärfen nach Resample anwenden"));
    left->addWidget(sharpen_);
    adaptDpi_ = new QCheckBox(IX_TEXT("DPI auf neue Größe anpassen (proport.)"));
    left->addWidget(adaptDpi_);
    auto *density = dialogFormLayout();
    dpi_ = spin(QStringLiteral("dpi")); dpi_->setValue(dpi);
    dpi_->setToolTip(IX_TEXT("(Automatik für cm/inch)"));
    density->addRow(QStringLiteral("DPI:"), dpi_);
    left->addLayout(density);
    left->addStretch();
    auto *standard = section(right, IX_TEXT("Einige Standard-Dimensionen: (Pixel):"));
    custom_ = new QComboBox;
    custom_->addItem(IX_TEXT("Neue Größe:"));
    custom_->addItems(QSettings().value(QStringLiteral("resize/presets")).toStringList());
    custom_->setToolTip(IX_TEXT("(verwendet 'Proportional')"));
    standard->layout()->addWidget(custom_);
    const QList<QSize> sizes{{640, 480}, {800, 600}, {1024, 768}, {1280, 720},
                             {1920, 1080}, {3840, 2160}, {7680, 4320}};
    const QStringList names{QStringLiteral("640 x 480"), QStringLiteral("800 x 600"),
        QStringLiteral("1024 x 768"), QStringLiteral("1280 x 720 (HD)"), QStringLiteral("1920 x 1080 (FHD)"),
        QStringLiteral("3840 x 2160 (4K)"), QStringLiteral("7680 x 4320 (8K)")};
    auto *presets = new QButtonGroup(this);
    auto *presetGrid = new QGridLayout;
    presetGrid->setHorizontalSpacing(16); presetGrid->setVerticalSpacing(8);
    for (int i = 0; i < sizes.size(); ++i) {
        auto *radio = new QRadioButton(names[i]);
        presets->addButton(radio, i);
        presetGrid->addWidget(radio, i < 4 ? i : i - 4, i < 4 ? 0 : 1);
    }
    static_cast<QVBoxLayout *>(standard->layout())->addLayout(presetGrid);
    connect(presets, &QButtonGroup::idClicked, this, [this, sizes, useAbsoluteSize](int index) {
        if (index < 0 || index >= sizes.size()) return;
        model_.preset(sizes[index], model_.proportional); useAbsoluteSize();
    });
    auto preset = [&](const QString &text, bool keep) {
        auto *radio = new QRadioButton(text);
        presets->addButton(radio);
        standard->layout()->addWidget(radio);
        connect(radio, &QRadioButton::clicked, this, [this, keep, useAbsoluteSize] {
            model_.preset(screen()->availableGeometry().size(), keep); useAbsoluteSize();
        });
    };
    preset(IX_TEXT("An Bildschirm anpassen"), true);
    preset(IX_TEXT("Bildschirmgröße (nicht proportional)"), false);
    auto button = [&](const QString &text, QHBoxLayout *row, auto action) {
        auto *widget = new QPushButton(text);
        row->addWidget(widget, 1);
        connect(widget, &QPushButton::clicked, this, action);
    };
    auto *scaleButtons = new QHBoxLayout; scaleButtons->setSpacing(8);
    static_cast<QVBoxLayout *>(standard->layout())->addLayout(scaleButtons);
    button(IX_TEXT("Halb"), scaleButtons, [this, useAbsoluteSize] { model_.scale(.5); useAbsoluteSize(); });
    button(IX_TEXT("Doppelt"), scaleButtons, [this, useAbsoluteSize] { model_.scale(2); useAbsoluteSize(); });
    auto *presetButtons = new QHBoxLayout; presetButtons->setSpacing(8);
    static_cast<QVBoxLayout *>(standard->layout())->addLayout(presetButtons);
    button(IX_TEXT("Seiten vertauschen"), presetButtons, [this, useAbsoluteSize] { model_.swapSides(); useAbsoluteSize(); });
    button(IX_TEXT("Zu Standards hinzuf."), presetButtons, [this] {
        const auto size = QStringLiteral("%1 x %2").arg(model_.size().width()).arg(model_.size().height());
        if (custom_->findText(size) < 0) custom_->addItem(size);
    });
    connect(custom_, &QComboBox::activated, this, [this, useAbsoluteSize](int index) {
        if (!index) return;
        const auto parts = custom_->currentText().split(QStringLiteral(" x "));
        if (parts.size() == 2) {
            model_.preset(QSize(parts[0].toInt(), parts[1].toInt()), model_.proportional); useAbsoluteSize();
        }
    });
    auto *method = section(right, IX_TEXT("Methode:"));
    filter_ = new QComboBox;
    filter_->setObjectName(QStringLiteral("filter"));
    filter_->addItems(resizeFilterLabels()); filter_->setCurrentIndex(5);
    filter_->setItemData(int(ResampleFilter::Nearest),
        IX_TEXT("Ohne Glättung: Jeder Pixel bleibt unverändert. Für gleichmäßige Pixelblöcke 200 %, 300 % oder andere ganze Vielfache wählen."), Qt::ToolTipRole);
    method->layout()->addWidget(filter_);
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] { updateMode(); });
    fast_ = new QCheckBox(IX_TEXT("Schnellen Filter fürs Verkleinern verwenden"));
    fast_->setChecked(true); method->layout()->addWidget(fast_);
    gamma_ = new QCheckBox(IX_TEXT("Gamma bei Resample verbessern (siehe Hilfe)"));
    method->layout()->addWidget(gamma_);
    right->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Cancel)->setText(IX_TEXT("Abbrechen"));
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &ResizeDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(std::min(900, screen()->availableGeometry().width() - 48),
           std::min(640, screen()->availableGeometry().height() - 64));
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
