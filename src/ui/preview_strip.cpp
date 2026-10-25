// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview_strip.h"
#include "core/localization.h"
#include "icons.h"
#include <QtWidgets>
namespace ix {
PreviewStrip::PreviewStrip(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("previewStrip"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 3, 4, 3);
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(92);
    scroll->setObjectName(QStringLiteral("neighborScroll"));
    auto *contents = new QWidget;
    row_ = new QHBoxLayout(contents);
    row_->setContentsMargins(0, 0, 0, 0);
    row_->setSpacing(6);
    row_->setSizeConstraint(QLayout::SetMinimumSize);
    currentMarker_ = new QLabel(contents);
    currentMarker_->setAlignment(Qt::AlignCenter);
    currentMarker_->setFixedWidth(64);
    currentMarker_->setObjectName(QStringLiteral("currentNeighborMarker"));
    currentMarker_->setAccessibleName(IX_TEXT("Aktuelles Bild"));
    currentMarker_->hide();
    scroll->setWidget(contents);
    layout->addWidget(scroll, 1);
    setMaximumHeight(104);
    applySettings();
    connect(&service_, &PreviewService::ready, this,
            [this](const QString &path, const QImage &image, int generation) {
                if (generation != generation_)
                    return;
                if (auto *button = buttons_.value(path)) {
                    if (!image.isNull()) {
                        const auto key = quint64(image.cacheKey());
                        if (button->property("previewImageKey").toULongLong() != key) {
                            button->setIcon(QPixmap::fromImage(image));
                            button->setProperty("previewImageKey", key);
                        }
                        button->setToolTip(path);
                    } else {
                        button->setIcon(uiIcon(QStringLiteral("image"), palette()));
                        button->setProperty("previewImageKey", QVariant());
                        button->setToolTip(path + IX_TEXT("\nKeine schnelle Vorschau verfügbar."));
                    }
                }
            });
}
void PreviewStrip::setFiles(QStringList files, int current) {
    const bool sameFiles = files_.isSharedWith(files);
    if (sameFiles && current_ == current && !needsRefresh_)
        return;
    files_ = std::move(files);
    current_ = current;
    rebuild(!sameFiles);
}
void PreviewStrip::applySettings() {
    QSettings settings;
    const int previous = std::clamp(settings.value(QStringLiteral("preview/previous"), 2).toInt(), 1, 5);
    const int next = std::clamp(settings.value(QStringLiteral("preview/next"), 2).toInt(), 1, 5);
    const bool countsChanged = previous != previous_ || next != next_;
    needsRefresh_ = needsRefresh_ || countsChanged;
    previous_ = previous;
    next_ = next;
    const auto mode = settings.value(QStringLiteral("preview/background"), QStringLiteral("theme")).toString();
    const bool imageBackground = mode == QStringLiteral("image");
    const bool transparent = mode == QStringLiteral("transparent");
    flatButtons_ = imageBackground || transparent;
    auto colors = parentWidget() ? parentWidget()->palette() : QApplication::palette();
    if (imageBackground) {
        const auto background = settings.value(QStringLiteral("view/background"), QColor(Qt::black)).value<QColor>();
        const auto text = background.lightnessF() < .5 ? QColor(QStringLiteral("#edf2f7"))
                                                      : QColor(QStringLiteral("#20242b"));
        colors.setColor(QPalette::Window, background);
        colors.setColor(QPalette::Base, background);
        colors.setColor(QPalette::WindowText, text);
        colors.setColor(QPalette::Text, text);
        colors.setColor(QPalette::ButtonText, text);
    }
    setPalette(colors);
    setAutoFillBackground(imageBackground);
    auto *scroll = findChild<QScrollArea *>(QStringLiteral("neighborScroll"));
    scroll->viewport()->setAutoFillBackground(!transparent);
    scroll->widget()->setAutoFillBackground(!transparent);
    for (auto *button : buttons_) {
        button->setAutoRaise(flatButtons_);
        if (!button->property("previewImageKey").isValid())
            button->setIcon(uiIcon(QStringLiteral("image"), palette()));
    }
    if ((countsChanged || row_->isEmpty()) && !isHidden())
        rebuild();
}
void PreviewStrip::stop() {
    needsRefresh_ = true;
    ++generation_;
    service_.cancel();
}
void PreviewStrip::rebuild(bool reset) {
    stop();
    needsRefresh_ = false;
    auto previousButtons = std::move(buttons_);
    buttons_ = {};
    while (auto *item = row_->takeAt(0)) {
        if (item->widget() != currentMarker_ && !qobject_cast<QToolButton *>(item->widget()))
            delete item->widget();
        delete item;
    }
    row_->addStretch();
    currentMarker_->hide();
    if (current_ < 0 || current_ >= files_.size()) {
        qDeleteAll(previousButtons);
        row_->addStretch();
        return;
    }
    const int first = std::max(0, current_ - previous_),
              last = std::min(int(files_.size()) - 1, current_ + next_);
    for (int index = first; index <= last; ++index) {
        if (index == current_) {
            currentMarker_->setText(QStringLiteral("%1 / %2").arg(index + 1).arg(files_.size()));
            currentMarker_->setToolTip(files_.at(index));
            row_->addWidget(currentMarker_);
            currentMarker_->show();
            continue;
        }
        const auto path = files_.at(index);
        auto *button = reset ? nullptr : previousButtons.take(path);
        if (!button) {
            button = new QToolButton;
            button->setObjectName(QStringLiteral("neighborPreview"));
            button->setProperty("previewPath", path);
            button->setIconSize({68, 50});
            button->setFixedSize(80, 76);
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setText(fontMetrics().elidedText(QFileInfo(path).fileName(), Qt::ElideMiddle, 72));
            button->setToolTip(path);
            button->setIcon(uiIcon(QStringLiteral("image"), palette()));
            connect(button, &QToolButton::clicked, this, [this, path] { emit openRequested(path); });
        }
        button->setAutoRaise(flatButtons_);
        row_->addWidget(button);
        buttons_.insert(path, button);
        // Revalidate cached pixels in the worker so edited/deleted files refresh.
        service_.request(path, {68, 50}, generation_);
    }
    qDeleteAll(previousButtons);
    row_->addStretch();
}
} // namespace ix
