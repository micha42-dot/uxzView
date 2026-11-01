// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QtGlobal>
#include <QSize>
#include <QString>
namespace ix {
constexpr qint64 MiB = 1024LL * 1024;
constexpr int ImageAxisLimit = 1000000;
// Accounting saturates above every supported cache/history budget, independently
// of the decoder budget. QCache costs must remain positive signed integers.
constexpr qint64 ImageCostLimit = 2147483647;
quint64 physicalMemoryBytes();
qint64 calculateImageMemoryLimit(quint64 physicalBytes, qint64 overrideMiB = 0,
                                int addressBits = int(sizeof(void *) * 8));
// Process-wide Qt image IO settings are initialized before workers start.
// Runtime preferences take effect after restart; Qt's global setter is not atomic.
void initializeImageMemoryLimit(qint64 overrideMiB = -1);
qint64 imageMemoryLimit();
QString imageSizeLimitError(QSize requested, int bytesPerPixel = 8);
} // namespace ix
