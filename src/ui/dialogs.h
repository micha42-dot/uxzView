// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "core/codec_registry.h"
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
bool canvasDialog(QWidget *parent, int &left, int &right, int &top, int &bottom, QColor &color);
void imageInformation(QWidget *parent, const ImageDocument &document);
void histogramDialog(QWidget *parent, const QImage &image);
void formatDialog(QWidget *parent, const CodecRegistry &registry);
void batchDialog(QWidget *parent, const QStringList &initial, const QString &journalRoot = {});
bool settingsDialog(QWidget *parent);
void thumbnailsDialog(QWidget *parent, const QStringList &files, const std::function<void(QString)> &open);
} // namespace ix
