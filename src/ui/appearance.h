// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
namespace ix {
// GUI-thread only. Applies once at startup or on a preference/system change.
void applyInterfaceTheme(const QString &theme);
} // namespace ix
