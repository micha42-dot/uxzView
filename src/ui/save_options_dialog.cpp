// SPDX-License-Identifier: GPL-3.0-or-later
#include "save_options_dialog.h"
#include "core/codec_registry.h"
#include "core/image_document.h"
#include "core/localization.h"
#include "ui/dialog_layout.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QtConcurrent>
#include <atomic>
namespace ix {
SaveOptionsDialog::SaveOptionsDialog(const ImageDocument &document, QWidget *parent)
    : QDialog(parent), document_(document) {
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    auto *layout = dialogLayout(this);
    title_ = new QLabel(this);
    QFont titleFont = title_->font();
    titleFont.setBold(true);
    title_->setFont(titleFont);
    layout->addWidget(title_);
    auto *form = dialogFormLayout();
    layout->addLayout(form);
    qualityRow_ = new QWidget(this);
    auto *qualityLayout = new QHBoxLayout(qualityRow_);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    auto *low = new QLabel(IX_TEXT("Niedrig"), this);
    qualitySlider_ = new QSlider(Qt::Horizontal, this);
    qualitySlider_->setRange(0, 100);
    qualitySlider_->setObjectName(QStringLiteral("compressionQuality"));
    auto *high = new QLabel(IX_TEXT("Hoch"), this);
    qualitySpin_ = new QSpinBox(this);
    qualitySpin_->setRange(0, 100);
    qualitySpin_->setObjectName(QStringLiteral("compressionQualityValue"));
    qualitySpin_->setMinimumWidth(64);
    qualityLayout->addWidget(low);
    qualityLayout->addWidget(qualitySlider_, 1);
    qualityLayout->addWidget(high);
    qualityLayout->addWidget(qualitySpin_);
    form->addRow(IX_TEXT("Qualität:"), qualityRow_);
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
    progressive_ = new QCheckBox(IX_TEXT("Als progressives JPEG speichern"), this);
    progressive_->setObjectName(QStringLiteral("compressionProgressive"));
    subsampling_ = new QCheckBox(IX_TEXT("Chroma-Subsampling deaktivieren"), this);
    subsampling_->setObjectName(QStringLiteral("compressionNoSubsampling"));
    jpegLayout->addWidget(progressive_);
    jpegLayout->addWidget(subsampling_);
    form->addRow(jpegRow_);
    connect(progressive_, &QCheckBox::toggled, this, [this] { collect(); });
    connect(subsampling_, &QCheckBox::toggled, this, [this] { collect(); });
    lossless_ = new QCheckBox(IX_TEXT("Verlustfrei speichern"), this);
    lossless_->setObjectName(QStringLiteral("compressionLossless"));
    form->addRow(lossless_);
    connect(lossless_, &QCheckBox::toggled, this, [this] { collect(); });
    effortRow_ = new QWidget(this);
    auto *effortLayout = new QHBoxLayout(effortRow_);
    effortLayout->setContentsMargins(0, 0, 0, 0);
    effortLabel_ = new QLabel(this);
    effortSlider_ = new QSlider(Qt::Horizontal, this);
    effortSlider_->setObjectName(QStringLiteral("compressionEffort"));
    effortSpin_ = new QSpinBox(this);
    effortSpin_->setObjectName(QStringLiteral("compressionEffortValue"));
    effortSpin_->setMinimumWidth(64);
    effortLayout->addWidget(effortLabel_);
    effortLayout->addWidget(effortSlider_, 1);
    effortLayout->addWidget(effortSpin_);
    form->addRow(IX_TEXT("Komprimierung:"), effortRow_);
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
    pngLevel_ = new QSpinBox(this);
    pngLevel_->setRange(0, 9);
    pngLevel_->setObjectName(QStringLiteral("compressionPngLevel"));
    pngLayout->addWidget(new QLabel(IX_TEXT("Stufe (0–9, verlustfrei):"), this));
    pngLayout->addWidget(pngLevel_, 1);
    form->addRow(pngRow_);
    connect(pngLevel_, &QSpinBox::valueChanged, this, [this] { collect(); });
    gifRow_ = new QWidget(this);
    auto *gifLayout = new QVBoxLayout(gifRow_);
    gifLayout->setContentsMargins(0, 0, 0, 0);
    interlaced_ = new QCheckBox(IX_TEXT("Interlaced speichern"), this);
    interlaced_->setObjectName(QStringLiteral("compressionInterlaced"));
    gifLayout->addWidget(interlaced_);
    form->addRow(gifRow_);
    connect(interlaced_, &QCheckBox::toggled, this, [this] { collect(); });
    metaBox_ = new QWidget(this);
    auto *metaLayout = new QVBoxLayout(metaBox_);
    metaLayout->setContentsMargins(0, 0, 0, 0);
    keepExif_ = new QCheckBox(IX_TEXT("EXIF-Metadaten erhalten"), this);
    keepExif_->setObjectName(QStringLiteral("compressionKeepExif"));
    keepIptc_ = new QCheckBox(IX_TEXT("IPTC-Metadaten erhalten"), this);
    keepIptc_->setObjectName(QStringLiteral("compressionKeepIptc"));
    keepXmp_ = new QCheckBox(IX_TEXT("XMP-Metadaten erhalten"), this);
    keepXmp_->setObjectName(QStringLiteral("compressionKeepXmp"));
    keepIcc_ = new QCheckBox(IX_TEXT("ICC-Farbprofil erhalten"), this);
    keepIcc_->setObjectName(QStringLiteral("compressionKeepIcc"));
    fixOrientation_ = new QCheckBox(IX_TEXT("EXIF-Ausrichtung korrigieren"), this);
    fixOrientation_->setObjectName(QStringLiteral("compressionFixOrientation"));
    metaLayout->addWidget(keepExif_);
    metaLayout->addWidget(keepIptc_);
    metaLayout->addWidget(keepXmp_);
    metaLayout->addWidget(keepIcc_);
    metaLayout->addWidget(fixOrientation_);
    form->addRow(IX_TEXT("Metadaten:"), metaBox_);
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
    targetValue_->setMinimumWidth(90);
    targetUnit_ = new QComboBox(this);
    targetUnit_->addItem(QStringLiteral("KB"), 1024);
    targetUnit_->addItem(QStringLiteral("MB"), 1024 * 1024);
    targetUnit_->setObjectName(QStringLiteral("compressionTargetUnit"));
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
    targetBarLayout->addWidget(targetProgress_, 1);
    targetBarLayout->addWidget(targetCancel_);
    targetLayout->addWidget(targetBar);
    form->addRow(targetRow_);
    connect(targetEnabled_, &QCheckBox::toggled, this, [this](bool checked) {
        collect();
        if (checked)
            runTargetOptimization();
    });
    connect(targetValue_, &QDoubleSpinBox::valueChanged, this, [this] {
        collect();
        if (targetEnabled_->isChecked())
            runTargetOptimization();
    });
    connect(targetUnit_, &QComboBox::currentIndexChanged, this, [this] {
        collect();
        if (targetEnabled_->isChecked())
            runTargetOptimization();
    });
    auto *profileRow = new QWidget(this);
    auto *profileLayout = new QHBoxLayout(profileRow);
    profileLayout->setContentsMargins(0, 0, 0, 0);
    profileCombo_ = new QComboBox(this);
    profileCombo_->setObjectName(QStringLiteral("compressionProfile"));
    profileCombo_->setMinimumWidth(120);
    auto *loadButton = new QPushButton(IX_TEXT("Laden"), this);
    auto *deleteButton = new QPushButton(IX_TEXT("Löschen"), this);
    auto *saveButton = new QPushButton(IX_TEXT("Speichern"), this);
    profileLayout->addWidget(profileCombo_, 1);
    profileLayout->addWidget(loadButton);
    profileLayout->addWidget(deleteButton);
    profileLayout->addWidget(saveButton);
    form->addRow(IX_TEXT("Profile:"), profileRow);
    connect(loadButton, &QPushButton::clicked, this, [this] { loadProfile(profileCombo_->currentIndex()); });
    connect(deleteButton, &QPushButton::clicked, this, [this] { deleteProfile(); });
    connect(saveButton, &QPushButton::clicked, this, [this] { saveProfile(); });
}
void SaveOptionsDialog::setRegistry(const CodecRegistry *registry) {
    registry_ = registry;
}
void SaveOptionsDialog::setFormat(const QByteArray &format) {
    options_ = loadSaveOptions(format);
    caps_ = saveCapabilities(format);
    rebuild();
}
void SaveOptionsDialog::rebuild() {
    // Block every widget signal while the controls are populated: an early
    // collect() would read a half-built dialog and persist that corrupt state.
    rebuilding_ = true;
    const auto done = qScopeGuard([this] { rebuilding_ = false; });
    const QSignalBlocker blockQuality(qualitySlider_);
    const QSignalBlocker blockSpin(qualitySpin_);
    title_->setText(QString::fromLatin1(options_.format).toUpper() + IX_TEXT("-Optionen speichern"));
    qualitySlider_->setValue(options_.quality);
    qualitySpin_->setValue(options_.quality);
    progressive_->setChecked(options_.progressive);
    subsampling_->setChecked(options_.noSubsampling);
    lossless_->setChecked(options_.lossless);
    const QSignalBlocker blockEffort(effortSlider_);
    const QSignalBlocker blockEffortSpin(effortSpin_);
    effortSlider_->setRange(caps_.minEffort, caps_.maxEffort);
    effortSpin_->setRange(caps_.minEffort, caps_.maxEffort);
    effortLabel_->setText(caps_.effortLabel.isEmpty() ? IX_TEXT("Aufwand:") : caps_.effortLabel + QStringLiteral(":"));
    effortSlider_->setValue(options_.effort);
    effortSpin_->setValue(options_.effort);
    pngLevel_->setValue(options_.pngLevel);
    interlaced_->setChecked(options_.interlaced);
    keepExif_->setChecked(options_.keepExif);
    keepIptc_->setChecked(options_.keepIptc);
    keepXmp_->setChecked(options_.keepXmp);
    keepIcc_->setChecked(options_.keepIcc);
    fixOrientation_->setChecked(options_.fixOrientation);
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
    qualitySlider_->setEnabled(!options_.targetSizeEnabled || !caps_.targetSize);
    qualitySpin_->setEnabled(!options_.targetSizeEnabled || !caps_.targetSize);
    adjustSize();
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
    emit optionsChanged(options_);
}
void SaveOptionsDialog::runTargetOptimization() {
    if (!registry_ || !caps_.targetSize)
        return;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    targetProgress_->setVisible(true);
    targetProgress_->setValue(0);
    targetCancel_->setVisible(true);
    targetResult_->setText(IX_TEXT("Optimiere Qualität …"));
    auto *watcher = new QFutureWatcher<TargetSizeResult>(this);
    connect(targetCancel_, &QPushButton::clicked, watcher, [cancel] { cancel->store(true); });
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, cancel] {
        watcher->deleteLater();
        targetProgress_->setVisible(false);
        targetCancel_->setVisible(false);
        const auto result = watcher->future().takeResult();
        if (cancel->load()) {
            targetResult_->setText(IX_TEXT("Abgebrochen."));
            return;
        }
        if (!result.quality) {
            targetResult_->setText(result.error);
            QMessageBox::warning(this, IX_TEXT("Zieldateigröße"), result.error);
            return;
        }
        options_.autoQuality = *result.quality;
        const QSignalBlocker blockSlider(qualitySlider_);
        const QSignalBlocker blockSpin(qualitySpin_);
        qualitySlider_->setValue(*result.quality);
        qualitySpin_->setValue(*result.quality);
        targetResult_->setText(IX_TEXT("Qualität %1 (ca. %2 KB).")
                                   .arg(*result.quality)
                                   .arg(result.bytes / 1024));
        storeSaveOptions(options_);
        emit optionsChanged(options_);
    });
    const auto document = document_;
    const auto options = options_;
    const auto *registry = registry_;
    watcher->setFuture(QtConcurrent::run([document, options, registry, cancel] {
        const std::atomic_bool &stop = *cancel;
        return optimizeForTargetSize(document, options, *registry, {}, stop);
    }));
    // Progress pulses while the worker runs; the bounded probe count keeps
    // this short. Cancellation flows through the shared flag.
    QTimer *pulse = new QTimer(this);
    connect(pulse, &QTimer::timeout, this, [this, pulse, watcher] {
        if (!watcher->isRunning()) {
            pulse->deleteLater();
            return;
        }
        targetProgress_->setValue((targetProgress_->value() + 1) % 11);
    });
    pulse->start(120);
}
void SaveOptionsDialog::setProfiles(const QJsonObject &profiles) {
    profiles_ = profiles;
    refreshProfileCombo();
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
    options_ = SaveOptions::fromJson(profiles_.value(name).toObject());
    caps_ = saveCapabilities(options_.format);
    rebuild();
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
    QDialog::closeEvent(event);
}
} // namespace ix
