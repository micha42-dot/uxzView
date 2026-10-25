// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QIcon>
#include <QPalette>
namespace ix {
// Original 24-unit outline artwork; colors follow the current desktop palette.
QIcon uiIcon(const QString &name, const QPalette &palette);
QStringList uiIconNames();
} // namespace ix
