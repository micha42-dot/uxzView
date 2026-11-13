// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
class QFileDialog;
class QScrollArea;
class QWidget;
namespace ix {
class CodecRegistry;
// Adds window-size controls and optional compression space to a Qt file picker.
QScrollArea *addSavePickerControls(QFileDialog &picker, bool withCompression);
QString openImageFile(QWidget *parent, const QString &caption, const QString &directory,
                      const CodecRegistry &registry);
} // namespace ix
