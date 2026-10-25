// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
namespace ix {
inline QString jsonStorageError(const QString &path, bool array, qint64 limit) {
    QFile file(path);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly))
        return file.errorString();
    if (file.size() > limit)
        return QStringLiteral("Datenspeicher ist zu groß: ") + path;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || (array ? !document.isArray() : !document.isObject()))
        return QStringLiteral("Beschädigter Datenspeicher; keine Änderung vorgenommen: ") + path;
    return {};
}
} // namespace ix
