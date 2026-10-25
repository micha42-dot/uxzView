// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "selection.h"
#include "image_document.h"
#include <cstring>
#include <stdexcept>
namespace ix {
QRect centeredMaximalSelection(QSize image, QSize ratio) {
    if (!image.isValid() || !ratio.isValid())
        return {};
    auto size = ratio.scaled(image, Qt::KeepAspectRatio);
    return {(image.width() - size.width()) / 2, (image.height() - size.height()) / 2, size.width(),
            size.height()};
}
QImage applySelectionEffect(const QImage &source, QRect selection, bool inverted,
                            const std::function<QImage(const QImage &)> &filter) {
    if (source.isNull())
        throw std::runtime_error("Kein Bild geladen.");
    selection = selection.intersected(source.rect());
    if (selection.isEmpty())
        return filter(source);
    const auto working = imageWorkingFormat(source);
    const int bytes = QImage(1, 1, working).depth() / 8;
    if (inverted) {
        // Evaluate neighborhood filters against the complete source, then retain
        // every selected pixel exactly, including RGB under zero alpha.
        auto result = filter(source).convertToFormat(working);
        if (result.size() != source.size())
            throw std::runtime_error("Bildfilter änderte die Bildgröße.");
        auto original = source.convertToFormat(working);
        for (int y = selection.top(); y <= selection.bottom(); ++y)
            std::memcpy(result.scanLine(y) + selection.left() * bytes,
                        original.constScanLine(y) + selection.left() * bytes,
                        size_t(selection.width()) * bytes);
        inheritImageProperties(source, result);
        return result;
    }
    auto patch = filter(source.copy(selection));
    if (patch.size() != selection.size())
        throw std::runtime_error("Bildfilter änderte die Größe der Markierung.");
    auto result = source.convertToFormat(working);
    patch = patch.convertToFormat(working);
    for (int y = 0; y < selection.height(); ++y)
        std::memcpy(result.scanLine(selection.y() + y) + selection.left() * bytes, patch.constScanLine(y),
                    size_t(selection.width()) * bytes);
    inheritImageProperties(source, result);
    return result;
}
} // namespace ix
