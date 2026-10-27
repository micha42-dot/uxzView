// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "rotation_dialog.h"
#include "dialog_layout.h"
#include "core/localization.h"
#include "platform_support.h"
#include <QtWidgets>
#include <cmath>
#include <numbers>
namespace ix {
class RotationPreview : public QWidget {
  public:
    QImage image;
    bool source = false, drawing = false, line = false;
    QPoint from, to;
    std::function<void(double)> angleChanged;
    std::function<void()> rightClick;
    using QWidget::QWidget;

  protected:
    QRect imageRect() const {
        auto size = image.size().scaled(this->size() - QSize(4, 4), Qt::KeepAspectRatio);
        return {(width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height()};
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(imageRect(), image);
        p.setPen(palette().color(QPalette::Mid));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        if (line) {
            p.setPen(QPen(Qt::black, 3));
            p.drawLine(from, to);
            p.setPen(QPen(Qt::white, 1));
            p.drawLine(from, to);
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::RightButton) {
            if (rightClick)
                rightClick();
            return;
        }
        if (source && e->button() == Qt::LeftButton && imageRect().contains(e->position().toPoint())) {
            drawing = line = true;
            from = to = e->position().toPoint();
            update();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (drawing) {
            to = e->position().toPoint();
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (!drawing || e->button() != Qt::LeftButton)
            return;
        drawing = false;
        to = e->position().toPoint();
        auto d = to - from;
        if (std::hypot(d.x(), d.y()) < 3)
            return;
        double angle = -std::atan2(double(d.y()), double(d.x())) * 180 / std::numbers::pi;
        if (angle > 90)
            angle -= 180;
        if (angle < -90)
            angle += 180;
        if (angleChanged)
            angleChanged(angle);
        update();
    }
};
RotationDialog::RotationDialog(const QImage &image, QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("rotationDialog"));
    setWindowTitle(IX_TEXT("Winkelrotation"));
    auto *outer = dialogLayout(this);
    auto *body = dialogBody(outer);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto *previews = new QGridLayout;
    previews->setHorizontalSpacing(16);
    previews->setVerticalSpacing(8);
    auto *sourceTitle = new QLabel(IX_TEXT("Originalbild"));
    auto *resultTitle = new QLabel(IX_TEXT("Neues Bild"));
    sourceTitle->setAlignment(Qt::AlignCenter);
    resultTitle->setAlignment(Qt::AlignCenter);
    previews->addWidget(sourceTitle, 0, 0);
    previews->addWidget(resultTitle, 0, 1);
    original_ = new RotationPreview(this);
    original_->setObjectName(QStringLiteral("rotationOriginal"));
    original_->setMinimumSize(175, 170);
    original_->source = true;
    result_ = new RotationPreview(this);
    result_->setObjectName(QStringLiteral("rotationResult"));
    result_->setMinimumSize(175, 170);
    const auto instructions = IX_TEXT("1) Links eine Linie im Originalbild ziehen.\nRechtsklick übernimmt die Drehung.\n\n2) Rechtsklick im Ergebnis setzt den Winkel zurück.");
    original_->setToolTip(instructions);
    result_->setToolTip(instructions);
    previews->addWidget(original_, 1, 0);
    previews->addWidget(result_, 1, 1);
    previews->setColumnStretch(0, 1);
    previews->setColumnStretch(1, 1);
    layout->addLayout(previews);
    thumbnail_ = image.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    original_->image = thumbnail_;
    auto *form = dialogFormLayout();
    angle_ = new QDoubleSpinBox(this);
    angle_->setObjectName(QStringLiteral("angle"));
    angle_->setRange(-360, 360);
    angle_->setDecimals(1);
    angle_->setSingleStep(.1);
    angle_->setValue(45);
    angle_->setToolTip(IX_TEXT("−360° bis 360°; Schritt 0,1° (Strg: 1°)"));
    form->addRow(IX_TEXT("Winkel:"), angle_);
    layout->addLayout(form);
    auto check = [&](const QString &text, const QString &name) {
        auto *widget = new QCheckBox(text, this);
        widget->setObjectName(name);
        layout->addWidget(widget);
        connect(widget, &QCheckBox::toggled, this, &RotationDialog::updatePreview);
        return widget;
    };
    canvas_ = check(IX_TEXT("Originalgröße der Leinwand beibehalten"), QStringLiteral("keepCanvas"));
    background_ = check(IX_TEXT("Originalbild als Hintergrund beibehalten"), QStringLiteral("originalBackground"));
    background_->setEnabled(false);
    fast_ = check(IX_TEXT("Schnelle Rotation ohne Kantenglättung verwenden"), QStringLiteral("fastMethod"));
    antialias_ = check(IX_TEXT("Kantenglättung aktivieren (bessere Qualität, langsamer)"), QStringLiteral("antialias"));
    select_ = check(IX_TEXT("Größten Originalbereich im Ergebnis markieren"), QStringLiteral("selectOriginal"));
    sharpen_ = new QCheckBox(IX_TEXT("Nach der Rotation schärfen:"));
    sharpen_->setObjectName(QStringLiteral("sharpen"));
    connect(sharpen_, &QCheckBox::toggled, this, &RotationDialog::updatePreview);
    amount_ = new QSpinBox;
    amount_->setObjectName(QStringLiteral("sharpenAmount"));
    amount_->setRange(1, 99);
    amount_->setValue(20);
    amount_->setEnabled(false);
    auto *sharpenRow = new QHBoxLayout;
    sharpenRow->setSpacing(8);
    sharpenRow->addWidget(sharpen_);
    sharpenRow->addWidget(amount_);
    sharpenRow->addStretch();
    layout->addLayout(sharpenRow);
    auto *colorRow = new QHBoxLayout;
    colorRow->setSpacing(8);
    auto *colorTitle = new QLabel(IX_TEXT("Hintergrundfarbe (4-, 8-, 24-BPP-Bilder)"));
    colorTitle->setWordWrap(true);
    colorRow->addWidget(colorTitle, 1);
    color_ = new QPushButton(IX_TEXT("Wählen"));
    color_->setObjectName(QStringLiteral("backgroundColor"));
    colorRow->addWidget(color_);
    layout->addLayout(colorRow);
    layout->addStretch();
    connect(color_, &QPushButton::clicked, this, [this] {
        const auto color = QColorDialog::getColor(backgroundColor_, this, IX_TEXT("Hintergrundfarbe"));
        if (color.isValid()) {
            backgroundColor_ = color;
            updatePreview();
        }
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("rotationOk"));
    buttons->button(QDialogButtonBox::Cancel)->setText(IX_TEXT("Abbrechen"));
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(620, std::min(640, screen()->availableGeometry().height() - 64));
    original_->angleChanged = [this](double angle) { angle_->setValue(angle); };
    original_->rightClick = [this] { accept(); };
    result_->rightClick = [this] {
        angle_->setValue(0);
        original_->line = false;
        original_->update();
    };
    connect(angle_, &QDoubleSpinBox::valueChanged, this, &RotationDialog::updatePreview);
    connect(amount_, &QSpinBox::valueChanged, this, &RotationDialog::updatePreview);
    antialias_->setChecked(true);
    updatePreview();
}
RotationOptions RotationDialog::options() const {
    RotationOptions o;
    o.angle = angle_->value();
    o.keepCanvas = canvas_->isChecked();
    o.originalAsBackground = canvas_->isChecked() && background_->isChecked();
    o.fastMethod = fast_->isChecked();
    o.antialias = antialias_->isChecked() && !fast_->isChecked();
    o.selectOriginal = select_->isChecked();
    o.sharpen = sharpen_->isChecked();
    o.sharpenAmount = amount_->value();
    o.background = backgroundColor_;
    return o;
}
void RotationDialog::updatePreview() {
    background_->setEnabled(canvas_->isChecked());
    antialias_->setEnabled(!fast_->isChecked());
    amount_->setEnabled(sharpen_->isChecked());
    QPixmap swatch(18, 14);
    swatch.fill(backgroundColor_);
    color_->setIcon(QIcon(swatch));
    try {
        auto o = options();
        o.selectOriginal = false;
        result_->image = rotateImage(thumbnail_, o).pixels;
        result_->setToolTip(original_->toolTip());
    } catch (const std::exception &e) {
        result_->image = {};
        result_->setToolTip(QString::fromUtf8(e.what()));
    }
    result_->update();
}
} // namespace ix
