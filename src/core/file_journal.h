// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <functional>
namespace ix {
QString sha256File(const QString &path, const std::atomic_bool *cancel = nullptr);
class FileJournal {
  public:
    // A guard can reject record persistence (e.g. fault-injection tests); it
    // never replaces the real atomic writer or relaxes filesystem checks.
    using WriteGuard = std::function<QString(const QJsonObject &)>;
    explicit FileJournal(QString root = {}, WriteGuard writeGuard = {});
    QString root() const {
        return root_;
    }
    QJsonArray entries(QString *error = nullptr) const;
    QString perform(const QString &kind, const QString &source, const QString &target = {});
    QString installOutput(const QString &staged, const QString &target, bool overwrite,
                          const QString &expectedStamp, const QString &expectedHash = {});
    QString undo(const QString &id = {});
    // Explicit repair, under the same interprocess lock as file operations.
    // Reconcile verifiable states only; retain ambiguous files/records.
    QString recover();

  private:
    QString root_;
    WriteGuard writeGuard_;
    QString migrateLegacy() const;
    QString writeEntry(QJsonObject &, bool reserve = false) const;
    QJsonArray readEntries(QString &error) const;
    QString recoverEntries(QJsonArray &entries, const QString &chosen = {}) const;
};
} // namespace ix
