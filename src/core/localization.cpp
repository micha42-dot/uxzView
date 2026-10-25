// SPDX-License-Identifier: GPL-3.0-or-later
#include "localization.h"
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QVariant>
namespace ix {
namespace {
QString language = QStringLiteral("de");
QHash<QString, QString> messages;
} // namespace
bool setUiLanguage(const QString &code) {
    const QStringList languages{QStringLiteral("de"), QStringLiteral("en"), QStringLiteral("ko"),
                                QStringLiteral("ja"), QStringLiteral("fr")};
    const int index = languages.indexOf(code);
    if (index < 0)
        return false;
    QHash<QString, QString> next;
    if (index > 0) {
        QFile file(QStringLiteral(":/languages.json"));
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isArray())
            return false;
        for (auto item : document.array()) {
            const auto row = item.toArray();
            if (row.size() != languages.size() || row[index].toString().isEmpty())
                return false;
            next.insert(row[0].toString(), row[index].toString());
        }
    }
    messages = std::move(next);
    language = code;
    if (auto *app = QCoreApplication::instance())
        app->setProperty("ixUiLanguage", code);
    QLocale::setDefault(QLocale(code));
    return true;
}
QString uiLanguage() {
    return language;
}
QString localized(const QString &source) {
    const auto it = messages.constFind(source);
    return it == messages.cend() ? source : *it;
}
} // namespace ix
