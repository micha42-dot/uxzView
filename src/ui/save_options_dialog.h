// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Companion window next to the Save As dialog: compact per-format compression
// options with quality slider, target file size, metadata choices and named
// profiles. Non-modal, parented to the native save dialog while a save is in
// flight. The main window owns one instance across save sessions: building the
// ~20 widgets costs ~100 ms, so re-creating it per Save As is what felt slow.
// The native QFileDialog itself stays per-save (a reused one clobbers the
// typed file name through its asynchronous directory model).
#include "core/codec_registry.h"
#include "core/save_options.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QList>
#include <QPointer>
#include <atomic>
#include <memory>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTimer;
namespace ix {
struct ImageDocument;
class SaveOptionsDialog final : public QDialog {
    Q_OBJECT
  public:
    SaveOptionsDialog(const CodecRegistry &registry, QWidget *owner);
    SaveOptions options() const { return options_; }
    void setFormat(const QByteArray &format);
    // One session = one Save As: hand over the current document/options/profiles
    // and the native chooser. Everything (values, profile list, visibility and
    // the fixed size) is applied with updates frozen, so the first paint is
    // already complete. Parents the window to the chooser (top-most + usable
    // while its modal loop runs) and docks it beside the native frame.
    void prepare(const ImageDocument &document, SaveOptions options,
                 const QJsonObject &profiles, QDialog &chooser);
    // Ends the session: hide, cancel workers, clear the stale auto result and
    // give the window back to its owner so the next save can reuse it.
    void endSession();
    void preparePlacement(QWidget *nativeDialog, const QPoint &remembered);
  signals:
    void optionsChanged(const SaveOptions &options);
    // Emitted when the user drags the companion (not on programmatic move).
    void movedByUser(const QPoint &position);

  protected:
    void closeEvent(QCloseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    // Esc must close both windows, not just the companion: forward rejection
    // to the native save dialog (its own Esc already hides us, see below).
    void reject() override;

  public:
    ~SaveOptionsDialog() override;

  private:
    QWidget *const owner_;
    const CodecRegistry registry_;
    QPointer<QDialog> chooser_;
    ImageDocument document_;
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
    QCheckBox *targetEnabled_ = nullptr;
    QDoubleSpinBox *targetValue_ = nullptr;
    QComboBox *targetUnit_ = nullptr;
    QLabel *targetResult_ = nullptr;
    QProgressBar *targetProgress_ = nullptr;
    QPushButton *targetCancel_ = nullptr;
    QCheckBox *keepExif_ = nullptr, *keepIptc_ = nullptr, *keepXmp_ = nullptr, *keepIcc_ = nullptr,
              *fixOrientation_ = nullptr;
    QRadioButton *alphaWhite_ = nullptr, *alphaKeep_ = nullptr, *alphaAsk_ = nullptr;
    QWidget *alphaBox_ = nullptr;
    QTimer *optimizeTimer_ = nullptr;
    // Target-size workers outlive UI events: every watcher/timer pair is
    // tracked here so the destructor can cancel + disconnect before Qt
    // tears down children (a finished lambda must never touch a dead this).
    QList<QPointer<QFutureWatcher<TargetSizeResult>>> targetWatchers_;
    QList<QPointer<QTimer>> targetTimers_;
    QList<std::shared_ptr<std::atomic_bool>> targetCancels_;
    std::shared_ptr<std::atomic_bool> activeCancel_; // Set by targetCancel_.
    QSpinBox *pngLevel_ = nullptr;
    QComboBox *profileCombo_ = nullptr;
    QLabel *effortCaption_ = nullptr;
    // captions_ holds (caption, row) pairs so rebuild() can hide both when the
    // format does not provide the feature — stacked rows can never overlap.
    // A null caption marks a bare row (lossless_); only the row is toggled.
    QList<QPair<QLabel *, QWidget *>> captions_;
    QWidget *qualityRow_ = nullptr, *effortRow_ = nullptr, *pngRow_ = nullptr, *jpegRow_ = nullptr,
            *gifRow_ = nullptr, *targetRow_ = nullptr, *metaBox_ = nullptr;
    bool rebuilding_ = false;
    bool autoPlaced_ = false; // True only inside a programmatic placement move.
    // Set by setFormat(); consumed on first show so no encode blocks paint.
    bool pendingAutoOptimize_ = false;
    void applyOptions(SaveOptions options);
    // setFixedSize() release/freeze pair. releaseFixedSize() must use
    // setMinimumSize(0,0)/setMaximumSize(QWIDGETSIZE_MAX,...) — passing
    // QWIDGETSIZE_MAX as a minimum installs a ~16777 px floor.
    void releaseFixedSize();
    void freezeSizeToLayout();
    void rebuild();
    void collect();
    void runTargetOptimization();
    // Stops every live target-size worker (flags + watchers + timers) so no
    // stale finished handler can publish into the next session.
    void cancelTargetOptimization();
    void cancelTargetWorkers();
    void loadProfile(int index);
    void saveProfile();
    void deleteProfile();
    void refreshProfileCombo();
};
} // namespace ix
