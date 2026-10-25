// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "core/rotation.h"
#include <QDialog>
class QDoubleSpinBox;
class QCheckBox;
class QSpinBox;
class QPushButton;
namespace ix {
class RotationPreview;
class RotationDialog : public QDialog {
    Q_OBJECT
  public:
    explicit RotationDialog(const QImage &image, QWidget *parent = nullptr);
    RotationOptions options() const;

  private:
    QImage thumbnail_;
    RotationPreview *original_, *result_;
    QDoubleSpinBox *angle_;
    QCheckBox *canvas_, *background_, *fast_, *antialias_, *select_, *sharpen_;
    QSpinBox *amount_;
    QPushButton *color_;
    QColor backgroundColor_ = Qt::black;
    void updatePreview();
};
} // namespace ix
