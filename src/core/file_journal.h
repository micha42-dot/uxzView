// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <atomic>
namespace ix {
QString sha256File(const QString &path, const std::atomic_bool *cancel = nullptr);
class FileJournal {
  public:
    explicit FileJournal(QString root = {});
    QString root() const {
        return root_;
    }
    QJsonArray entries(QString *error = nullptr) const;
    QString perform(const QString &kind, const QString &source, const QString &target = {});
    QString installOutput(const QString &staged, const QString &target, bool overwrite,
                          const QString &expectedStamp, const QString &expectedHash = {});
    QString undo(const QString &id = {});

  private:
    QString root_;
    QString migrateLegacy() const;
    QString writeEntry(QJsonObject &, bool reserve = false) const;
    QJsonArray readEntries(QString &error) const;
};
} // namespace ix
