// SPDX-License-Identifier: GPL-3.0-or-later
#include "save_options_dialog.h"
#include "core/codec_registry.h"
#include "core/image_document.h"
#include "core/localization.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMoveEvent>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <atomic>
#include <memory>
#include <utility>
namespace ix {
namespace {
// Intended width band: compact like IrfanView, but wide enough for the stacked
// caption rows. Kept in one place because the fixed-size release path must
// restore exactly this band (QWIDGETSIZE_MAX is NOT a valid "release" value for
// a minimum size — it would set a 16777215 px floor and silently widen the
// dialog on every format change).
constexpr int kCompanionMinWidth = 210;
constexpr int kCompanionMaxWidth = 260;
} // namespace
SaveOptionsDialog::SaveOptionsDialog(const CodecRegistry &registry, QWidget *owner)
    : QDialog(owner), owner_(owner), registry_(registry) {
    setAttribute(Qt::WA_DeleteOnClose, false);
    // Side-by-side companion WITHOUT any system buttons: Qt::CustomizeWindowHint
    // strips the native title-bar buttons, so the ugly hover-red X is gone for
    // good. An in-dialog bold header replaces the title bar text. Never modal.
    // NOTE: no WindowStaysOnTopHint anymore — it pulled the companion above
    // every unrelated window (file picker of other apps, taskbar popups).
    // Top-most relative to the native save dialog comes from parenting.
    setWindowFlags(Qt::Tool | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    setWindowModality(Qt::NonModal);
    auto *layout = new QVBoxLayout(this);
    // Compact: ~25% narrower than before.
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(3);
    setMinimumWidth(210);
    setMaximumWidth(260);
    // Explicit header inside the dialog (there is no functional system title
    // bar anymore): format name placeholder, replaced by rebuild().
    title_ = new QLabel(this);
    QFont titleFont = title_->font();
    titleFont.setBold(true);
    title_->setFont(titleFont);
    title_->setObjectName(QStringLiteral("compressionTitle"));
    layout->addWidget(title_);
    // Single column only — no QFormLayout label column. Every row is a full
    // width widget with its own inline caption. Two-column forms caused the
    // label/control overlap when rows were hidden per format.
    auto *rows = new QWidget(this);
    auto *rowsLayout = new QVBoxLayout(rows);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(4);
    layout->addWidget(rows);
    qualityRow_ = new QWidget(this);
    auto *qualityLayout = new QHBoxLayout(qualityRow_);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    auto *low = new QLabel(IX_TEXT("Niedrig"), qualityRow_);
    qualitySlider_ = new QSlider(Qt::Horizontal, qualityRow_);
    qualitySlider_->setRange(0, 100);
    qualitySlider_->setFixedWidth(100);
    qualitySlider_->setObjectName(QStringLiteral("compressionQuality"));
    qualitySpin_ = new QSpinBox(qualityRow_);
    qualitySpin_->setRange(0, 100);
    qualitySpin_->setObjectName(QStringLiteral("compressionQualityValue"));
    qualitySpin_->setMinimumWidth(48);
    qualitySpin_->setMaximumWidth(56);
    qualityLayout->addWidget(low);
    qualityLayout->addWidget(qualitySlider_, 1);
    qualityLayout->addWidget(qualitySpin_);
    // Stacked captions instead of a two-column label column (no overlap).
    auto *qualityCaption = new QLabel(IX_TEXT("Qualität:"), this);
    rowsLayout->addWidget(qualityCaption);
    rowsLayout->addWidget(qualityRow_);
    connect(qualitySlider_, &QSlider::valueChanged, this, [this](int value) {
        if (qualitySpin_->value() != value)
            qualitySpin_->setValue(value);
        collect();
    });
    connect(qualitySpin_, &QSpinBox::valueChanged, this, [this](int value) {
        if (qualitySlider_->value() != value)
            qualitySlider_->setValue(value);
        else
            collect();
    });
    jpegRow_ = new QWidget(this);
    auto *jpegLayout = new QVBoxLayout(jpegRow_);
    jpegLayout->setContentsMargins(0, 0, 0, 0);
    jpegLayout->setSpacing(2);
    progressive_ = new QCheckBox(IX_TEXT("Als progressives JPEG speichern"), jpegRow_);
    progressive_->setObjectName(QStringLiteral("compressionProgressive"));
    subsampling_ = new QCheckBox(IX_TEXT("Chroma-Subsampling deaktivieren"), jpegRow_);
    subsampling_->setObjectName(QStringLiteral("compressionNoSubsampling"));
    jpegLayout->addWidget(progressive_);
    jpegLayout->addWidget(subsampling_);
    auto *jpegCaption = new QLabel(IX_TEXT("JPEG:"), this);
    rowsLayout->addWidget(jpegCaption);
    rowsLayout->addWidget(jpegRow_);
    connect(progressive_, &QCheckBox::toggled, this, [this] { collect(); });
    connect(subsampling_, &QCheckBox::toggled, this, [this] { collect(); });
    lossless_ = new QCheckBox(IX_TEXT("Verlustfrei speichern"), this);
    lossless_->setObjectName(QStringLiteral("compressionLossless"));
    rowsLayout->addWidget(lossless_);
    connect(lossless_, &QCheckBox::toggled, this, [this] { collect(); });
    effortRow_ = new QWidget(this);
    auto *effortLayout = new QHBoxLayout(effortRow_);
    effortLayout->setContentsMargins(0, 0, 0, 0);
    effortSlider_ = new QSlider(Qt::Horizontal, effortRow_);
    effortSlider_->setFixedWidth(90);
    effortSlider_->setObjectName(QStringLiteral("compressionEffort"));
    effortSpin_ = new QSpinBox(effortRow_);
    effortSpin_->setObjectName(QStringLiteral("compressionEffortValue"));
    effortSpin_->setMinimumWidth(48);
    effortSpin_->setMaximumWidth(56);
    effortLayout->addWidget(effortSlider_, 1);
    effortLayout->addWidget(effortSpin_);
    // Stacked caption above the row (single column, never overlapping).
    effortCaption_ = new QLabel(this);
    rowsLayout->addWidget(effortCaption_);
    rowsLayout->addWidget(effortRow_);
    connect(effortSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (effortSpin_->value() != value)
            effortSpin_->setValue(value);
        collect();
    });
    connect(effortSpin_, &QSpinBox::valueChanged, this, [this](int value) {
        if (effortSlider_->value() != value)
            effortSlider_->setValue(value);
        else
            collect();
    });
    pngRow_ = new QWidget(this);
    auto *pngLayout = new QHBoxLayout(pngRow_);
    pngLayout->setContentsMargins(0, 0, 0, 0);
    pngLevel_ = new QSpinBox(pngRow_);
    pngLevel_->setRange(0, 9);
    pngLevel_->setObjectName(QStringLiteral("compressionPngLevel"));
    pngLayout->addWidget(new QLabel(IX_TEXT("Stufe (0–9, verlustfrei):"), pngRow_));
    pngLayout->addWidget(pngLevel_, 1);
    auto *pngCaption = new QLabel(IX_TEXT("PNG (verlustfrei):"), this);
    rowsLayout->addWidget(pngCaption);
    rowsLayout->addWidget(pngRow_);
    connect(pngLevel_, &QSpinBox::valueChanged, this, [this] { collect(); });
    gifRow_ = new QWidget(this);
    auto *gifLayout = new QVBoxLayout(gifRow_);
    gifLayout->setContentsMargins(0, 0, 0, 0);
    interlaced_ = new QCheckBox(IX_TEXT("Interlaced speichern"), gifRow_);
    interlaced_->setObjectName(QStringLiteral("compressionInterlaced"));
    gifLayout->addWidget(interlaced_);
    auto *gifCaption = new QLabel(IX_TEXT("GIF:"), this);
    rowsLayout->addWidget(gifCaption);
    rowsLayout->addWidget(gifRow_);
    connect(interlaced_, &QCheckBox::toggled, this, [this] { collect(); });
    // Transparency handling for formats without alpha (screenshot: Farbe des
    // Hauptfensters / Transparenter Farbwert / Vorschau): White flattens
    // onto white, Keep preserves the pixels, Ask resolves per save.
    alphaBox_ = new QWidget(this);
    auto *alphaLayout = new QVBoxLayout(alphaBox_);
    alphaLayout->setContentsMargins(0, 0, 0, 0);
    alphaLayout->setSpacing(2);
    alphaWhite_ = new QRadioButton(IX_TEXT("Auf Weiß reduzieren"), alphaBox_);
    alphaWhite_->setObjectName(QStringLiteral("compressionAlphaWhite"));
    alphaKeep_ = new QRadioButton(IX_TEXT("Pixel behalten"), alphaBox_);
    alphaKeep_->setObjectName(QStringLiteral("compressionAlphaKeep"));
    alphaAsk_ = new QRadioButton(IX_TEXT("Bei jedem Speichern fragen"), alphaBox_);
    alphaAsk_->setObjectName(QStringLiteral("compressionAlphaAsk"));
    alphaLayout->addWidget(alphaWhite_);
    alphaLayout->addWidget(alphaKeep_);
    alphaLayout->addWidget(alphaAsk_);
    auto *alphaCaption = new QLabel(IX_TEXT("Transparenz:"), this);
    rowsLayout->addWidget(alphaCaption);
    rowsLayout->addWidget(alphaBox_);
    for (auto *button : {alphaWhite_, alphaKeep_, alphaAsk_})
        connect(button, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked)
                collect();
        });
    metaBox_ = new QWidget(this);
    auto *metaLayout = new QVBoxLayout(metaBox_);
    metaLayout->setContentsMargins(0, 0, 0, 0);
    metaLayout->setSpacing(2);
    keepExif_ = new QCheckBox(IX_TEXT("EXIF-Metadaten erhalten"), metaBox_);
    keepExif_->setObjectName(QStringLiteral("compressionKeepExif"));
    keepIptc_ = new QCheckBox(IX_TEXT("IPTC-Metadaten erhalten"), metaBox_);
    keepIptc_->setObjectName(QStringLiteral("compressionKeepIptc"));
    keepXmp_ = new QCheckBox(IX_TEXT("XMP-Metadaten erhalten"), metaBox_);
    keepXmp_->setObjectName(QStringLiteral("compressionKeepXmp"));
    keepIcc_ = new QCheckBox(IX_TEXT("ICC-Farbprofil erhalten"), metaBox_);
    keepIcc_->setObjectName(QStringLiteral("compressionKeepIcc"));
    fixOrientation_ = new QCheckBox(IX_TEXT("EXIF-Ausrichtung korrigieren"), metaBox_);
    fixOrientation_->setObjectName(QStringLiteral("compressionFixOrientation"));
    // No encoder honors this yet (orientation is already baked into pixels on
    // decode; a future tag-normalization step will use the flag). Show it
    // disabled rather than silently ignoring a user click.
    fixOrientation_->setEnabled(false);
    fixOrientation_->setToolTip(IX_TEXT("Derzeit ohne Wirkung — die Ausrichtung ist bereits in den Pixeln enthalten."));
    metaLayout->addWidget(keepExif_);
    metaLayout->addWidget(keepIptc_);
    metaLayout->addWidget(keepXmp_);
    metaLayout->addWidget(keepIcc_);
    metaLayout->addWidget(fixOrientation_);
    auto *metaCaption = new QLabel(IX_TEXT("Metadaten:"), this);
    rowsLayout->addWidget(metaCaption);
    rowsLayout->addWidget(metaBox_);
    for (auto *box : {keepExif_, keepIptc_, keepXmp_, keepIcc_, fixOrientation_})
        connect(box, &QCheckBox::toggled, this, [this] { collect(); });
    targetRow_ = new QWidget(this);
    auto *targetLayout = new QVBoxLayout(targetRow_);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    auto *targetTop = new QWidget(this);
    auto *targetTopLayout = new QHBoxLayout(targetTop);
    targetTopLayout->setContentsMargins(0, 0, 0, 0);
    targetEnabled_ = new QCheckBox(IX_TEXT("Zieldateigröße:"), this);
    targetEnabled_->setObjectName(QStringLiteral("compressionTargetEnabled"));
    targetValue_ = new QDoubleSpinBox(this);
    targetValue_->setRange(1.0, 2048.0);
    targetValue_->setDecimals(2);
    targetValue_->setValue(150.0);
    targetValue_->setObjectName(QStringLiteral("compressionTargetValue"));
    targetValue_->setMinimumWidth(64);
    targetValue_->setMaximumWidth(80);
    targetUnit_ = new QComboBox(this);
    targetUnit_->addItem(QStringLiteral("KB"), 1024);
    targetUnit_->addItem(QStringLiteral("MB"), 1024 * 1024);
    targetUnit_->setObjectName(QStringLiteral("compressionTargetUnit"));
    targetUnit_->setMaximumWidth(56);
    targetTopLayout->addWidget(targetEnabled_);
    targetTopLayout->addWidget(targetValue_, 1);
    targetTopLayout->addWidget(targetUnit_);
    targetLayout->addWidget(targetTop);
    targetResult_ = new QLabel(this);
    targetResult_->setWordWrap(true);
    targetResult_->setObjectName(QStringLiteral("compressionTargetResult"));
    targetLayout->addWidget(targetResult_);
    auto *targetBar = new QWidget(this);
    auto *targetBarLayout = new QHBoxLayout(targetBar);
    targetBarLayout->setContentsMargins(0, 0, 0, 0);
    targetProgress_ = new QProgressBar(this);
    targetProgress_->setRange(0, 10);
    targetProgress_->setValue(0);
    targetProgress_->setVisible(false);
    targetCancel_ = new QPushButton(IX_TEXT("Abbrechen"), this);
    targetCancel_->setVisible(false);
    connect(targetCancel_, &QPushButton::clicked, this, [this] {
        if (activeCancel_)
            activeCancel_->store(true);
    });
    targetBarLayout->addWidget(targetProgress_, 1);
    targetBarLayout->addWidget(targetCancel_);
    targetLayout->addWidget(targetBar);
    auto *targetCaption = new QLabel(IX_TEXT("Ziel:"), this);
    rowsLayout->addWidget(targetCaption);
    rowsLayout->addWidget(targetRow_);
    optimizeTimer_ = new QTimer(this);
    optimizeTimer_->setSingleShot(true);
    optimizeTimer_->setInterval(600);
    connect(optimizeTimer_, &QTimer::timeout, this, [this] {
        if (targetEnabled_->isChecked())
            runTargetOptimization();
    });
    connect(targetEnabled_, &QCheckBox::toggled, this, [this](bool checked) {
        collect();
        optimizeTimer_->stop();
        if (checked)
            runTargetOptimization();
    });
    connect(targetValue_, &QDoubleSpinBox::valueChanged, this, [this] {
        collect();
        // Debounced: typing a size must not start an optimization per keystroke.
        optimizeTimer_->stop();
        if (targetEnabled_->isChecked())
            optimizeTimer_->start();
    });
    connect(targetUnit_, &QComboBox::currentIndexChanged, this, [this] {
        collect();
        optimizeTimer_->stop();
        if (targetEnabled_->isChecked())
            optimizeTimer_->start();
    });
    auto *profileBox = new QWidget(this);
    auto *profileBoxLayout = new QVBoxLayout(profileBox);
    profileBoxLayout->setContentsMargins(0, 0, 0, 0);
    profileBoxLayout->setSpacing(2);
    profileBoxLayout->addWidget(new QLabel(IX_TEXT("Profile:"), profileBox));
    auto *profileRow = new QWidget(profileBox);
    auto *profileLayout = new QHBoxLayout(profileRow);
    profileLayout->setContentsMargins(0, 0, 0, 0);
    profileLayout->setSpacing(4);
    profileCombo_ = new QComboBox(profileRow);
    profileCombo_->setObjectName(QStringLiteral("compressionProfile"));
    profileCombo_->setMinimumWidth(80);
    auto *loadButton = new QPushButton(IX_TEXT("Laden"), profileRow);
    auto *deleteButton = new QPushButton(IX_TEXT("Löschen"), profileRow);
    auto *saveButton = new QPushButton(IX_TEXT("Speichern"), profileRow);
    loadButton->setMaximumWidth(62);
    deleteButton->setMaximumWidth(62);
    saveButton->setMaximumWidth(74);
    profileLayout->addWidget(profileCombo_, 1);
    profileLayout->addWidget(loadButton);
    profileLayout->addWidget(deleteButton);
    profileLayout->addWidget(saveButton);
    profileBoxLayout->addWidget(profileRow);
    rowsLayout->addWidget(profileBox);
    // captions_ holds (caption, row) pairs so rebuild() can hide both when the
    // format does not provide the feature — stacked rows can never overlap.
    captions_.append({qualityCaption, qualityRow_});
    captions_.append({jpegCaption, jpegRow_});
    captions_.append({pngCaption, pngRow_});
    captions_.append({gifCaption, gifRow_});
    captions_.append({alphaCaption, alphaBox_});
    captions_.append({metaCaption, metaBox_});
    captions_.append({effortCaption_, effortRow_});
    captions_.append({targetCaption, targetRow_});
    // lossless_ is a bare row without a stacked caption: track it with a null
    // caption so the ctor-hide loop and rebuild() treat it like every row.
    captions_.append({nullptr, lossless_});
    connect(loadButton, &QPushButton::clicked, this, [this] { loadProfile(profileCombo_->currentIndex()); });
    connect(deleteButton, &QPushButton::clicked, this, [this] { deleteProfile(); });
    connect(saveButton, &QPushButton::clicked, this, [this] { saveProfile(); });
    // Start hidden: only setFormat()/rebuild() decides visibility per format.
    // Leaving the ctor-visible default otherwise paints every row for one
    // frame (the "empty dialog, then rows pop in" flicker). Bare rows carry
    // a null caption and hide the row only.
    for (const auto &pair : std::as_const(captions_)) {
        pair.second->hide();
        if (pair.first)
            pair.first->hide();
    }
}
void SaveOptionsDialog::setFormat(const QByteArray &format) {
    // One atomic format swap; applyOptions() cancels stale workers first so no
    // late result can publish into the new format.
    releaseFixedSize();
    applyOptions(loadSaveOptions(format));
    freezeSizeToLayout();
    // Defer the IrfanView-style auto-optimization until the dialog is
    // actually visible: starting full-res encodes here blocked first paint.
    pendingAutoOptimize_ = options_.targetSizeEnabled && caps_.targetSize;
}

