// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QDialog>
#include <QMap>
class QCheckBox;
class QSpinBox;
class QComboBox;
class QDoubleSpinBox;
class QKeySequenceEdit;
class QListWidget;
class QStackedWidget;
namespace ix {
class FullscreenInfoEditor;
class CropRatioEditor;
class SettingsDialog : public QDialog {
    Q_OBJECT
  public:
    explicit SettingsDialog(QWidget *parent = nullptr, QString storageHome = {});
    void selectPage(const QString &objectName);

  protected:
    void accept() override;

  private:
    QMap<QString, QCheckBox *> checks_;
    QMap<QString, QSpinBox *> integers_;
    QComboBox *fit_, *sort_, *iconSize_, *language_, *previewBackground_, *theme_;
    QListWidget *categories_;
    QStackedWidget *pages_;
    QKeySequenceEdit *captureShortcut_;
    QDoubleSpinBox *seconds_;
    CropRatioEditor *cropRatios_;
    QCheckBox *portable_;
    FullscreenInfoEditor *fullscreenInfo_;
    QString storageHome_;
    QColor background_, fullscreenBackground_;
};
} // namespace ix
