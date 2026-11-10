// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "dialogs.h"
#include "dialog_layout.h"
#include "core/batch_processor.h"
#include "core/localization.h"
#include "platform_support.h"
#include "settings_dialog.h"
#include <QColorSpace>
#include <QImageReader>
#include <QPainterPath>
#include <QtConcurrent>
#include <QtWidgets>
#include <atomic>
#include <array>
#include <algorithm>
#include <numeric>
namespace ix {
namespace {
using HistogramData = std::array<std::array<quint64, 256>, 3>;
class HistogramChart final : public QWidget {
    HistogramData counts_{};
    bool ready_ = false;
    int channel_ = 0;
  public:
    HistogramChart() {
        setObjectName(QStringLiteral("histogramChart"));
        setAccessibleName(IX_TEXT("Histogramm"));
        setMinimumSize(280, 160);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    QSize sizeHint() const override { return {640, 280}; }
    void setHistogram(HistogramData counts) {
        counts_ = std::move(counts);
        ready_ = true;
        const auto pixels = std::accumulate(counts_[0].cbegin(), counts_[0].cend(), quint64(0));
        setAccessibleDescription(IX_TEXT("Histogramm für %1 Pixel").arg(uiLocale().toString(pixels)));
        update();
    }
    void setChannel(int channel) { channel_ = std::clamp(channel, 0, 3); update(); }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        painter.setPen(palette().color(QPalette::Text));
        if (!ready_) {
            painter.drawText(rect(), Qt::AlignCenter, IX_TEXT("Histogramm wird berechnet …"));
            return;
        }
        quint64 maximum = 1;
        for (int c = 0; c < 3; ++c) if (!channel_ || channel_ == c + 1)
            for (auto value : counts_[c]) maximum = std::max(maximum, value);
        const auto peak = uiLocale().toString(maximum);
        const auto metrics = fontMetrics();
        const int left = metrics.horizontalAdvance(peak) + 12;
        const QRectF plot(left, metrics.height() + 6, std::max(1, width() - left - 12),
                          std::max(1, height() - metrics.height() * 2 - 18));
        painter.drawText(QRectF(0, 0, width(), metrics.height()), Qt::AlignLeft, peak);
        painter.drawText(QRectF(plot.left(), plot.bottom() + 6, plot.width(), metrics.height()), Qt::AlignLeft, QStringLiteral("0"));
        painter.drawText(QRectF(plot.left(), plot.bottom() + 6, plot.width(), metrics.height()), Qt::AlignRight, QStringLiteral("255"));
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
        const bool dark = palette().color(QPalette::Base).lightnessF() < .5;
        const std::array<QColor, 3> colors = dark
            ? std::array<QColor, 3>{QColor(QStringLiteral("#ff8b83")), QColor(QStringLiteral("#66d99e")), QColor(QStringLiteral("#80adfa"))}
            : std::array<QColor, 3>{QColor(QStringLiteral("#bc3038")), QColor(QStringLiteral("#167548")), QColor(QStringLiteral("#285fce"))};
        const std::array<Qt::PenStyle, 3> styles{Qt::SolidLine, Qt::DashLine, Qt::DotLine};
        painter.setRenderHint(QPainter::Antialiasing);
        for (int c = 0; c < 3; ++c) {
            if (channel_ && channel_ != c + 1) continue;
            QPainterPath path;
            for (int i = 0; i < 256; ++i) {
                const QPointF point(plot.left() + plot.width() * i / 255.0,
                    plot.bottom() - plot.height() * (double(counts_[c][i]) / maximum));
                if (i) path.lineTo(point); else path.moveTo(point);
            }
            painter.setPen(QPen(colors[c], 1.25, styles[c]));
            painter.drawPath(path);
        }
    }
};
void buttons(QDialog &d, QVBoxLayout *layout) {
    auto *b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    b->button(QDialogButtonBox::Cancel)->setText(IX_TEXT("Abbrechen"));
    QObject::connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    layout->addWidget(b);
}
QSpinBox *integer(QFormLayout *f, QString label, int value, int low, int high) {
    auto *box = new QSpinBox;
    box->setRange(low, high);
    box->setValue(value);
    f->addRow(label, box);
    return box;
}
} // namespace
ColorDialog::ColorDialog(const QImage &image, QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("colorDialog"));
    setWindowTitle(IX_TEXT("Farben ändern"));
    auto *outer = dialogLayout(this);
    auto *body = dialogBody(outer);
    auto *grid = new QGridLayout(body);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(12);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    auto preview = [&](const QString &title, int column) {
        auto *label = new QLabel(title);
        label->setAlignment(Qt::AlignCenter);
        grid->addWidget(label, 0, column);
        auto *pixels = new QLabel;
        pixels->setMinimumSize(230, 150);
        pixels->setAlignment(Qt::AlignCenter);
        pixels->setFrameStyle(QFrame::Box);
        grid->addWidget(pixels, 1, column);
        return pixels;
    };
    originalLabel_ = preview(IX_TEXT("Originalbild"), 0);
    originalLabel_->setObjectName(QStringLiteral("colorOriginal"));
    preview_ = preview(IX_TEXT("Neues Bild"), 1);
    originalLabel_->installEventFilter(this);
    originalLabel_->setCursor(Qt::CrossCursor);
    originalLabel_->setToolTip(IX_TEXT("Info: Klicken Sie ins Originalbild (heller Bereich)\num Weißabgleich zu ändern (RGB-Werte)"));
    auto section = [&](const QString &title, int row, int column) {
        auto *group = new QGroupBox(title);
        auto *form = dialogFormLayout();
        form->setContentsMargins(12, 12, 12, 12);
        group->setLayout(form);
        grid->addWidget(group, row, column);
        return form;
    };
    inputs_.resize(7);
    auto control = [&](QFormLayout *form, int index, const QString &name = QString()) {
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *slider = new QSlider(Qt::Horizontal);
        auto *spin = new QDoubleSpinBox;
        spin->setObjectName(QStringList{QStringLiteral("brightness"), QStringLiteral("contrast"),
            QStringLiteral("gamma"), QStringLiteral("saturation"), QStringLiteral("red"),
            QStringLiteral("green"), QStringLiteral("blue")}[index]);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setMinimumWidth(72);
        spin->setRange(index == 2 ? .01 : index == 1 || index == 3 ? -100 : -255,
                       index == 2 ? 10 : index == 1 || index == 3 ? 100 : 255);
        spin->setDecimals(index == 2 ? 2 : 0);
        spin->setValue(index == 2 ? 1 : 0);
        inputs_[index] = spin;
        slider->setRange(index == 2 ? 1 : int(spin->minimum()), index == 2 ? 1000 : int(spin->maximum()));
        slider->setValue(index == 2 ? 100 : 0);
        layout->addWidget(slider, 1);
        layout->addWidget(spin);
        if (name.isEmpty()) form->addRow(row); else form->addRow(name, row);
        connect(slider, &QSlider::valueChanged, this, [spin, index](int value) {
            spin->setValue(index == 2 ? value / 100.0 : value);
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, index](double value) {
            QSignalBlocker block(slider);
            slider->setValue(qRound(index == 2 ? value * 100 : value));
            updatePreview();
        });
    };
    control(section(IX_TEXT("Helligkeit:"), 2, 0), 0);
    control(section(IX_TEXT("Kontrast:"), 2, 1), 1);
    auto *balance = section(IX_TEXT("Farb-Balancierung:"), 3, 0);
    for (int index = 4; index < 7; ++index)
        control(balance, index, QStringList{QStringLiteral("R:"), QStringLiteral("G:"), QStringLiteral("B:")}[index - 4]);
    auto *tones = new QWidget;
    auto *toneLayout = new QVBoxLayout(tones);
    toneLayout->setContentsMargins(0, 0, 0, 0);
    toneLayout->setSpacing(12);
    auto *gammaGroup = new QGroupBox(IX_TEXT("Gamma-Korrektur:"));
    auto *gamma = dialogFormLayout(); gamma->setContentsMargins(12, 12, 12, 12); gammaGroup->setLayout(gamma);
    control(gamma, 2);
    auto *saturationGroup = new QGroupBox(IX_TEXT("Farbsättigung:"));
    auto *saturation = dialogFormLayout(); saturation->setContentsMargins(12, 12, 12, 12); saturationGroup->setLayout(saturation);
    control(saturation, 3);
    toneLayout->addWidget(gammaGroup);
    toneLayout->addWidget(saturationGroup);
    grid->addWidget(tones, 3, 1);
    auto *profileForm = section(IX_TEXT("Profile:"), 4, 0);
    auto *profiles = new QComboBox;
    profiles->setEditable(true);
    profileForm->addRow(profiles);
    QSettings settings;
    settings.beginGroup(QStringLiteral("color/profiles"));
    profiles->addItems(settings.childKeys());
    settings.endGroup();
    auto *profileButtons = new QHBoxLayout;
    profileButtons->setSpacing(8);
    profileForm->addRow(profileButtons);
    auto button = [&](const QString &title, QHBoxLayout *layout, auto action) {
        auto *widget = new QPushButton(title);
        layout->addWidget(widget);
        connect(widget, &QPushButton::clicked, this, action);
        return widget;
    };
    button(IX_TEXT("Laden"), profileButtons, [=, this] {
        const auto list = QSettings().value(QStringLiteral("color/profiles/") + profiles->currentText()).toList();
        if (list.size() == 7) for (int i = 0; i < 7; ++i) inputs_[i]->setValue(list[i].toDouble());
    });
    button(IX_TEXT("Löschen"), profileButtons, [=] {
        QSettings().remove(QStringLiteral("color/profiles/") + profiles->currentText());
        profiles->removeItem(profiles->currentIndex());
    });
    button(IX_TEXT("Speichern"), profileButtons, [=, this] {
        const auto name = profiles->currentText().trimmed();
        if (name.isEmpty() || name.contains(QLatin1Char('/'))) return;
        QVariantList list;
        for (auto *spin : inputs_) list << spin->value();
        QSettings().setValue(QStringLiteral("color/profiles/") + name, list);
        if (profiles->findText(name) < 0) profiles->addItem(name);
    });
    auto *actions = new QWidget;
    auto *actionLayout = new QVBoxLayout(actions);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    auto *actionButtons = new QHBoxLayout;
    actionButtons->setSpacing(8);
    actionLayout->addLayout(actionButtons);
    button(IX_TEXT("Auf Bild anwenden"), actionButtons, [this] { emit applyRequested(values()); });
    button(IX_TEXT("Standardwerte"), actionButtons, [this] {
        for (int i = 0; i < inputs_.size(); ++i) inputs_[i]->setValue(i == 2 ? 1 : 0);
    });
    auto *remember = new QCheckBox(IX_TEXT("Werte speichern"));
    remember->setChecked(settings.value(QStringLiteral("color/remember"), false).toBool());
    actionLayout->addWidget(remember);
    actionLayout->addStretch();
    grid->addWidget(actions, 4, 1);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Cancel)->setText(IX_TEXT("Abbrechen"));
    outer->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, this, [this, remember] {
        QSettings settings;
        settings.setValue(QStringLiteral("color/remember"), remember->isChecked());
        QVariantList list;
        for (auto *spin : inputs_) list << spin->value();
        settings.setValue(QStringLiteral("color/values"), list);
        accept();
    });
    setImage(image);
    if (remember->isChecked()) {
        const auto list = settings.value(QStringLiteral("color/values")).toList();
        if (list.size() == 7) for (int i = 0; i < 7; ++i) inputs_[i]->setValue(list[i].toDouble());
    }
    resize(700, std::min(540, screen()->availableGeometry().height() - 64));
    updatePreview();
}
void ColorDialog::setImage(const QImage &image) {
    original_ = image.scaled(228, 148, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    originalLabel_->setPixmap(QPixmap::fromImage(original_));
    for (int i = 0; i < inputs_.size(); ++i)
        inputs_[i]->setValue(i == 2 ? 1 : 0);
    updatePreview();
}
bool ColorDialog::eventFilter(QObject *object, QEvent *event) {
    if (object == originalLabel_ && event->type() == QEvent::MouseButtonPress && !original_.isNull()) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QPoint offset((originalLabel_->width() - original_.width()) / 2,
                      (originalLabel_->height() - original_.height()) / 2);
        QPoint pixel = mouse->position().toPoint() - offset;
        if (original_.rect().contains(pixel)) {
            auto c = original_.pixelColor(pixel);
            int maximum = std::max({c.red(), c.green(), c.blue()});
            inputs_[4]->setValue(maximum - c.red());
            inputs_[5]->setValue(maximum - c.green());
            inputs_[6]->setValue(maximum - c.blue());
        }
        return true;
    }
    return QDialog::eventFilter(object, event);
}
ColorAdjust ColorDialog::values() const {
    ColorAdjust o;
    o.brightness = inputs_[0]->value();
    o.contrast = inputs_[1]->value();
    o.gamma = inputs_[2]->value();
    o.saturation = inputs_[3]->value();
    o.red = int(inputs_[4]->value());
    o.green = int(inputs_[5]->value());
    o.blue = int(inputs_[6]->value());
    return o;
}
void ColorDialog::updatePreview() {
    preview_->setPixmap(QPixmap::fromImage(adjustColors(original_, values())));
}
bool newImageDialog(QWidget *p, ImageDocument &out) {
    QDialog d(p);
    d.setWindowTitle(IX_TEXT("Neues Bild erzeugen"));
    auto *v = dialogLayout(&d);
    auto *f = dialogFormLayout();
    v->addLayout(f);
    auto *w = integer(f, IX_TEXT("Bildbreite:"), 100, 1, 100000),
         *h = integer(f, IX_TEXT("Bildhöhe:"), 100, 1, 100000),
         *dx = integer(f, QStringLiteral("X-DPI:"), 96, 1, 100000),
         *dy = integer(f, QStringLiteral("Y-DPI:"), 96, 1, 100000);
    auto *alpha = new QCheckBox(IX_TEXT("Hintergrund ist transparent"));
    v->addWidget(alpha);
    auto *gray = new QCheckBox(IX_TEXT("Graustufen erstellen"));
    v->addWidget(gray);
    QColor color(255, 128, 0);
    auto *choose = new QPushButton(IX_TEXT("Hintergrundfarbe"));
    v->addWidget(choose);
    QObject::connect(choose, &QPushButton::clicked, &d, [&] {
        auto c = QColorDialog::getColor(color, &d);
        if (c.isValid())
            color = c;
    });
    buttons(d, v);
    if (d.exec() != QDialog::Accepted)
        return false;
    if (!safeSize({w->value(), h->value()})) {
        QMessageBox::warning(p, IX_TEXT("Neues Bild"), IX_TEXT("Bildgröße überschreitet das Speicherlimit."));
        return false;
    }
    out = {};
    out.pixels =
        QImage(w->value(), h->value(), alpha->isChecked() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    out.pixels.fill(alpha->isChecked() ? QColor(Qt::transparent) : color);
    out.pixels.setDotsPerMeterX(qRound(dx->value() / .0254));
    out.pixels.setDotsPerMeterY(qRound(dy->value() / .0254));
    out.pixels.setColorSpace(QColorSpace::SRgb);
    if (gray->isChecked())
        out.pixels = grayscale(out.pixels);
    out.modified = true;
    return !out.empty();
}
bool selectionDialog(QWidget *p, QSize image, QRect &r) {
    QDialog d(p);
    d.setWindowTitle(IX_TEXT("Spezielle Markierung erstellen"));
    auto *v = dialogLayout(&d);
    auto *f = dialogFormLayout();
    v->addLayout(f);
    auto *x = integer(f, IX_TEXT("X-Position:"), r.x(), 0, image.width() - 1),
         *y = integer(f, IX_TEXT("Y-Position:"), r.y(), 0, image.height() - 1),
         *w = integer(f, IX_TEXT("Breite:"), r.isEmpty() ? image.width() : r.width(), 1, image.width()),
         *h = integer(f, IX_TEXT("Höhe:"), r.isEmpty() ? image.height() : r.height(), 1, image.height());
    buttons(d, v);
    if (d.exec() != QDialog::Accepted)
        return false;
    r = QRect(x->value(), y->value(), w->value(), h->value()).intersected(QRect(QPoint(), image));
    return true;
}
namespace {
void paintTextOverlay(QPainter &painter, const TextOverlay &overlay, const QRect &area) {
    if (overlay.text.isEmpty() || area.isEmpty()) return;
    QFont font(overlay.fontFamily, overlay.pointSize);
    font.setBold(overlay.bold); font.setItalic(overlay.italic); font.setUnderline(overlay.underline);
    painter.setFont(font); painter.setPen(overlay.color);
    painter.drawText(area, overlay.alignment | Qt::AlignTop | Qt::TextWordWrap, overlay.text);
}
QRect textPreviewCrop(const QImage &source, QRect area) {
    area = area.intersected(source.rect());
    if (area.isEmpty()) return {};
    const int margin = std::max(8, qRound(.15 * std::max(area.width(), area.height())));
    return area.adjusted(-margin, -margin, margin, margin).intersected(source.rect());
}
QImage textPreviewBackground(const QImage &source, QRect crop, QSize bounds) {
    if (crop.isEmpty() || bounds.isEmpty()) return {};
    bounds = bounds.boundedTo({1024, 1024});
    auto size = crop.size().scaled(bounds, Qt::KeepAspectRatio);
    size = {std::max(1, size.width()), std::max(1, size.height())};
    QImage background(size, QImage::Format_ARGB32_Premultiplied);
    if (background.isNull()) return {};
    background.setColorSpace(source.colorSpace());
    background.setDotsPerMeterX(source.dotsPerMeterX());
    background.setDotsPerMeterY(source.dotsPerMeterY());
    background.fill(Qt::transparent);
    QPainter painter(&background);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(background.rect(), source, crop);
    painter.end();
    return background;
}
QImage paintTextPreview(QImage background, QRect crop, const TextOverlay &overlay,
                         const QRect &area, bool frame) {
    if (background.isNull()) return {};
    QPainter painter(&background);
    // Layout keeps original coordinates and DPI. Only rasterization is scaled;
    // rounding a reduced point size changes glyph metrics and word wrapping.
    painter.setWindow(crop); painter.setViewport(background.rect());
    paintTextOverlay(painter, overlay, area);
    if (frame) {
        QPen pen(Qt::DashLine);
        pen.setWidthF(std::max(double(crop.width()) / background.width(),
                              double(crop.height()) / background.height()));
        painter.setPen(pen); painter.drawRect(area.adjusted(0, 0, -1, -1));
    }
    painter.end();
    return background;
}
class TextPreview final : public QLabel {
  public:
    TextPreview(const QImage &source, const QRect &area)
        : source_(source), area_(area), crop_(textPreviewCrop(source, area)) {}
    void setOverlay(const TextOverlay &overlay) { overlay_ = overlay; refresh(); }
  protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event); background_ = {}; refresh();
    }
  private:
    QImage source_, background_;
    QRect area_, crop_;
    TextOverlay overlay_;
    void refresh() {
        if (background_.isNull()) background_ = textPreviewBackground(source_, crop_, contentsRect().size());
        setPixmap(QPixmap::fromImage(paintTextPreview(background_, crop_, overlay_, area_, true)));
    }
};
} // namespace
QImage renderTextOverlay(const QImage &source, const TextOverlay &overlay, const QRect &area) {
    const auto wanted = source.depth() > 32 ? QImage::Format_RGBA64 : QImage::Format_ARGB32;
    auto image = source.format() == wanted || source.isNull() ? source : source.convertToFormat(wanted);
    image.detach();
    if (image.isNull() || overlay.text.isEmpty() || area.isEmpty()) return image;
    QPainter painter(&image); paintTextOverlay(painter, overlay, area); painter.end();
    inheritImageProperties(source, image);
    return image;
}
QImage renderTextOverlayPreview(const QImage &source, const TextOverlay &overlay,
                               const QRect &area, QSize bounds, bool frame) {
    const auto crop = textPreviewCrop(source, area);
    return paintTextPreview(textPreviewBackground(source, crop, bounds), crop, overlay, area, frame);
}