void SaveOptionsDialog::releaseFixedSize() {
    // Undo setFixedSize() with the documented Qt idiom. setMinimumSize(
    // QWIDGETSIZE_MAX, ...) is WRONG: it installs a ~16777 px minimum that
    // survives into the next layout pass and silently widens the dialog.
    setMinimumSize(kCompanionMinWidth, 0);
    setMaximumSize(kCompanionMaxWidth, QWIDGETSIZE_MAX);
}

void SaveOptionsDialog::freezeSizeToLayout() {
    // One size from the fully populated layout, then frozen. Called exactly
    // once per session (prepare) and once per format/profile switch — never
    // per rebuild(), so no visible re-layout pass happens after show().
    adjustSize();
    setFixedSize(size());
}

void SaveOptionsDialog::prepare(const ImageDocument &document, SaveOptions options,
                                const QJsonObject &profiles, QDialog &chooser) {
    // Single-shot, fully-populated session start: everything (values, profile
    // list, visibility, fixed size) is applied with updates frozen, before the
    // window is shown. The old path painted empty/half-built frames first —
    // that staged loading is exactly what felt slow.
    Q_ASSERT(owner_);
    if (parentWidget() != &chooser)
        setParent(&chooser, windowFlags());
    chooser_ = &chooser;
    document_ = document;
    profiles_ = profiles;
    refreshProfileCombo();
    setUpdatesEnabled(false);
    const auto unfreeze = qScopeGuard([this] { setUpdatesEnabled(true); });
    applyOptions(options);
    // Same IrfanView-style deferral as setFormat(): the target-size optimizer
    // must not encode before first paint. prepare() bypasses setFormat(), so
    // the flag has to be armed here too — otherwise the auto-optimization
    // never starts on the first open.
    pendingAutoOptimize_ = options_.targetSizeEnabled && caps_.targetSize;
    // One fixed size from the fully populated layout; no further adjustSize()
    // passes while the dialog is visible (each one repaints empty first).
    freezeSizeToLayout();
}

