// SPDX-License-Identifier: GPL-3.0-or-later
#include "recognition_input.h"
#include "localization.h"
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
namespace ix {
QString prepareRecognitionInput(const QImage &image, QRect area, const QString &directory,
                                const QList<RecognitionLanguage> &languages,
                                const std::atomic_bool &cancel) {
    if (cancel.load()) return IX_TEXT("Abgebrochen.");
    area = area.intersected(image.rect());
    if (area.isEmpty() || qint64(area.width()) * area.height() > 100000000 ||
        area.width() > 32768 || area.height() > 32768)
        return IX_TEXT("Bildbereich zu groß. Bitte einen kleineren Bereich auswählen.");
    QSet<QString> codes;
    for (const auto &language : languages) {
        if (!QRegularExpression(QStringLiteral("^[a-z]{3}$")).match(language.code).hasMatch() ||
            codes.contains(language.code) || language.modelFile.isEmpty())
            return IX_TEXT("Sprachdaten konnten nicht vorbereitet werden.");
        codes.insert(language.code);
    }
    // The protocol is row-oriented: never materialize the entire selected
    // region merely to write it to disk. A 1 MiB tile amortizes painter setup.
    const qint64 rowStride = (qint64(area.width()) * 3 + 3) & ~qint64(3);
    const int rows = int(std::clamp<qint64>(1024 * 1024 / rowStride, 1, 64));
    QImage pixels(area.width(), rows, QImage::Format_RGB888);
    if (pixels.isNull()) return IX_TEXT("Nicht genügend Speicher.");
    QFile file(directory + QStringLiteral("/input.ppm"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return file.errorString();
    const auto header = QByteArray("P6\n") + QByteArray::number(area.width()) + ' ' +
                        QByteArray::number(area.height()) + "\n255\n";
    if (file.write(header) != header.size()) return file.errorString();
    for (int top = 0; top < area.height(); top += rows) {
        if (cancel.load()) return IX_TEXT("Abgebrochen.");
        const int count = std::min(rows, area.height() - top);
        pixels.fill(Qt::white);
        {
            QPainter painter(&pixels);
            painter.drawImage(QPoint(), image, QRect(area.x(), area.y() + top, area.width(), count));
        }
        const auto bytes = qint64(area.width()) * 3;
        if (pixels.bytesPerLine() == bytes) {
            // Aligned RGB rows are already a complete PPM payload: one write
            // per strip avoids thousands of small filesystem calls.
            const auto length = bytes * count;
            if (file.write(reinterpret_cast<const char *>(pixels.constBits()), length) != length)
                return file.errorString();
        } else {
            for (int y = 0; y < count; ++y)
                if (file.write(reinterpret_cast<const char *>(pixels.constScanLine(y)), bytes) != bytes)
                    return file.errorString();
        }
    }
    if (!file.flush()) return file.errorString();
    if (!languages.isEmpty() && !QDir().mkpath(directory + QStringLiteral("/tessdata")))
        return IX_TEXT("Sprachdaten konnten nicht vorbereitet werden.");
    for (const auto &language : languages) {
        if (cancel.load()) return IX_TEXT("Abgebrochen.");
        if (!QFile::copy(language.modelFile, directory + QStringLiteral("/tessdata/") +
                        language.code + QStringLiteral(".traineddata")))
            return IX_TEXT("Sprachdaten konnten nicht vorbereitet werden.");
    }
    return cancel.load() ? IX_TEXT("Abgebrochen.") : QString();
}
} // namespace ix