bool textOverlayDialog(QWidget *parent, const QImage &previewSource, const QRect &area,
                       TextOverlay &overlay) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("textOverlayDialog"));
    dialog.setWindowTitle(IX_TEXT("Text einfügen"));
    dialog.resize(720, 480);
    auto *layout = dialogLayout(&dialog);
    auto *columns = new QHBoxLayout;
    layout->addLayout(columns, 1);
    auto *left = new QVBoxLayout;
    columns->addLayout(left, 0);
    auto *textEdit = new QPlainTextEdit(overlay.text);
    textEdit->setObjectName(QStringLiteral("textOverlayText"));
    textEdit->setPlaceholderText(IX_TEXT("Text:"));
    textEdit->setMinimumHeight(70);
    left->addWidget(textEdit);
    auto *form = dialogFormLayout();
    left->addLayout(form);
    auto *family = new QFontComboBox;
    family->setObjectName(QStringLiteral("textOverlayFont"));
    family->setCurrentFont(QFont(overlay.fontFamily));
    form->addRow(IX_TEXT("Schriftart:"), family);
    auto *size = new QSpinBox;
    size->setObjectName(QStringLiteral("textOverlaySize"));
    size->setRange(6, 500);
    size->setValue(overlay.pointSize);
    form->addRow(IX_TEXT("Schriftgröße:"), size);
    auto *styleRow = new QHBoxLayout;
    auto *bold = new QPushButton(IX_TEXT("Fett"));
    bold->setObjectName(QStringLiteral("textOverlayBold"));
    bold->setCheckable(true);
    bold->setChecked(overlay.bold);
    auto *italic = new QPushButton(IX_TEXT("Kursiv"));
    italic->setObjectName(QStringLiteral("textOverlayItalic"));
    italic->setCheckable(true);
    italic->setChecked(overlay.italic);
    auto *underline = new QPushButton(IX_TEXT("Unterstrichen"));
    underline->setObjectName(QStringLiteral("textOverlayUnderline"));
    underline->setCheckable(true);
    underline->setChecked(overlay.underline);
    styleRow->addWidget(bold);
    styleRow->addWidget(italic);
    styleRow->addWidget(underline);
    form->addRow(IX_TEXT("Stil:"), styleRow);
    auto *colorRow = new QHBoxLayout;
    auto *swatch = new QLabel;
    swatch->setObjectName(QStringLiteral("textOverlaySwatch"));
    swatch->setMinimumSize(64, 28);
    swatch->setFrameStyle(QFrame::Box);
    swatch->setAutoFillBackground(true);
    auto *colorButton = new QPushButton(IX_TEXT("Textfarbe"));
    colorButton->setObjectName(QStringLiteral("textOverlayColor"));
    colorButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    colorRow->addWidget(swatch);
    colorRow->addWidget(colorButton);
    colorRow->addStretch(1);
    form->addRow(IX_TEXT("Farbe:"), colorRow);
    auto *align = new QComboBox;
    align->setObjectName(QStringLiteral("textOverlayAlign"));
    align->addItem(IX_TEXT("Links"), int(Qt::AlignLeft));
    align->addItem(IX_TEXT("Zentriert"), int(Qt::AlignHCenter));
    align->addItem(IX_TEXT("Rechts"), int(Qt::AlignRight));
    align->setCurrentIndex(qMax(0, align->findData(int(overlay.alignment))));
    form->addRow(IX_TEXT("Ausrichtung:"), align);
    auto *previewBox = new QVBoxLayout;
    columns->addLayout(previewBox, 1);
    auto *previewLabel = new QLabel(IX_TEXT("Vorschau"));
    previewLabel->setObjectName(QStringLiteral("textOverlayPreviewLabel"));
    previewBox->addWidget(previewLabel);
    auto *preview = new TextPreview(previewSource, area);
    preview->setObjectName(QStringLiteral("textOverlayPreview"));
    preview->setMinimumSize(280, 240);
    preview->setFrameStyle(QFrame::Box);
    preview->setAlignment(Qt::AlignCenter);
    preview->setAccessibleName(IX_TEXT("Vorschau"));
    preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    previewBox->addWidget(preview, 1);
    auto *placeLabel = new QLabel;
    placeLabel->setObjectName(QStringLiteral("textOverlayPlace"));
    placeLabel->setWordWrap(true);
    placeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewBox->addWidget(placeLabel);
    auto refresh = [&] {
        overlay.text = textEdit->toPlainText();
        overlay.fontFamily = family->currentFont().family();
        overlay.pointSize = size->value();
        overlay.bold = bold->isChecked();
        overlay.italic = italic->isChecked();
        overlay.underline = underline->isChecked();
        overlay.alignment = Qt::Alignment(align->currentData().toInt());
        QPalette palette = swatch->palette();
        palette.setColor(QPalette::Window, overlay.color);
        swatch->setPalette(palette);
        if (area.isEmpty() || previewSource.isNull()) {
            preview->clear();
            placeLabel->clear();
            return;
        }
        preview->setOverlay(overlay);
        placeLabel->setText(IX_TEXT("Bereich: %1 × %2 an (%3, %4)")
                                .arg(area.width())
                                .arg(area.height())
                                .arg(area.x())
                                .arg(area.y()));
    };
    QObject::connect(textEdit, &QPlainTextEdit::textChanged, &dialog, refresh);
    QObject::connect(family, &QFontComboBox::currentFontChanged, &dialog, refresh);
    QObject::connect(size, qOverload<int>(&QSpinBox::valueChanged), &dialog, refresh);
    QObject::connect(bold, &QPushButton::toggled, &dialog, refresh);
    QObject::connect(italic, &QPushButton::toggled, &dialog, refresh);
    QObject::connect(underline, &QPushButton::toggled, &dialog, refresh);
    QObject::connect(align, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, refresh);
    QObject::connect(colorButton, &QPushButton::clicked, &dialog, [&] {
        const auto next = QColorDialog::getColor(overlay.color, &dialog, IX_TEXT("Textfarbe"));
        if (next.isValid()) {
            overlay.color = next;
            refresh();
        }
    });
    refresh();
    buttons(dialog, layout);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    refresh();
    return true;
}


