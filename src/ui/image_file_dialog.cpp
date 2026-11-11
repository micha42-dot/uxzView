// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_file_dialog.h"
#include "core/codec_registry.h"
#include <QFileDialog>
#include <QLayout>
#include <QScreen>

namespace ix {
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