void SaveOptionsDialog::endSession() {
    // A finished/cancelled save must not leave a stale auto result or a live
    // worker behind: the next session starts from neutral state.
    cancelTargetWorkers();
    hide();
    if (parentWidget() != owner_)
        setParent(owner_, windowFlags());
    chooser_ = nullptr;
    document_ = ImageDocument();
    targetResult_->clear();
    targetProgress_->hide();
    targetCancel_->hide();
    options_.autoQuality = -1;
    pendingAutoOptimize_ = false;
    optimizeTimer_->stop();
}

void SaveOptionsDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    // Consume the pending auto-optimization even after the first show: a
    // re-show (e.g. save -> cancel -> save again on the same instance) must
    // not replay a stale pending flag into a worker storm.
    if (pendingAutoOptimize_) {
        pendingAutoOptimize_ = false;
        if (isVisible())
            runTargetOptimization();
    }
}

void SaveOptionsDialog::preparePlacement(QWidget *nativeDialog, const QPoint &remembered) {
    // IrfanView shows its JPEG options window in the same frame as the save
    // dialog: no polling for the native frame (it is not mapped yet when the
    // modal loop has not started), no post-show dock move, no staged paint.
    // The position is therefore derived from the geometry we DO have: the main
    // window. The native save dialog opens centred on its parent, so its right
    // edge is predictable: parent centre + half a typical dialog width.
    if (!remembered.isNull()) {
        // The user's own placement wins.
        autoPlaced_ = true;
        move(remembered);
        autoPlaced_ = false;
        return;
    }
    adjustSize();
    const QRect screen = this->screen() ? this->screen()->availableGeometry() : QRect();
    // Native save dialogs are ~ 0.9 * screen width but never wider than 1100 px;
    // the companion sits right of its right edge, or left of it when tight.
    const QRect parentFrame = parentWidget() ? parentWidget()->frameGeometry() : frameGeometry();
    const int dialogWidth = qMin(1100, qMax(640, int(screen.width() * 0.72)));
    const int dialogHeight = qMin(760, qMax(420, int(screen.height() * 0.72)));
    QPoint dialogTopLeft = parentFrame.center() - QPoint(dialogWidth / 2, dialogHeight / 2);
    if (screen.isValid()) {
        dialogTopLeft.setX(qBound(screen.left(), dialogTopLeft.x(),
                                  qMax(screen.left(), screen.right() - dialogWidth)));
        dialogTopLeft.setY(qBound(screen.top(), dialogTopLeft.y(),
                                  qMax(screen.top(), screen.bottom() - dialogHeight)));
    }
    const int dialogRight = dialogTopLeft.x() + dialogWidth;
    QPoint pos(dialogRight + 8, dialogTopLeft.y());
    if (screen.isValid() && pos.x() + width() > screen.right())
        pos.setX(qMax(screen.left(), dialogTopLeft.x() - width() - 8));
    if (screen.isValid())
        pos.setY(qBound(screen.top(), pos.y(), qMax(screen.top(), screen.bottom() - height())));
    // Programmatic placement: moveEvent must not report it as a user drag
    // (would overwrite the remembered position with the dock position).
    autoPlaced_ = true;
    move(pos);
    autoPlaced_ = false;
    Q_UNUSED(nativeDialog);
}