bool canvasDialog(QWidget *p, int &l, int &r, int &t, int &b, QColor &c) {
    QDialog d(p);
    d.setWindowTitle(IX_TEXT("Farb-Rahmen hinzufügen (Leinwandgröße ändern)"));
    auto *v = dialogLayout(&d);
    auto *f = dialogFormLayout();
    v->addLayout(f);
    auto *ls = integer(f, IX_TEXT("Links:"), l, -100000, 100000),
         *rs = integer(f, IX_TEXT("Rechts:"), r, -100000, 100000),
         *ts = integer(f, IX_TEXT("Oben:"), t, -100000, 100000),
         *bs = integer(f, IX_TEXT("Unten:"), b, -100000, 100000);
    auto *choose = new QPushButton(IX_TEXT("Rahmenfarbe"));
    v->addWidget(choose);
    QObject::connect(choose, &QPushButton::clicked, &d, [&] {
        auto n = QColorDialog::getColor(c, &d);
        if (n.isValid())
            c = n;
    });
    buttons(d, v);
    if (d.exec() != QDialog::Accepted)
        return false;
    l = ls->value();
    r = rs->value();
    t = ts->value();
    b = bs->value();
    return true;
}
void imageInformation(QWidget *p, const ImageDocument &d) {
    QDialog dialog(p);
    dialog.setWindowTitle(IX_TEXT("Bild-Information"));
    dialog.resize(570, 440);
    auto *v = dialogLayout(&dialog);
    auto *f = dialogFormLayout();
    v->addLayout(f);
    auto row = [&](QString a, QString b) {
        auto *l = new QLabel(b);
        l->setTextFormat(Qt::PlainText);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        l->setWordWrap(true);
        f->addRow(a, l);
    };
    row(IX_TEXT("Dateiname:"), d.path.isEmpty() ? IX_TEXT("Zwischenablage") : d.path);
    row(IX_TEXT("Bildformat:"), QString::fromLatin1(d.format).toUpper());
    row(IX_TEXT("Aktuelle Größe:"),
        QStringLiteral("%1 x %2 Pixel").arg(d.pixels.width()).arg(d.pixels.height()));
    row(IX_TEXT("Farbtiefe:"), QStringLiteral("%1 BPP").arg(d.pixels.depth()));
    row(IX_TEXT("Auflösung:"),
        QStringLiteral("%1 x %2 DPI").arg(d.dpiX(), 0, 'f', 2).arg(d.dpiY(), 0, 'f', 2));
    row(IX_TEXT("Speicherbedarf:"),
        QStringLiteral("%1 MiB").arg(d.pixels.sizeInBytes() / 1048576.0, 0, 'f', 2));
    row(IX_TEXT("Seite:"), QStringLiteral("%1 / %2").arg(d.page + 1).arg(d.pageCount));
    row(IX_TEXT("Farbprofil:"), d.pixels.colorSpace().isValid() ? d.pixels.colorSpace().description()
                                                                : IX_TEXT("Kein eingebettetes ICC-Profil"));
    row(IX_TEXT("Alphakanal:"), d.pixels.hasAlphaChannel() ? IX_TEXT("Ja") : IX_TEXT("Nein"));
    auto *text = new QPlainTextEdit;
    text->setAccessibleName(IX_TEXT("Metadaten"));
    text->setReadOnly(true);
    QString report;
    for (auto it = d.text.begin(); it != d.text.end(); ++it)
        report += it.key() + QStringLiteral(": ") + it.value() + QLatin1Char('\n');
    if (d.metadata)
        for (auto it = d.metadata->tags.begin(); it != d.metadata->tags.end(); ++it)
            report += it.key() + QStringLiteral(": ") + it.value() + QLatin1Char('\n');
    if (!d.metadataWarning.isEmpty())
        report += IX_TEXT("Metadaten-Hinweis: ") + d.metadataWarning;
    text->setPlainText(report);
    v->addWidget(text);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    v->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}
