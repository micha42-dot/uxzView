// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QString>
class QIODevice;
namespace ix {
qint64 jpegliWorkingBytes(int width);
QString writeJpegli(QIODevice &destination, const QImage &image, int quality, qint64 byteBudget = 0);
}
