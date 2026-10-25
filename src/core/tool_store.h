// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <memory>
#include <vector>
class QLockFile;
namespace ix {
class ToolUse {
  public:
    ToolUse();
    ~ToolUse();
    QString executable() const { return executable_; }
    QStringList modelFiles() const { return models_; }
  private:
    friend class ToolStore;
    QString executable_;
    QStringList models_;
    std::vector<std::unique_ptr<QLockFile>> locks_;
};
class ToolStore {
  public:
    explicit ToolStore(QString root = {});
    QString root() const { return root_; }
    QJsonArray installed() const;
    QJsonObject package(const QString &id) const;
    QString executable(const QString &id) const;
    QString languageFile(const QString &language) const;
    QStringList languages() const;
    static QJsonObject inspect(const QString &path, QString &error);
    static QString compatibility(const QJsonObject &manifest);
    QString install(const QString &path, const QJsonObject &expected, const std::atomic_bool &cancel);
    std::unique_ptr<ToolUse> acquire(const QString &id, const QStringList &languages, QString &error) const;
    QString remove(const QString &id, const std::atomic_bool &cancel);
    QString setEnabled(const QString &id, bool enabled);
  private:
    QString root_;
    QJsonObject state() const;
    QString writeState(const QJsonObject &) const;
    QString packagePath(const QJsonObject &) const;
    QString payloadRoot() const;
};
} // namespace ix