void histogramDialog(QWidget *parent, const QImage &image) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("histogramDialog"));
    dialog.setWindowTitle(IX_TEXT("Histogramm"));
    dialog.resize(680, 390);
    auto *layout = dialogLayout(&dialog);
    auto *channels = new QComboBox;
    channels->setObjectName(QStringLiteral("histogramChannel"));
    channels->setAccessibleName(IX_TEXT("Farbkanal anzeigen"));
    channels->addItems({QStringLiteral("RGB"), IX_TEXT("Rot"), IX_TEXT("Grün"), IX_TEXT("Blau")});
    layout->addWidget(channels, 0, Qt::AlignLeft);
    auto *chart = new HistogramChart;
    layout->addWidget(chart, 1);
    QObject::connect(channels, qOverload<int>(&QComboBox::currentIndexChanged), chart,
                     [chart](int channel) { chart->setChannel(channel); });
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    QFutureWatcher<HistogramData> watcher;
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &dialog, [&] {
        if (!cancelled->load()) chart->setHistogram(watcher.future().takeResult());
    });
    watcher.setFuture(QtConcurrent::run([image, cancelled] {
        HistogramData data{};
        // Convert a row at a time so the histogram does not duplicate a large source.
        for (int y = 0; y < image.height(); ++y) {
            if (cancelled->load())
                return HistogramData{};
            auto line = image.copy(0, y, image.width(), 1).convertToFormat(QImage::Format_ARGB32);
            if (line.isNull())
                return HistogramData{};
            auto *pixels = reinterpret_cast<const QRgb *>(line.constScanLine(0));
            for (int x = 0; x < image.width(); ++x) {
                ++data[0][qRed(pixels[x])];
                ++data[1][qGreen(pixels[x])];
                ++data[2][qBlue(pixels[x])];
            }
        }
        return data;
    }));
    dialog.exec();
    cancelled->store(true);
    watcher.waitForFinished();
}
void colorPickerDialog(QWidget *parent, const QImage &image, QPoint position) {
    // A single document pixel, nothing derived: the dialog shows exactly the
    // stored value, independent of zoom, fit or display sharpening. Out-of-range
    // positions cannot occur (the picker clamps), but guard anyway.
    const QColor color = (position.x() >= 0 && position.y() >= 0 && position.x() < image.width() &&
                          position.y() < image.height())
                             ? image.pixelColor(position.x(), position.y())
                             : QColor();
    if (!color.isValid())
        return;
    const QString hex = QStringLiteral("#%1%2%3")
                            .arg(color.red(), 2, 16, QLatin1Char('0'))
                            .arg(color.green(), 2, 16, QLatin1Char('0'))
                            .arg(color.blue(), 2, 16, QLatin1Char('0'))
                            .toUpper();
    const QString rgb = QStringLiteral("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue());
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("colorPickerDialog"));
    dialog.setWindowTitle(IX_TEXT("Farbpipette"));
    dialog.resize(320, 220);
    auto *layout = dialogLayout(&dialog);
    auto *swatch = new QLabel;
    swatch->setObjectName(QStringLiteral("colorPickerSwatch"));
    swatch->setMinimumHeight(64);
    swatch->setFrameStyle(QFrame::Box);
    swatch->setAutoFillBackground(true);
    QPalette palette = swatch->palette();
    palette.setColor(QPalette::Window, color);
    swatch->setPalette(palette);
    swatch->setAccessibleName(IX_TEXT("Gewählte Farbe"));
    layout->addWidget(swatch);
    auto *positionLabel = new QLabel(IX_TEXT("Pixel (%1, %2)").arg(position.x()).arg(position.y()));
    positionLabel->setObjectName(QStringLiteral("colorPickerPosition"));
    positionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(positionLabel);
    auto *hexRow = new QHBoxLayout;
    auto *hexLabel = new QLabel(QStringLiteral("Hex:"));
    auto *hexValue = new QLineEdit(hex);
    hexValue->setObjectName(QStringLiteral("colorPickerHex"));
    hexValue->setReadOnly(true);
    auto *hexCopy = new QPushButton(IX_TEXT("Hex kopieren"));
    hexCopy->setObjectName(QStringLiteral("colorPickerCopyHex"));
    QObject::connect(hexCopy, &QPushButton::clicked, hexCopy, [hexCopy, hex] {
        QApplication::clipboard()->setText(hex);
        hexCopy->setText(IX_TEXT("Hex in Zwischenablage kopiert."));
    });
    hexRow->addWidget(hexLabel);
    hexRow->addWidget(hexValue, 1);
    hexRow->addWidget(hexCopy);
    layout->addLayout(hexRow);
    auto *rgbRow = new QHBoxLayout;
    auto *rgbLabel = new QLabel(QStringLiteral("RGB:"));
    auto *rgbValue = new QLineEdit(rgb);
    rgbValue->setObjectName(QStringLiteral("colorPickerRgb"));
    rgbValue->setReadOnly(true);
    auto *rgbCopy = new QPushButton(IX_TEXT("RGB kopieren"));
    rgbCopy->setObjectName(QStringLiteral("colorPickerCopyRgb"));
    QObject::connect(rgbCopy, &QPushButton::clicked, rgbCopy, [rgbCopy, rgb] {
        QApplication::clipboard()->setText(rgb);
        rgbCopy->setText(IX_TEXT("RGB in Zwischenablage kopiert."));
    });
    rgbRow->addWidget(rgbLabel);
    rgbRow->addWidget(rgbValue, 1);
    rgbRow->addWidget(rgbCopy);
    layout->addLayout(rgbRow);
    if (color.alpha() < 255) {
        auto *alphaLabel = new QLabel(IX_TEXT("Alpha: %1").arg(color.alpha()));
        alphaLabel->setObjectName(QStringLiteral("colorPickerAlpha"));
        alphaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(alphaLabel);
    }
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}
void formatDialog(QWidget *p, const CodecRegistry &registry) {
    QDialog d(p);
    d.setWindowTitle(IX_TEXT("üxzView – verfügbare Bildformate"));
    d.resize(650, 450);
    auto *v = dialogLayout(&d);
    auto *table = new QTableWidget(0, 5);
    table->setHorizontalHeaderLabels({IX_TEXT("Format"), IX_TEXT("Erweiterungen"), IX_TEXT("Lesen"),
                                      IX_TEXT("Schreiben"), IX_TEXT("Bibliothek")});
    for (const auto &f : registry.capabilities()) {
        int row = table->rowCount();
        table->insertRow(row);
        QStringList cells{f.name, f.extensions.join(QStringLiteral(", ")),
                          f.read ? IX_TEXT("Ja") : IX_TEXT("Nein"), f.write ? IX_TEXT("Ja") : IX_TEXT("Nein"),
                          f.library};
        for (int c = 0; c < 5; ++c)
            table->setItem(row, c, new QTableWidgetItem(cells[c]));
    }
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    v->addWidget(table);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    v->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    d.exec();
}
BatchFileChanges batchDialog(QWidget *p, const QStringList &initial) {
    BatchFileChanges changes;
    QDialog d(p);
    d.setWindowTitle(IX_TEXT("Batch(Stapel)-Konvertierung/Umbenennung"));
    d.resize(720, 570);
    auto *v = dialogLayout(&d);
    auto *files = new QListWidget;
    files->addItems(initial);
    files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->addWidget(files);
    auto *add = new QPushButton(IX_TEXT("Dateien hinzufügen..."));
    v->addWidget(add);
    QObject::connect(add, &QPushButton::clicked, &d, [&] {
        auto paths =
            QFileDialog::getOpenFileNames(&d, IX_TEXT("Bilddateien"), {}, CodecRegistry().openFilter());
        for (const auto &path : paths)
            if (files->findItems(path, Qt::MatchExactly).isEmpty())
                files->addItem(path);
    });
    auto *remove = new QPushButton(IX_TEXT("Aus Liste entfernen"));
    v->addWidget(remove);
    QObject::connect(remove, &QPushButton::clicked, &d, [&] { qDeleteAll(files->selectedItems()); });
    auto *f = dialogFormLayout();
    v->addLayout(f);
    auto *mode = new QComboBox;
    mode->addItems({IX_TEXT("Batch-Konvertierung"), IX_TEXT("Batch-Umbenennung (Dateien verschieben)")});
    mode->setCurrentIndex(1);
    f->addRow(IX_TEXT("Arbeite als:"), mode);
    auto *format = new QComboBox;
    for (const auto &c : CodecRegistry().capabilities())
        if (c.write)
            format->addItem(c.name, c.extensions.first());
    format->setCurrentIndex(std::max(0, format->findData(QStringLiteral("png"))));
    f->addRow(IX_TEXT("Zielformat:"), format);
    auto *quality = integer(f, IX_TEXT("Qualität (0–100):"), 95, 0, 100);
    quality->setObjectName(QStringLiteral("batchQuality"));
    auto *targetKb = integer(f, IX_TEXT("Zieldateigröße (KB, 0 = aus):"), 0, 0, 2048000);
    targetKb->setObjectName(QStringLiteral("batchTargetKb"));
    auto *pattern = new QLineEdit(QStringLiteral("{name}_####"));
    f->addRow(IX_TEXT("Name ({name}, ####):"), pattern);
    auto *output = new QLineEdit;
    auto *choose = new QPushButton(IX_TEXT("Zielverzeichnis wählen..."));
    f->addRow(choose, output);
    QObject::connect(choose, &QPushButton::clicked, &d, [&] {
        auto path = QFileDialog::getExistingDirectory(&d, IX_TEXT("Zielverzeichnis"));
        if (!path.isEmpty())
            output->setText(path);
    });
    auto *width = integer(f, IX_TEXT("Max. Breite (0 = unverändert):"), 0, 0, 100000),
         *height = integer(f, IX_TEXT("Max. Höhe (0 = unverändert):"), 0, 0, 100000);
    auto *loss =
        new QCheckBox(IX_TEXT("Verlust von EXIF/Animation/Farbtiefe/Transparenz beim Export zulassen"));
    v->addWidget(loss);
    const auto updateMode = [=] {
        const bool converting = mode->currentIndex() == 0;
        for (auto *control : QList<QWidget *>{format, quality, targetKb, width, height, loss})
            control->setEnabled(converting);
    };
    QObject::connect(mode, &QComboBox::currentIndexChanged, &d, [updateMode](int) { updateMode(); });
    updateMode();
    auto *note = new QLabel(IX_TEXT(
        "Vorhandene Dateien werden übersprungen. Erfolgreiche Umbenennungen bleiben bei Abbruch bestehen."));
    note->setWordWrap(true);
    v->addWidget(note);
    auto *progress = new QProgressBar;
    v->addWidget(progress);
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    v->addWidget(log);
    auto *controls = new QDialogButtonBox;
    auto *start = controls->addButton(IX_TEXT("Starten"), QDialogButtonBox::ActionRole);
    auto *close = controls->addButton(IX_TEXT("Schließen"), QDialogButtonBox::RejectRole);
    v->addWidget(controls);
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    QFutureWatcher<QList<BatchEntry>> watcher;
    bool running = false;
    QObject::connect(start, &QPushButton::clicked, &d, [&] {
        if (running) {
            cancellation->store(true);
            return;
        }
        if (output->text().isEmpty() || !files->count()) {
            QMessageBox::warning(&d, QStringLiteral("Batch"),
                                 IX_TEXT("Dateien und Zielverzeichnis auswählen."));
            return;
        }
        BatchOptions o;
        for (int i = 0; i < files->count(); ++i)
            o.inputs << files->item(i)->text();
        o.outputDirectory = output->text();
        o.format = format->currentData().toString().toLatin1();
        o.namePattern = pattern->text();
        o.renameOnly = mode->currentIndex() == 1;
        o.allowLoss = loss->isChecked();
        o.quality = quality->value();
        o.targetBytes = targetKb->value() > 0 ? qint64(targetKb->value()) * 1024 : 0;
        if (width->value() && height->value())
            o.resizeTo = {width->value(), height->value()};
        cancellation->store(false);
        running = true;
        start->setText(IX_TEXT("Abbrechen"));
        close->setEnabled(false);
        progress->setRange(0, 0);
        log->clear();
        watcher.setFuture(
            QtConcurrent::run([o, cancellation] { return BatchProcessor::run(o, *cancellation); }));
    });
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &d, [&] {
        for (const auto &e : watcher.future().takeResult()) {
            changes.add(e);
            const auto detail = !e.error.isEmpty() ? localized(e.error) : localized(e.warning);
            log->appendPlainText((e.error.isEmpty() ? QStringLiteral("OK: ") : IX_TEXT("FEHLER: ")) +
                                 e.input + QStringLiteral(" → ") + e.output +
                                 (detail.isEmpty() ? QString() : QStringLiteral("\n  ") + detail));
        }
        running = false;
        progress->setRange(0, 1);
        progress->setValue(1);
        start->setText(IX_TEXT("Starten"));
        close->setEnabled(true);
    });
    QObject::connect(controls, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    QObject::connect(&d, &QDialog::finished, &d, [&] { cancellation->store(true); });
    d.exec();
    cancellation->store(true);
    watcher.waitForFinished();
    if (running)
        for (const auto &entry : watcher.future().takeResult()) changes.add(entry);
    return changes;
}
bool settingsDialog(QWidget *p) {
    SettingsDialog dialog(p);
    return dialog.exec() == QDialog::Accepted;
}
} // namespace ix
