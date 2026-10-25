// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QRect>
class QWidget;
namespace ix {
void imageToolDialog(QWidget *parent, const QString &id, const QImage &image, const QRect &selection);
}
