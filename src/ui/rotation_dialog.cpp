// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "rotation_dialog.h"
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
    setFont(referenceFont(8));
    setFixedSize(380, 544);
    auto label = [&](QString text, QRect rect) {
        auto *l = new QLabel(text, this);
        l->setGeometry(rect);
        return l;
    };
    label(IX_TEXT("Originalbild"), {10, 8, 175, 18});
    label(IX_TEXT("Neues Bild"), {195, 8, 175, 18});
    original_ = new RotationPreview(this);
    original_->setObjectName(QStringLiteral("rotationOriginal"));
    original_->setGeometry(10, 28, 175, 170);
    original_->source = true;
    result_ = new RotationPreview(this);
    result_->setObjectName(QStringLiteral("rotationResult"));
    result_->setGeometry(195, 28, 175, 170);
    thumbnail_ = image.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    original_->image = thumbnail_;
    label(IX_TEXT("Winkel:"), {10, 208, 40, 22});
    angle_ = new QDoubleSpinBox(this);
    angle_->setObjectName(QStringLiteral("angle"));
    angle_->setGeometry(52, 208, 80, 22);
    angle_->setRange(-360, 360);
    angle_->setDecimals(1);
    angle_->setSingleStep(.1);
    angle_->setValue(45);
    auto *range = label(IX_TEXT("−360° bis 360°; Schritt 0,1° (Strg: 1°)"), {10, 234, 360, 18});
    range->setFont(referenceFont(7));
    auto check = [&](QString text, QString name, int y) {
        auto *c = new QCheckBox(text, this);
        c->setObjectName(name);
        c->setGeometry(10, y, 360, 21);
        connect(c, &QCheckBox::toggled, this, &RotationDialog::updatePreview);
        return c;
    };
    canvas_ = check(IX_TEXT("Originalgröße der Leinwand beibehalten"), QStringLiteral("keepCanvas"), 257);
    background_ =
        check(IX_TEXT("Originalbild als Hintergrund beibehalten"), QStringLiteral("originalBackground"), 280);
    background_->setEnabled(false);
    fast_ =
        check(IX_TEXT("Schnelle Rotation ohne Kantenglättung verwenden"), QStringLiteral("fastMethod"), 303);
    fast_->setFont(referenceFont(7));
    antialias_ = check(IX_TEXT("Kantenglättung aktivieren (bessere Qualität, langsamer)"),
                       QStringLiteral("antialias"), 326);
    // Connect only after every dependent control exists.
    select_ = check(IX_TEXT("Größten Originalbereich im Ergebnis markieren"),
                    QStringLiteral("selectOriginal"), 349);
    sharpen_ = check(IX_TEXT("Nach der Rotation schärfen:"), QStringLiteral("sharpen"), 372);
    sharpen_->setGeometry(10, 372, 205, 21);
    amount_ = new QSpinBox(this);
    amount_->setObjectName(QStringLiteral("sharpenAmount"));
    amount_->setGeometry(220, 372, 60, 22);
    amount_->setRange(1, 99);
    amount_->setValue(20);
    amount_->setEnabled(false);
    label(QStringLiteral("(1 - 99)"), {287, 372, 65, 22});
    label(IX_TEXT("Hintergrundfarbe (4-, 8-, 24-BPP-Bilder)"), {10, 400, 262, 22});
    color_ = new QPushButton(IX_TEXT("Wählen"), this);
    color_->setObjectName(QStringLiteral("backgroundColor"));
    color_->setGeometry(297, 400, 73, 23);
    connect(color_, &QPushButton::clicked, this, [this] {
        auto color = QColorDialog::getColor(backgroundColor_, this, IX_TEXT("Hintergrundfarbe"));
        if (color.isValid()) {
            backgroundColor_ = color;
            updatePreview();
        }
    });
    auto *hints = new QGroupBox(IX_TEXT("Hinweise:"), this);
    hints->setGeometry(10, 429, 360, 76);
    auto *hint = new QLabel(IX_TEXT("1) Links eine Linie im Originalbild ziehen.\nRechtsklick übernimmt die "
                                    "Drehung.\n\n2) Rechtsklick im Ergebnis setzt den Winkel zurück."),
                            hints);
    hint->setFont(referenceFont(7));
    hint->setGeometry(8, 14, 344, 55);
    auto *ok = new QPushButton(QStringLiteral("OK"), this);
    ok->setObjectName(QStringLiteral("rotationOk"));
    ok->setGeometry(205, 514, 78, 23);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    auto *cancel = new QPushButton(IX_TEXT("Abbrechen"), this);
    cancel->setGeometry(292, 514, 78, 23);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
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
        result_->setToolTip({});
    } catch (const std::exception &e) {
        result_->image = {};
        result_->setToolTip(QString::fromUtf8(e.what()));
    }
    result_->update();
}
} // namespace ix
