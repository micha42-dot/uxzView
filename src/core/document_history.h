// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "image_document.h"
#include <QRect>
#include <functional>
namespace ix {
// GUI-thread value owner. Pixel history never changes files on disk.
class DocumentHistory {
  public:
    enum class Direction { Undo, Redo };
    struct Entry : ImageDocument {
        QRect selection;
        Entry(ImageDocument document, QRect selected)
            : ImageDocument(std::move(document)), selection(selected) {}
    };
    explicit DocumentHistory(qint64 byteLimit = 128LL * 1024 * 1024, int entryLimit = 12);
    bool canUndo() const { return !undo_.isEmpty(); }
    bool canRedo() const { return !redo_.isEmpty(); }
    int size() const { return int(undo_.size() + redo_.size()); }
    void clear();
    // Record only after a successful edit. An error must retain both stacks.
    void recordEdit(const ImageDocument &current, QRect selection);
    const Entry *target(Direction direction) const;
    // Commit only after presentation of target() has been prepared successfully.
    bool commit(Direction direction, const ImageDocument &current, QRect selection);
    // Save/rename reconciliation may update source state; never replace pixels.
    void updateSources(const std::function<void(ImageDocument &)> &update, bool discardRedo = false);
    qint64 retainedBytes() const;
  private:
    QList<Entry> undo_, redo_;
    qint64 byteLimit_;
    int entryLimit_;
    void trim(Direction newest);
};
}
