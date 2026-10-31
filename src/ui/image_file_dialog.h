// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
class QWidget;
namespace ix {
class CodecRegistry;
QString openImageFile(QWidget *parent, const QString &caption, const QString &directory,
                      const CodecRegistry &registry);
} // namespace ix
