// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QIcon>
#include <QPalette>
namespace ix {
// Original 24-unit outline artwork; colors follow the current desktop palette.
QIcon uiIcon(const QString &name, const QPalette &palette);
// For Qt controls that retain their standard icon across theme changes.
QIcon uiIcon(const QString &name);
QStringList uiIconNames();
} // namespace ix