void SaveOptionsDialog::applyOptions(SaveOptions options) {
    // One atomic option swap: cancel, then apply values and visibility with all
    // signals blocked, so nothing persists or optimizes half-way.
    cancelTargetOptimization();
    options_ = std::move(options);
    caps_ = saveCapabilities(options_.format);
    rebuild();
    // Target-size widgets are decided from the options alone, NOT from
    // isVisible(): prepare() runs while the dialog is still hidden, so an
    // isVisible() guard left them hidden and showEvent() showed them
    // afterwards — that second visibility change was a second layout+paint
    // pass, i.e. the visible "window first, controls later" staging.
    const bool wantsTarget = options_.targetSizeEnabled && caps_.targetSize;
    targetProgress_->setVisible(wantsTarget);
    targetCancel_->setVisible(wantsTarget);
    if (!wantsTarget)
        targetResult_->clear();
    // Only start encodes once the window is actually on screen; the flag is
    // consumed by showEvent() for the very first open.
    if (wantsTarget && isVisible())
        runTargetOptimization();
}

void SaveOptionsDialog::rebuild() {
    // Block every widget signal while the controls are populated: an early
    // collect() or toggled()/valueChanged() handler would otherwise read a
    // half-built dialog, persist corrupt state, or kick off a target-size
    // optimization before first paint (the old first-open stall).
    rebuilding_ = true;
    const auto done = qScopeGuard([this] { rebuilding_ = false; });
    const QSignalBlocker blockQuality(qualitySlider_);
    const QSignalBlocker blockSpin(qualitySpin_);
    const QSignalBlocker blockProgressive(progressive_);
    const QSignalBlocker blockSubsampling(subsampling_);
    const QSignalBlocker blockLossless(lossless_);
    const QSignalBlocker blockPng(pngLevel_);
    const QSignalBlocker blockInterlaced(interlaced_);
    const QSignalBlocker blockExif(keepExif_);
    const QSignalBlocker blockIptc(keepIptc_);
    const QSignalBlocker blockXmp(keepXmp_);
    const QSignalBlocker blockIcc(keepIcc_);
    const QSignalBlocker blockFix(fixOrientation_);
    const QSignalBlocker blockAlphaWhite(alphaWhite_);
    const QSignalBlocker blockAlphaKeep(alphaKeep_);
    const QSignalBlocker blockAlphaAsk(alphaAsk_);
    const QSignalBlocker blockTargetEnabled(targetEnabled_);
    const QSignalBlocker blockTargetValue(targetValue_);
    const QSignalBlocker blockTargetUnit(targetUnit_);
    const QSignalBlocker blockEffortSlider(effortSlider_);
    const QSignalBlocker blockEffortSpin(effortSpin_);
    title_->setText(QString::fromLatin1(options_.format).toUpper() + IX_TEXT("-Optionen speichern"));
    qualitySlider_->setValue(options_.quality);
    qualitySpin_->setValue(options_.quality);
    progressive_->setChecked(options_.progressive);
    subsampling_->setChecked(options_.noSubsampling);
    lossless_->setChecked(options_.lossless);
    effortSlider_->setRange(caps_.minEffort, caps_.maxEffort);
    effortSpin_->setRange(caps_.minEffort, caps_.maxEffort);
    effortSlider_->setValue(options_.effort);
    effortSpin_->setValue(options_.effort);
    // Caption lives stacked above the row (single column, no overlap).
    if (effortCaption_) {
        effortCaption_->setText(caps_.effortLabel.isEmpty() ? IX_TEXT("Aufwand:")
                                                            : caps_.effortLabel + QStringLiteral(":"));
        effortCaption_->setVisible(caps_.effort);
    }
    pngLevel_->setValue(options_.pngLevel);
    interlaced_->setChecked(options_.interlaced);
    keepExif_->setChecked(options_.keepExif);
    keepIptc_->setChecked(options_.keepIptc);
    keepXmp_->setChecked(options_.keepXmp);
    keepIcc_->setChecked(options_.keepIcc);
    fixOrientation_->setChecked(options_.fixOrientation);
    // Alpha handling only matters for formats without alpha; otherwise the
    // section stays hidden but the stored choice is preserved.
    if (options_.alphaMode == SaveOptions::AlphaMode::Keep)
        alphaKeep_->setChecked(true);
    else if (options_.alphaMode == SaveOptions::AlphaMode::Ask)
        alphaAsk_->setChecked(true);
    else
        alphaWhite_->setChecked(true);
    alphaBox_->setVisible(!alphaWritable(options_.format) && options_.format != "jpeg");
    const bool kb = options_.targetBytes < 1024 * 1024;
    targetUnit_->setCurrentIndex(targetUnit_->findData(kb ? 1024 : 1024 * 1024));
    targetValue_->setValue(double(options_.targetBytes) / (kb ? 1024.0 : 1024.0 * 1024.0));
    targetEnabled_->setChecked(options_.targetSizeEnabled);
    qualityRow_->setVisible(caps_.quality);
    jpegRow_->setVisible(caps_.progressive || caps_.subsampling);
    progressive_->setVisible(caps_.progressive);
    subsampling_->setVisible(caps_.subsampling);
    lossless_->setVisible(caps_.lossless);
    effortRow_->setVisible(caps_.effort);
    pngRow_->setVisible(caps_.pngLevel);
    gifRow_->setVisible(caps_.interlaced);
    const bool hasMeta = caps_.metadataExif || caps_.metadataIptc || caps_.metadataXmp ||
                         caps_.metadataIcc || caps_.orientation;
    metaBox_->setVisible(hasMeta);
    keepExif_->setVisible(caps_.metadataExif);
    keepIptc_->setVisible(caps_.metadataIptc);
    keepXmp_->setVisible(caps_.metadataXmp);
    keepIcc_->setVisible(caps_.metadataIcc);
    fixOrientation_->setVisible(caps_.orientation);
    targetRow_->setVisible(caps_.targetSize);
    // Captions follow their rows: a caption without its row (or vice versa)
    // left the stale overlaps. The profile box is always visible. Bare rows
    // (null caption, e.g. lossless_) toggle the row only.
    // IMPORTANT: use isVisibleTo(this), not isVisible(). rebuild() also runs
    // while the dialog itself is still hidden (prepare() before the first
    // show). isVisible() is then false for every row even when the row was
    // explicitly shown, so isVisible() would hide ALL captions on the first
    // open and they would only reappear on a later rebuild (format change) —
    // exactly the "elements load in stages" effect. isVisibleTo(this) reports
    // the explicit per-row state regardless of the dialog's own visibility.
    for (const auto &pair : std::as_const(captions_)) {
        if (pair.first)
            pair.first->setVisible(pair.second->isVisibleTo(this));
    }
    qualitySlider_->setEnabled(!options_.targetSizeEnabled || !caps_.targetSize);
    qualitySpin_->setEnabled(!options_.targetSizeEnabled || !caps_.targetSize);
    // No adjustSize() here: the single-shot prepare() path sizes once from the
    // fully populated layout. Re-sizing per rebuild caused visible empty-frame
    // flicker and row overlap on HiDPI.
    emit optionsChanged(options_);
}
void SaveOptionsDialog::collect() {
    if (rebuilding_)
        return;
    options_.quality = qualitySlider_->value();
    options_.progressive = progressive_->isChecked();
    options_.noSubsampling = subsampling_->isChecked();
    options_.lossless = lossless_->isChecked();
    options_.effort = effortSlider_->value();
    options_.pngLevel = pngLevel_->value();
    options_.interlaced = interlaced_->isChecked();
    if (alphaWhite_->isChecked())
        options_.alphaMode = SaveOptions::AlphaMode::White;
    else if (alphaKeep_->isChecked())
        options_.alphaMode = SaveOptions::AlphaMode::Keep;
    else if (alphaAsk_->isChecked())
        options_.alphaMode = SaveOptions::AlphaMode::Ask;
    options_.keepExif = keepExif_->isChecked();
    options_.keepIptc = keepIptc_->isChecked();
    options_.keepXmp = keepXmp_->isChecked();
    options_.keepIcc = keepIcc_->isChecked();
    options_.fixOrientation = fixOrientation_->isChecked();
    options_.targetSizeEnabled = targetEnabled_->isChecked();
    options_.targetBytes =
        qint64(targetValue_->value() * targetUnit_->currentData().toInt());
    qualitySlider_->setEnabled(!options_.targetSizeEnabled || !caps_.targetSize);
    qualitySpin_->setEnabled(!options_.targetSizeEnabled || !caps_.targetSize);
    // Any manual change invalidates a previous auto result; only a completed
    // optimization run sets autoQuality again.
    options_.autoQuality = -1;
    if (!options_.targetSizeEnabled)
        targetResult_->clear();
    // Instant: only update in memory + notify. The caller persists once on
    // dialog accept — the old storeSaveOptions() here flushed the INI file
    // synchronously on every slider tick and made dragging stutter.
    emit optionsChanged(options_);
}
void SaveOptionsDialog::cancelTargetOptimization() {
    // Shared by setFormat(), applyOptions() and endSession(): stop every live
    // worker so no stale finished-handler can publish into the next session.
    for (const auto &watcher : std::as_const(targetWatchers_)) {
        if (watcher) {
            watcher->disconnect(this);
            watcher->cancel();
        }
    }
    targetWatchers_.clear();
    for (const auto &timer : std::as_const(targetTimers_)) {
        if (timer) {
            timer->disconnect(this);
            timer->stop();
        }
    }
    targetTimers_.clear();
    for (const auto &cancel : std::as_const(targetCancels_)) {
        if (cancel)
            cancel->store(true);
    }
    targetCancels_.clear();
    optimizeTimer_->stop();
    targetProgress_->hide();
    targetCancel_->hide();
}

