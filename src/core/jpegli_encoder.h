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
// One tiny encode to warm jpegli's lazy initialisation (Highway SIMD dispatch
// tables, Huffman/quant tables, colour-transform setup) and the ICC path. The
// first real save otherwise pays all of this inside the modal progress dialog,
// which is what made the first PNG->JPEG export take a visible pause while
// every later one was instant. Returns an empty string on success; the encoded
// bytes are discarded, only the warm-up matters. Never throws.
QString warmJpegEncoder();
} // namespace ix
