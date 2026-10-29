// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_history.h"
#include "image_memory.h"
#include <QSet>
#include <algorithm>
namespace ix {
DocumentHistory::DocumentHistory(qint64 bytes, int entries)
    : byteLimit_(std::max<qint64>(0, bytes)), entryLimit_(std::max(1, entries)) {}
void DocumentHistory::clear() { undo_.clear(); redo_.clear(); }
void DocumentHistory::recordEdit(const ImageDocument &current, QRect selection) {
    redo_.clear();
    undo_.append({current, selection});
    trim(Direction::Undo);
}
const DocumentHistory::Entry *DocumentHistory::target(Direction direction) const {
    const auto &stack = direction == Direction::Undo ? undo_ : redo_;
    return stack.isEmpty() ? nullptr : &stack.constLast();
}
bool DocumentHistory::commit(Direction direction, const ImageDocument &current, QRect selection) {
    auto &from = direction == Direction::Undo ? undo_ : redo_;
    auto &to = direction == Direction::Undo ? redo_ : undo_;
    if (from.isEmpty()) return false;
    to.append({current, selection});
    from.removeLast();
    trim(direction == Direction::Undo ? Direction::Redo : Direction::Undo);
    return true;
}
void DocumentHistory::updateSources(const std::function<void(ImageDocument &)> &update, bool discardRedo) {
    if (discardRedo) redo_.clear();
    for (auto *stack : {&undo_, &redo_})
        for (auto &entry : *stack) update(entry);
}
qint64 DocumentHistory::retainedBytes() const {
    qint64 bytes = 0;
    QSet<qint64> images;
    QSet<const Metadata *> metadata;
    for (const auto *stack : {&undo_, &redo_})
        for (const auto &entry : *stack) {
            bytes += documentPropertiesMemoryCost(entry) + sizeof(QRect);
            if (!images.contains(entry.pixels.cacheKey())) {
                images.insert(entry.pixels.cacheKey());
                bytes += imageMemoryCost(entry.pixels);
            }
            if (entry.metadata && !metadata.contains(entry.metadata.get())) {
                metadata.insert(entry.metadata.get());
                bytes += metadataMemoryCost(*entry.metadata);
            }
        }
    return bytes;
}
void DocumentHistory::trim(Direction newest) {
    auto &protectedStack = newest == Direction::Undo ? undo_ : redo_;
    auto &other = newest == Direction::Undo ? redo_ : undo_;
    // Drop the most distant states first, preserving the just-recorded inverse
    // operation. One oversized nearest state is retained, as before, so a large
    // edit can still be undone/redone. This is not a hard total-process RAM cap.
    while (size() > 1 && (size() > entryLimit_ || retainedBytes() > byteLimit_)) {
        if (!other.isEmpty()) other.removeFirst();
        else protectedStack.removeFirst();
    }
}
}
