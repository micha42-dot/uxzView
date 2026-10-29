// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <atomic>
#include <functional>
namespace ix {
QString sha256File(const QString &path, const std::atomic_bool *cancel = nullptr);
class FileOperations {
  public:
    using TrashMover = std::function<QString(const QString &, QString *)>;
    // Optional worker-side preflight, useful for policy/failure injection.
    using BeforeChange = std::function<QString()>;
    explicit FileOperations(TrashMover trashMover = {}, BeforeChange beforeChange = {});
    struct OperationResult {
        QString error;
        bool applied = false;
        QString sourceRevision = {}, targetRevision = {};
    };
    OperationResult perform(const QString &kind, const QString &source, const QString &target = {},
                            const QString &expectedSource = {});
  private:
    TrashMover trashMover_;
    BeforeChange beforeChange_;
};
} // namespace ix
