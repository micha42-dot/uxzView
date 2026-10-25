// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
namespace ix {
class PluginStore {
  public:
    explicit PluginStore(QString root = {});
    QString root() const {
        return root_;
    }
    QJsonArray installed() const;
    QStringList disabledFormats() const;
    static QJsonArray builtins();
    static QString compatibility(const QJsonObject &manifest);
    QString install(const QByteArray &package, const QString &expectedSha256 = {});
    QString setEnabled(const QString &id, bool enabled);
    QString setBuiltinEnabled(const QStringList &formats, bool enabled);
    static void prepareStartup();
    static bool formatEnabled(const QByteArray &format);
    static QString platform();

  private:
    QString root_;
    QJsonObject state() const;
    QString writeState(const QJsonObject &state) const;
};
QString optionalModulePath(const QString &name);
} // namespace ix
