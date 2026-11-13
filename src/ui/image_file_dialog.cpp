// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_file_dialog.h"
#include "core/codec_registry.h"
#include "core/localization.h"
#include <QFileDialog>
#include <QGridLayout>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>

namespace ix {
QScrollArea *addSavePickerControls(QFileDialog &picker, bool withCompression) {
    auto *layout = picker.layout();
    if (!layout)
        return nullptr;
    // Some Wayland decorations omit maximize/restore controls.
    auto *resize = new QToolButton(&picker);
    const auto label = IX_TEXT("Fenstergröße umschalten");
    resize->setObjectName(QStringLiteral("savePickerWindowSize"));
    resize->setToolTip(label);
    resize->setAccessibleName(label);
    resize->setIcon(resize->style()->standardIcon(QStyle::SP_TitleBarNormalButton));
    QObject::connect(resize, &QToolButton::clicked, &picker, [&picker] {
        if (picker.isMaximized() || picker.isFullScreen())
            picker.showNormal();
        else
            picker.showMaximized();
    });
    auto *grid = qobject_cast<QGridLayout *>(layout);
    const int column = grid ? grid->columnCount() : 0;
    if (grid)
        grid->addWidget(resize, 0, column, Qt::AlignRight);
    else
        layout->addWidget(resize);
    if (!withCompression)
        return nullptr;
    auto *panel = new QScrollArea(&picker);
    panel->setFrameShape(QFrame::NoFrame);
    panel->setWidgetResizable(true);
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setAlignment(Qt::AlignTop);
    panel->setWidget(content);
    if (grid)
        grid->addWidget(panel, 1, column, grid->rowCount() - 1, 1);
    else
        layout->addWidget(panel);
    return panel;
}

QString openImageFile(QWidget *parent, const QString &caption, const QString &directory,
                      const CodecRegistry &registry) {
    QFileDialog picker(parent, caption);
    picker.setObjectName(QStringLiteral("imageOpenDialog"));
    picker.setOptions(QFileDialog::HideNameFilterDetails | QFileDialog::DontUseCustomDirectoryIcons);
    picker.setAcceptMode(QFileDialog::AcceptOpen);
    picker.setFileMode(QFileDialog::ExistingFile);
    if (!directory.isEmpty()) picker.setDirectory(directory);
    picker.setNameFilters(registry.cachedOpenFilter().split(QStringLiteral(";;")));
    // Native dialogs retain their OS geometry. Prepare the Qt fallback before
    // its first Show event instead of resizing an already visible browser.
    picker.ensurePolished();
    if (picker.layout()) picker.layout()->activate();
    picker.resize(QSize(800, 520).boundedTo(picker.screen()->availableGeometry().size() - QSize(32, 48)));
    if (picker.exec() != QDialog::Accepted) return {};
    return picker.selectedFiles().value(0);
}
} // namespace ix
