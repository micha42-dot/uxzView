// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "core/processing.h"
#include "core/resize_model.h"
#include <QDialog>
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QGroupBox;
namespace ix {
class ResizeDialog : public QDialog {
    Q_OBJECT
  public:
    ResizeDialog(QSize original, double dpi, QWidget *parent = nullptr);
    ResizeDialog(const QImage &original, double dpi, QWidget *parent = nullptr);
    ResizeOptions options() const;

  private:
    ResizeModel model_;
    QSize sourceSize_;
    QImage::Format sourceFormat_ = QImage::Format_ARGB32;
    qint64 sourceBytes_ = 0;
    bool sourceMapped_ = false;
    QDoubleSpinBox *width_, *height_, *percentW_, *percentH_, *mp_, *dpi_;
    QCheckBox *proportional_, *sharpen_, *adaptDpi_, *fast_, *gamma_;
    QComboBox *filter_, *custom_;
    QLabel *result_;
    QRadioButton *absolute_, *percent_, *megapixel_;
    QGroupBox *absoluteGroup_, *percentGroup_, *mpGroup_;
    bool syncing_ = false;
    void sync(QDoubleSpinBox *editing = nullptr);
    void updateMode();
    void updateEstimate();
    void accept() override;
};
} // namespace ix
