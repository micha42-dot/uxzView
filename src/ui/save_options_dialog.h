// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Companion window next to the Save As dialog: compact per-format compression
// options with quality slider, target file size, metadata choices and named
// profiles. Non-modal; dies with the save dialog.
#include "core/save_options.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
namespace ix {
class CodecRegistry;
struct ImageDocument;
class SaveOptionsDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit SaveOptionsDialog(const ImageDocument &document, QWidget *parent = nullptr);
    SaveOptions options() const { return options_; }
    void setFormat(const QByteArray &format);
    void setRegistry(const CodecRegistry *registry);
    void setProfiles(const QJsonObject &profiles);
  signals:
    void optionsChanged(const SaveOptions &options);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    const ImageDocument &document_;
    const CodecRegistry *registry_ = nullptr;
    SaveOptions options_;
    QJsonObject profiles_;
    SaveCapabilities caps_;
    QLabel *title_ = nullptr;
    QSlider *qualitySlider_ = nullptr;
    QSpinBox *qualitySpin_ = nullptr;
    QCheckBox *progressive_ = nullptr, *subsampling_ = nullptr, *interlaced_ = nullptr;
    QCheckBox *lossless_ = nullptr;
    QSlider *effortSlider_ = nullptr;
    QSpinBox *effortSpin_ = nullptr;
    QLabel *effortLabel_ = nullptr;
    QCheckBox *targetEnabled_ = nullptr;
    QDoubleSpinBox *targetValue_ = nullptr;
    QComboBox *targetUnit_ = nullptr;
    QLabel *targetResult_ = nullptr;
    QProgressBar *targetProgress_ = nullptr;
    QPushButton *targetCancel_ = nullptr;
    QCheckBox *keepExif_ = nullptr, *keepIptc_ = nullptr, *keepXmp_ = nullptr, *keepIcc_ = nullptr,
              *fixOrientation_ = nullptr;
    QSpinBox *pngLevel_ = nullptr;
    QComboBox *profileCombo_ = nullptr;
    QWidget *qualityRow_ = nullptr, *effortRow_ = nullptr, *pngRow_ = nullptr, *jpegRow_ = nullptr,
            *gifRow_ = nullptr, *targetRow_ = nullptr, *metaBox_ = nullptr;
    bool rebuilding_ = false;
    void rebuild();
    void collect();
    void runTargetOptimization();
    void loadProfile(int index);
    void saveProfile();
    void deleteProfile();
    void refreshProfileCombo();
};
} // namespace ix
