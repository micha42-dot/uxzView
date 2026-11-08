// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "core/codec_registry.h"
#include "core/batch_processor.h"
#include "core/image_document.h"
#include "core/processing.h"
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
namespace ix {
class ColorDialog : public QDialog {
    Q_OBJECT
  public:
    ColorDialog(const QImage &image, QWidget *parent = nullptr);
    ColorAdjust values() const;

  public:
    void setImage(const QImage &image);
  signals:
    void applyRequested(ix::ColorAdjust);

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    QList<QDoubleSpinBox *> inputs_;
    QImage original_;
    QLabel *preview_, *originalLabel_;
    void updatePreview();
};
bool newImageDialog(QWidget *parent, ImageDocument &output);
bool selectionDialog(QWidget *parent, QSize imageSize, QRect &selection);
// Single text overlay: one dialog for content, font, size, style, color and
// alignment with a live preview. The preview and the final application share
// renderTextOverlay, so the preview cannot diverge from the result.
struct TextOverlay {
    QString text;
    QString fontFamily = QStringLiteral("Arial");
    int pointSize = 24;
    bool bold = false, italic = false, underline = false;
    QColor color = Qt::white;
    Qt::Alignment alignment = Qt::AlignLeft;
};
QImage renderTextOverlay(const QImage &source, const TextOverlay &overlay, const QRect &area);
bool textOverlayDialog(QWidget *parent, const QImage &previewSource, const QRect &area,
                       TextOverlay &overlay);
bool canvasDialog(QWidget *parent, int &left, int &right, int &top, int &bottom, QColor &color);
void imageInformation(QWidget *parent, const ImageDocument &document);
void histogramDialog(QWidget *parent, const QImage &image);
void colorPickerDialog(QWidget *parent, const QImage &image, QPoint position);
void formatDialog(QWidget *parent, const CodecRegistry &registry);
BatchFileChanges batchDialog(QWidget *parent, const QStringList &initial);
bool settingsDialog(QWidget *parent);
} // namespace ix
