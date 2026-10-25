// SPDX-License-Identifier: GPL-3.0-or-later
#include "recognition_input.h"
#include "localization.h"
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
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
    QImage pixels(area.size(), QImage::Format_RGB888);
    if (pixels.isNull()) return IX_TEXT("Nicht genügend Speicher.");
    pixels.fill(Qt::white);
    { QPainter painter(&pixels); painter.drawImage(QPoint(), image, area); }
    QFile file(directory + QStringLiteral("/input.ppm"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return file.errorString();
    const auto header = QByteArray("P6\n") + QByteArray::number(pixels.width()) + ' ' +
                        QByteArray::number(pixels.height()) + "\n255\n";
    if (file.write(header) != header.size()) return file.errorString();
    for (int y = 0; y < pixels.height(); ++y) {
        if (cancel.load()) return IX_TEXT("Abgebrochen.");
        const auto bytes = qint64(pixels.width()) * 3;
        if (file.write(reinterpret_cast<const char *>(pixels.constScanLine(y)), bytes) != bytes)
            return file.errorString();
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
