// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
#include <optional>
#include <utility>
namespace ix {
enum class UnreadableFiles { Skip, Message, Black };
inline UnreadableFiles unreadableFilesMode(int value) {
    return value >= 0 && value <= 2 ? UnreadableFiles(value) : UnreadableFiles::Skip;
}
// GUI-owned immutable snapshot plus a bounded cursor. No per-failure rescans or
// growing visited set; refreshes cannot invalidate an in-flight traversal.
class FolderTraversal {
  public:
    static std::optional<FolderTraversal> start(QStringList files, qsizetype current,
                                                int step, bool wrap, QString origin) {
        if (files.isEmpty() || step == 0) return {};
        FolderTraversal result;
        result.files_ = std::move(files); result.direction_ = step > 0 ? 1 : -1;
        result.wrap_ = wrap; result.origin_ = std::move(origin);
        result.remaining_ = result.files_.size();
        if (current < 0 || current >= result.files_.size())
            current = step > 0 ? -1 : result.files_.size();
        if (!result.position(current + step)) return {};
        return result;
    }
    QString path() const { return files_.at(index_); }
    qsizetype index() const { return index_; }
    const QStringList &files() const { return files_; }
    bool advance() {
        if (--remaining_ <= 0) return false;
        return position(index_ + direction_);
    }
  private:
    QStringList files_;
    QString origin_;
    qsizetype index_ = 0, remaining_ = 0;
    int direction_ = 1;
    bool wrap_ = true;
    bool position(qsizetype index) {
        if (wrap_) { index %= files_.size(); if (index < 0) index += files_.size(); }
        if (index < 0 || index >= files_.size() || files_.at(index) == origin_) return false;
        index_ = index;
        return true;
    }
};
} // namespace ix
