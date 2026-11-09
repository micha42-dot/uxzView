// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QList>
#include <QStringList>
namespace ix {
class CodecRegistry;
QByteArray canonicalSaveFormat(QByteArray format);
struct SaveFormat {
    QByteArray id;
    QString label;
    QStringList extensions;
    QString filter() const;
    QString suffix() const;
};
QList<SaveFormat> writableSaveFormats(const CodecRegistry &registry);
struct SaveTarget {
    SaveFormat format;
    QString suggestedPath;
    bool needsDialog = true;
};
SaveTarget preferredSaveTarget(const QString &sourcePath, QByteArray sourceFormat,
                               const QList<SaveFormat> &formats, bool preferOriginal,
                               QByteArray fixedFormat);
// An explicit supported extension is a user choice. Otherwise use the selected
// filter and append its suffix; no preference is changed by a single export.
SaveTarget selectedSaveTarget(QString path, const QString &selectedFilter,
                              const QList<SaveFormat> &formats);
}
