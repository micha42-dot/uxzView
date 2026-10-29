// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
#include <functional>
class QWidget;
namespace ix {
void libraryDialog(QWidget *parent, const QStringList &files, const std::function<void(QString)> &open);
} // namespace ix