void SaveOptionsDialog::runTargetOptimization() {
    if (!caps_.targetSize || document_.empty())
        return;
    // Drop superseded runs before starting a new one: cancel + disconnect
    // the previous worker so a stale finished-handler cannot overwrite the
    // fresh result. Rapid quality ticks otherwise stack watchers (and their
    // targetCancel_ connections) without bound.
    for (const auto &old : std::as_const(targetWatchers_)) {
        if (old) {
            old->disconnect(this);
            old->cancel();
        }
    }
    for (const auto &old : std::as_const(targetTimers_)) {
        if (old) {
            old->disconnect(this);
            old->stop();
        }
    }
    targetWatchers_.clear();
    targetTimers_.clear();
    for (const auto &old : std::as_const(targetCancels_)) {
        if (old)
            old->store(true);
    }
    targetCancels_.clear();
    auto cancel = std::make_shared<std::atomic_bool>(false);
    // The worker lambda must not touch this after teardown: capture only
    // values + the cancel flag, never the dialog or its widgets.
    const auto document = document_;
    const auto options = options_;
    targetProgress_->setVisible(true);
    targetProgress_->setValue(0);
    targetCancel_->setVisible(true);
    targetResult_->setText(IX_TEXT("Optimiere Qualität …"));
    auto *watcher = new QFutureWatcher<TargetSizeResult>(this); targetWatchers_.append(watcher); targetCancels_.append(cancel);
    activeCancel_ = cancel;
    // The finished handler must survive dialog teardown: guard every widget
    // access with a QPointer. QFutureWatcher::cancel() cannot stop an already
    // running QtConcurrent task, so a late completion after dialog close must
    // do nothing instead of touching dead widgets.
    QPointer<SaveOptionsDialog> guard(this);
    // Capture the worker's input snapshot by value: the finished handler
    // compares it against the live dialog state to discard stale results.
    connect(watcher, &QFutureWatcherBase::finished, this,
            [guard, watcher, cancel, options] {
                watcher->deleteLater();
                if (!guard)
                    return;
                guard->targetWatchers_.removeAll(watcher);
                guard->targetCancels_.removeAll(cancel);
                guard->targetProgress_->setVisible(false);
                guard->targetCancel_->setVisible(false);
                const auto result = watcher->future().takeResult();
                if (cancel->load()) {
                    guard->targetResult_->setText(IX_TEXT("Abgebrochen."));
                    return;
                }
                if (!result.quality) {
                    guard->targetResult_->setText(result.error);
                    QMessageBox::warning(guard, IX_TEXT("Zieldateigröße"), result.error);
                    return;
                }
                // Any manual change while the worker ran invalidates its
                // result: only publish when the options are untouched.
                if (guard->options_.autoQuality >= 0 || guard->options_.quality != options.quality ||
                    guard->options_.targetBytes != options.targetBytes ||
                    guard->options_.format != options.format) {
                    guard->targetResult_->setText(IX_TEXT("Verworfen — Einstellungen wurden geändert."));
                    return;
                }
                guard->options_.autoQuality = *result.quality;
                const QSignalBlocker blockSlider(guard->qualitySlider_);
                const QSignalBlocker blockSpin(guard->qualitySpin_);
                guard->qualitySlider_->setValue(*result.quality);
                guard->qualitySpin_->setValue(*result.quality);
                // IrfanView-style honesty: when even quality 1 exceeds the target,
                // say so but still save the smallest reachable file.
                if (result.bytes > guard->options_.targetBytes)
                    guard->targetResult_->setText(
                        IX_TEXT("Ziel nicht erreichbar — kleinstmöglich: Qualität 1 (ca. %1 KB).")
                            .arg(result.bytes / 1024));
                else
                    guard->targetResult_->setText(IX_TEXT("Qualität %1 (ca. %2 KB).")
                                                      .arg(*result.quality)
                                                      .arg(result.bytes / 1024));
                // No disk write here: the result lives in memory and is
                // persisted once on dialog accept (keeps the UI instant).
                emit guard->optionsChanged(guard->options_);
            });
    watcher->setFuture(QtConcurrent::run([document, options, registry = registry_, cancel] {
        const std::atomic_bool &stop = *cancel;
        return optimizeForTargetSize(document, options, registry, {}, stop);
    }));
    // Progress pulses while the worker runs; the bounded probe count keeps
    // this short. Cancellation flows through the shared flag.
    QTimer *pulse = new QTimer(this); targetTimers_.append(pulse);
    QPointer<QFutureWatcher<TargetSizeResult>> watchGuard(watcher);
    QPointer<SaveOptionsDialog> pulseGuard(this);
    connect(pulse, &QTimer::timeout, this, [pulseGuard, pulse, watchGuard] {
        if (!pulseGuard || !watchGuard) {
            pulse->deleteLater();
            if (pulseGuard)
                pulseGuard->targetTimers_.removeAll(pulse);
            return;
        }
        if (!watchGuard->isRunning()) {
            pulse->deleteLater();
            pulseGuard->targetTimers_.removeAll(pulse);
            return;
        }
        pulseGuard->targetProgress_->setValue((pulseGuard->targetProgress_->value() + 1) % 11);
    });
    pulse->start(120);
}
void SaveOptionsDialog::refreshProfileCombo() {
    const QSignalBlocker block(profileCombo_);
    profileCombo_->clear();
    for (auto it = profiles_.begin(); it != profiles_.end(); ++it)
        profileCombo_->addItem(it.key());
}
void SaveOptionsDialog::loadProfile(int index) {
    if (index < 0 || index >= profileCombo_->count())
        return;
    const auto name = profileCombo_->itemText(index);
    if (!profiles_.contains(name))
        return;
    // applyOptions() cancels workers and re-applies everything atomically.
    // Release the fixed size first (QWIDGETSIZE_MAX is not a valid release
    // value) and freeze exactly one final size — a profile can change format,
    // which changes the visible rows.
    releaseFixedSize();
    applyOptions(SaveOptions::fromJson(profiles_.value(name).toObject()));
    freezeSizeToLayout();
    emit optionsChanged(options_);
}
void SaveOptionsDialog::saveProfile() {
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, IX_TEXT("Profil speichern"), IX_TEXT("Name:"), QLineEdit::Normal,
                              profileCombo_->currentText(), &ok);
    if (!ok)
        return;
    const bool exists = profiles_.contains(name);
    if (const auto error = saveProfileError(name, profiles_, exists); !error.isEmpty()) {
        QMessageBox::warning(this, IX_TEXT("Profil speichern"), error);
        return;
    }
    if (exists &&
        QMessageBox::question(this, IX_TEXT("Profil speichern"),
                              IX_TEXT("Profil „%1“ überschreiben?").arg(name)) != QMessageBox::Yes)
        return;
    profiles_.insert(name, options_.toJson());
    storeSaveProfiles(profiles_);
    refreshProfileCombo();
    profileCombo_->setCurrentText(name);
}
void SaveOptionsDialog::deleteProfile() {
    const int index = profileCombo_->currentIndex();
    if (index < 0)
        return;
    const auto name = profileCombo_->itemText(index);
    if (QMessageBox::question(this, IX_TEXT("Profil löschen"),
                              IX_TEXT("Profil „%1“ löschen?").arg(name)) != QMessageBox::Yes)
        return;
    profiles_.remove(name);
    storeSaveProfiles(profiles_);
    refreshProfileCombo();
}
void SaveOptionsDialog::closeEvent(QCloseEvent *event) {
    // No close button is shown (tool window without close hint); this only
    // covers programmatic closes or Alt+F4. Never accept-and-close the save.
    hide();
    event->ignore();
}

