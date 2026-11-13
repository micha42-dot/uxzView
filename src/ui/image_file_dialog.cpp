// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_file_dialog.h"
#include "core/codec_registry.h"
#include "core/localization.h"
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScreen>
#include <QScrollArea>
#include <QSplitter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

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
    if (!grid) {
        layout->addWidget(resize);
    } else if (!withCompression) {
        // Join the file navigation controls when there is no side panel.
        auto *navigation = grid->itemAtPosition(0, 1);
        if (navigation && navigation->layout())
            navigation->layout()->addWidget(resize);
        else
            grid->addWidget(resize, 0, grid->columnCount(), Qt::AlignRight | Qt::AlignVCenter);
    }
    if (!withCompression)
        return nullptr;

    auto *side = new QWidget(&picker);
    auto *sideLayout = new QHBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);
    auto *divider = new QFrame(side);
    divider->setFrameShape(QFrame::VLine);
    divider->setFrameShadow(QFrame::Sunken);
    sideLayout->addWidget(divider);
    auto *panel = new QScrollArea(side);
    panel->setFrameShape(QFrame::NoFrame);
    panel->setWidgetResizable(true);
    panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    sideLayout->addWidget(panel);
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setAlignment(Qt::AlignTop);
    panel->setWidget(content);
    if (grid) {
        const int column = grid->columnCount();
        grid->addWidget(resize, 0, column, Qt::AlignRight | Qt::AlignVCenter);
        auto *browser = picker.findChild<QSplitter *>(QStringLiteral("splitter"));
        const int browserIndex = browser ? grid->indexOf(browser) : -1;
        int row = 1, startColumn = 0, rowSpan = 1, columnSpan = 1;
        if (browserIndex >= 0)
            grid->getItemPosition(browserIndex, &row, &startColumn, &rowSpan, &columnSpan);
        grid->addWidget(side, row, column, rowSpan, 1);
    } else {
        layout->addWidget(side);
    }
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
