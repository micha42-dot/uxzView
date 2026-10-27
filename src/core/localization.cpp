// SPDX-License-Identifier: GPL-3.0-or-later
#include "localization.h"
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QVariant>
#include <array>
#include <atomic>
namespace ix {
namespace {
const QStringList languages{QStringLiteral("de"), QStringLiteral("en"), QStringLiteral("ko"),
                            QStringLiteral("ja"), QStringLiteral("fr")};
std::atomic_int activeLanguage{0};
struct Catalogs {
    std::array<QHash<QString, QString>, 5> messages;
    bool valid = false;
};
const Catalogs &catalogs() {
    // Loaded once on first non-German selection, then immutable for workers.
    static const Catalogs data = [] {
        Catalogs result;
        QFile file(QStringLiteral(":/languages.json"));
        if (!file.open(QIODevice::ReadOnly)) return result;
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isArray()) return result;
        for (const auto &item : document.array()) {
            const auto row = item.toArray();
            if (row.size() != languages.size()) return Catalogs{};
            for (int index = 1; index < 5; ++index) {
                if (row[index].toString().isEmpty()) return Catalogs{};
                result.messages[size_t(index)].insert(row[0].toString(), row[index].toString());
            }
        }
        result.valid = true;
        return result;
    }();
    return data;
}
} // namespace
bool setUiLanguage(const QString &code) {
    const int index = languages.indexOf(code);
    if (index < 0 || (index > 0 && !catalogs().valid)) return false;
    activeLanguage.store(index, std::memory_order_release);
    if (auto *app = QCoreApplication::instance())
        app->setProperty("ixUiLanguage", code);
    return true;
}
QString uiLanguage() {
    return languages.at(activeLanguage.load(std::memory_order_acquire));
}
QLocale uiLocale() { return QLocale(uiLanguage()); }
QString localized(const QString &source) {
    const int index = activeLanguage.load(std::memory_order_acquire);
    if (!index) return source;
    const auto &messages = catalogs().messages[size_t(index)];
    const auto it = messages.constFind(source);
    return it == messages.cend() ? source : *it;
}
} // namespace ix
