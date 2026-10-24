// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
class QWidget;
namespace ix {
bool extensionManagerDialog(QWidget *parent, const QString &selectedTool = {});
}
