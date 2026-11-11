// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QString>
class QIODevice;
namespace ix {
qint64 jpegliWorkingBytes(int width);
struct JpegliOptions {
    bool progressive = false;
    bool noSubsampling = false; // 4:4:4: no chroma subsampling.
    bool flattenAlpha = true;   // Flatten real transparency onto white.
};
QString writeJpegli(QIODevice &destination, const QImage &image, int quality, qint64 byteBudget = 0);
QString writeJpegli(QIODevice &destination, const QImage &image, int quality,
                    const JpegliOptions &options, qint64 byteBudget = 0);
} // namespace ix
