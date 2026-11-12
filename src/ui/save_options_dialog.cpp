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
#include <QScreen>
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
// Compactness comes from the layout (stacked captions, tight spacing), NOT
// from a pixel clamp. A hard maximumWidth is actively harmful: it also caps
// adjustSize() and minimumSizeHint(), so long captions (e.g. "Als progressives
// JPEG speichern" or long translations) get clipped/squeezed instead of
// widening the window. The only real limit is the available screen width.
constexpr int kCompanionMinWidth = 250;
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
    // The application font is deliberately small (referenceFont(9)); compact
    // dialogs with many rows need the same readable-font guard that
    // SettingsDialog applies, otherwise captions render at ~9 pt and become
    // hard to read. Only raises, never lowers.
    auto readableFont = font();
    if (readableFont.pointSizeF() > 0 && readableFont.pointSizeF() < 10) {
        readableFont.setPointSizeF(10);
        setFont(readableFont);
    }
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(5);
    // Font is never shrunk here. Compact means "no wasted space", not
    // "smaller letters": the width follows the natural text metrics of the
    // active theme. Only a floor is set — an upper clamp would squeeze long
    // captions instead of widening the window.
    setMinimumWidth(kCompanionMinWidth);
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
    qualitySlider_ = new QSlider(Qt::Horizontal, qualityRow_);
    qualitySlider_->setRange(0, 100);
    qualitySlider_->setObjectName(QStringLiteral("compressionQuality"));
    qualitySpin_ = new QSpinBox(qualityRow_);
    qualitySpin_->setRange(0, 100);
    qualitySpin_->setObjectName(QStringLiteral("compressionQualityValue"));
    qualitySpin_->setMinimumWidth(48);
    qualitySpin_->setMaximumWidth(56);
    qualityLayout->addWidget(qualitySlider_, 1);
    qualityLayout->addWidget(qualitySpin_);
    // Stacked captions instead of a two-column label column (no overlap).
    auto *qualityCaption = new QLabel(IX_TEXT("Qualität:"), this);
    qualityCaption->setBuddy(qualitySpin_);
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
    auto *effortCaption = new QLabel(IX_TEXT("Aufwand:"), this);
    rowsLayout->addWidget(effortCaption);
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
    // The stacked caption above the row already says "PNG (lossless):", so the
    // spin box only needs a short unit hint. The previous long in-row label
    // competed for the same 250-320 px and squeezed the row.
    pngLayout->addWidget(new QLabel(IX_TEXT("Stufe:"), pngRow_));
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
    targetProgress_->setRange(0, 0);
    targetProgress_->setVisible(false);
    targetCancel_ = new QPushButton(IX_TEXT("Abbrechen"), this);
    targetCancel_->setVisible(false);
    connect(targetCancel_, &QPushButton::clicked, this, [this] {
        cancelTargetOptimization();
        targetResult_->setText(IX_TEXT("Abgebrochen."));
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
    connect(optimizeTimer_, &QTimer::timeout, this, &SaveOptionsDialog::runTargetOptimization);
    connect(&targetWatcher_, &QFutureWatcherBase::finished, this, &SaveOptionsDialog::targetOptimizationFinished);
    connect(targetEnabled_, &QCheckBox::toggled, this, &SaveOptionsDialog::collect);
    connect(targetValue_, &QDoubleSpinBox::valueChanged, this, &SaveOptionsDialog::collect);
    connect(targetUnit_, &QComboBox::currentIndexChanged, this, &SaveOptionsDialog::collect);
    rowsLayout->addSpacing(6);
    auto *profileLabel = new QLabel(IX_TEXT("Profile:"), this);
    profileCombo_ = new QComboBox(this);
    profileCombo_->setObjectName(QStringLiteral("compressionProfile"));
    profileCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    profileCombo_->setMinimumContentsLength(12);
    profileCombo_->setMinimumHeight(fontMetrics().height() + 10);
    profileLabel->setBuddy(profileCombo_);
    rowsLayout->addWidget(profileLabel);
    rowsLayout->addWidget(profileCombo_);
    auto *profileActions = new QHBoxLayout;
    profileActions->setSpacing(6);
    auto *loadButton = new QPushButton(IX_TEXT("Laden"), this);
    auto *saveButton = new QPushButton(IX_TEXT("Speichern"), this);
    auto *deleteButton = new QPushButton(IX_TEXT("Löschen"), this);
    loadButton->setObjectName(QStringLiteral("compressionProfileLoad"));
    saveButton->setObjectName(QStringLiteral("compressionProfileSave"));
    deleteButton->setObjectName(QStringLiteral("compressionProfileDelete"));
    for (auto *button : {loadButton, saveButton, deleteButton}) {
        button->setMinimumHeight(fontMetrics().height() + 10);
        button->setAutoDefault(false);
        profileActions->addWidget(button, 1);
    }
    rowsLayout->addLayout(profileActions);
    const auto updateProfileActions = [loadButton, deleteButton](int index) {
        loadButton->setEnabled(index >= 0);
        deleteButton->setEnabled(index >= 0);
    };
    connect(profileCombo_, &QComboBox::currentIndexChanged, this, updateProfileActions);
    updateProfileActions(profileCombo_->currentIndex());
    // captions_ holds (caption, row) pairs so rebuild() can hide both when the
    // format does not provide the feature — stacked rows can never overlap.
    captions_.append({qualityCaption, qualityRow_});
    captions_.append({jpegCaption, jpegRow_});
    captions_.append({pngCaption, pngRow_});
    captions_.append({gifCaption, gifRow_});
    captions_.append({alphaCaption, alphaBox_});
    captions_.append({metaCaption, metaBox_});
    captions_.append({effortCaption, effortRow_});
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
}

void SaveOptionsDialog::releaseFixedSize() {
    // Release a frozen size with the documented Qt idiom. Never install
    // QWIDGETSIZE_MAX as a MINIMUM (that is a ~16777 px floor).
    setMinimumSize(kCompanionMinWidth, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}

void SaveOptionsDialog::freezeSizeToLayout() {
    // Width is frozen (that is what keeps the dialog compact and stops the
    // staged re-layout). The HEIGHT is deliberately left to the layout:
    // freezing it caused the reported "vertically squeezed" text, because the
    // frozen height was measured before the row layout settled and then
    // clipped every row into a too small box.
    // ensurePolished()+layout()->activate() matter: adjustSize() on a widget
    // whose layout was never activated returns a stale sizeHint, and the
    // correction then happens after the first on-screen paint.
    ensurePolished();
    if (layout())
        layout()->activate();
    adjustSize();
    QSize finalSize = size();
    // Never wider than the available screen (a long translation must not open
    // a window wider than the desktop), never narrower than the compact floor.
    if (const QScreen *screen = this->screen()) {
        const int available = screen->availableGeometry().width() - 2 * 8;
        finalSize.setWidth(qMin(finalSize.width(), available));
    }
    finalSize.setWidth(qMax(finalSize.width(), kCompanionMinWidth));
    // Freeze the width only; the height follows sizeHint so every row keeps
    // its natural height at any UI font or translation.
    setFixedWidth(finalSize.width());
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    adjustSize();
}

void SaveOptionsDialog::preCreateSurface() {
    // Radical but deterministic: make the native window exist now, while the
    // companion is still hidden. QWidget::create() allocates the platform
    // window and runs the full polish/layout path without mapping it on screen.
    // The save-time show() then only has to flip visibility — one paint, no
    // lazy surface creation, no second layout pass, no staged appearance.
    // Safe to call repeatedly (create() is a no-op once the surface exists) and
    // safe on a hidden widget; it never steals focus because the window stays
    // hidden until show().
    ensurePolished();
    if (layout())
        layout()->activate();
    (void)winId(); // Forces create() via the WId accessor.
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
    releaseFixedSize();
    applyOptions(options);
    // Order matters: create the native surface FIRST, then measure. Creating
    // the surface applies the platform theme/font; measuring before that uses
    // the pre-theme font metrics, so the frozen size was too small and the real
    // (larger) text got squeezed into it — exactly the "tiny, cramped text"
    // report. Surface first, measurement second, one fixed size.
    preCreateSurface();
    freezeSizeToLayout();
}

void SaveOptionsDialog::endSession() {
    // A finished/cancelled save must not leave a stale auto result or a live
    // worker behind: the next session starts from neutral state.
    cancelTargetOptimization();
    hide();
    if (parentWidget() != owner_)
        setParent(owner_, windowFlags());
    chooser_ = nullptr;
    document_ = ImageDocument();
    targetResult_->clear();
    options_.autoQuality = -1;
}

void SaveOptionsDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (chooser_ && options_.targetSizeEnabled && caps_.targetSize)
        optimizeTimer_->start(0);
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
    options_ = std::move(options);
    options_.autoQuality = -1;
    caps_ = saveCapabilities(options_.format);
    rebuild();
    scheduleTargetOptimization();
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
    // A stored profile may still carry jpegli-only flags while the
    // experimental encoder is off. Drop them here so the checkbox state, the
    // options handed to the encoder and the persisted profile all agree —
    // a hidden, still-checked box would otherwise keep writing progressive
    // JPEG after the user disabled jpegli. Signals are blocked above.
    if (!caps_.progressive) {
        options_.progressive = false;
        progressive_->setChecked(false);
    }
    if (!caps_.subsampling) {
        options_.noSubsampling = false;
        subsampling_->setChecked(false);
    }
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
}
void SaveOptionsDialog::collect() {
    if (rebuilding_)
        return;
    const auto previous = options_;
    if (!previous.targetSizeEnabled || !caps_.targetSize)
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
    if (options_ == previous)
        return;
    options_.autoQuality = -1;
    const QSignalBlocker blockSlider(qualitySlider_);
    const QSignalBlocker blockSpin(qualitySpin_);
    qualitySlider_->setValue(options_.quality);
    qualitySpin_->setValue(options_.quality);
    scheduleTargetOptimization();
    emit optionsChanged(options_);
}

void SaveOptionsDialog::cancelTargetOptimization() {
    optimizeTimer_->stop();
    optimizationPending_ = false;
    if (activeCancel_)
        activeCancel_->store(true);
    targetProgress_->hide();
    targetCancel_->hide();
}

void SaveOptionsDialog::scheduleTargetOptimization() {
    cancelTargetOptimization();
    const bool wanted = options_.targetSizeEnabled && caps_.targetSize;
    targetProgress_->setVisible(wanted);
    targetCancel_->setVisible(wanted);
    if (wanted) {
        targetResult_->setText(IX_TEXT("Optimiere Qualität …"));
        if (isVisible())
            optimizeTimer_->start();
    } else {
        targetResult_->clear();
    }
}

void SaveOptionsDialog::runTargetOptimization() {
    if (!chooser_ || !isVisible() || !options_.targetSizeEnabled || !caps_.targetSize || document_.empty())
        return;
    if (activeCancel_) {
        // Wait for the superseded probe to release its image and buffers.
        optimizationPending_ = activeCancel_->load();
        return;
    }
    activeCancel_ = std::make_shared<std::atomic_bool>(false);
    targetWatcher_.setFuture(QtConcurrent::run(
        [document = document_, options = options_, registry = registry_, cancel = activeCancel_] {
            return optimizeForTargetSize(document, options, registry, {}, *cancel);
        }));
}

void SaveOptionsDialog::targetOptimizationFinished() {
    const auto cancel = std::exchange(activeCancel_, {});
    const auto result = targetWatcher_.future().takeResult();
    if (std::exchange(optimizationPending_, false)) {
        runTargetOptimization();
        return;
    }
    if (!cancel || cancel->load() || !chooser_ || !isVisible())
        return;
    targetProgress_->hide();
    targetCancel_->hide();
    if (!result.quality) {
        targetResult_->setText(result.error);
        return;
    }
    options_.autoQuality = *result.quality;
    const QSignalBlocker blockSlider(qualitySlider_);
    const QSignalBlocker blockSpin(qualitySpin_);
    qualitySlider_->setValue(*result.quality);
    qualitySpin_->setValue(*result.quality);
    if (result.bytes > options_.targetBytes)
        targetResult_->setText(IX_TEXT("Ziel nicht erreichbar (ca. %1 KB).")
                                   .arg(result.bytes / 1024));
    else
        targetResult_->setText(IX_TEXT("Qualität %1 (ca. %2 KB).").arg(*result.quality).arg(result.bytes / 1024));
    emit optionsChanged(options_);
}
void SaveOptionsDialog::refreshProfileCombo() {
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
    cancelTargetOptimization();
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
    const auto chooser = chooser_;
    cancelTargetOptimization();
    QDialog::reject();
    if (chooser)
        chooser->reject();
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

SaveOptionsDialog::~SaveOptionsDialog() {
    if (activeCancel_)
        activeCancel_->store(true);
}
} // namespace ix