void SaveOptionsDialog::keyPressEvent(QKeyEvent *event) {
    // Route Esc through reject() so it also dismisses the native save dialog
    // (QDialog's default would only close the companion). Non-Esc keys keep
    // their default behavior (e.g. Enter still activates focused controls).
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        reject();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void SaveOptionsDialog::reject() {
    // Esc in the companion must behave like Esc in the native dialog: close
    // both. Guard against re-entrancy (the parent chooser finishing can
    // re-enter our close path while the first reject is still unwinding).
    static thread_local bool rejecting = false;
    if (rejecting)
        return;
    rejecting = true;
    const auto reset = qScopeGuard([&] { rejecting = false; });
    // QDialog::reject() first so any accepted-state drains and our own
    // finished handlers can't re-show a stale result afterwards; forwarding
    // to the parent chooser ends its modal loop (its own Esc/reject path
    // hides us as well, see MainWindow::saveImage). A parent-less instance
    // (unit tests) simply rejects itself.
    cancelTargetWorkers();
    QDialog::reject();
    hide();
    if (QWidget *owner = parentWidget()) {
        if (auto *dialog = qobject_cast<QDialog *>(owner))
            dialog->reject();
        else
            owner->close();
    }
}

void SaveOptionsDialog::moveEvent(QMoveEvent *event) {
    QDialog::moveEvent(event);
    // Only report user drags: programmatic move() calls happen before show()
    // or from remembered placement, isVisible() filters those out. Moves
    // emitted by the programmatic auto-placement are suppressed by the dialog
    // itself (autoPlaced_), not reported as user intent.
    if (isVisible() && !autoPlaced_)
        emit movedByUser(pos());
}

void SaveOptionsDialog::cancelTargetWorkers() {
    // Compatibility wrapper: the full cancel (flags + watchers + timers) is
    // cancelTargetOptimization().
    cancelTargetOptimization();
}

SaveOptionsDialog::~SaveOptionsDialog() {
    // Note: QFutureWatcher::cancel() cannot stop an already running
    // QtConcurrent task — the shared atomic flags are what actually abort
    // the bounded probe loop. Set them here so no worker keeps encoding
    // after the dialog is gone.
    cancelTargetWorkers();
    // Drop all queued finished/timeout calls before children are torn down;
    // the handlers guard with QPointer, this is the second safety net.
    for (const auto &watcher : targetWatchers_) {
        if (!watcher)
            continue;
        watcher->disconnect(this);
        watcher->cancel();
    }
    for (const auto &timer : targetTimers_) {
        if (!timer)
            continue;
        timer->disconnect(this);
        timer->stop();
    }
}
} // namespace ix
