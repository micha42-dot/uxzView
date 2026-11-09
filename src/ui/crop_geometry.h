// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QPointF>
#include <QRect>
#include <QSize>
#include <Qt>
#include <algorithm>
namespace ix {
// Rectangle edges are half-open pixel boundaries. A corner keeps its opposite
// corner fixed; a side keeps the opposite side and perpendicular centre fixed.
inline QRect resizeCropSelection(QRect original, Qt::Edges edges, QPointF delta,
                                 QSize image, QSize ratio = {}) {
    original = original.intersected(QRect(QPoint(), image));
    if (original.isEmpty() || !edges || delta.isNull()) return original;
    const bool left = edges.testFlag(Qt::LeftEdge), right = edges.testFlag(Qt::RightEdge);
    const bool top = edges.testFlag(Qt::TopEdge), bottom = edges.testFlag(Qt::BottomEdge);
    const int x0 = original.x(), x1 = x0 + original.width();
    const int y0 = original.y(), y1 = y0 + original.height();
    if (ratio.width() <= 0 || ratio.height() <= 0) {
        const int x = left ? qRound(std::clamp(x0 + delta.x(), 0.0, double(x1 - 1))) : x0;
        const int y = top ? qRound(std::clamp(y0 + delta.y(), 0.0, double(y1 - 1))) : y0;
        const int endX = right ? qRound(std::clamp(x1 + delta.x(), double(x0 + 1), double(image.width()))) : x1;
        const int endY = bottom ? qRound(std::clamp(y1 + delta.y(), double(y0 + 1), double(image.height()))) : y1;
        return {x, y, endX - x, endY - y};
    }
    const double rw = ratio.width(), rh = ratio.height();
    const double centreX = x0 + original.width() / 2.0, centreY = y0 + original.height() / 2.0;
    const double availableWidth = left ? x1 : right ? image.width() - x0
        : 2 * std::min(centreX, image.width() - centreX);
    const double availableHeight = top ? y1 : bottom ? image.height() - y0
        : 2 * std::min(centreY, image.height() - centreY);
    const double width = original.width() + (left ? -delta.x() : right ? delta.x() : 0);
    const double height = original.height() + (top ? -delta.y() : bottom ? delta.y() : 0);
    // Project corner motion onto the ratio diagonal, so dragging either axis
    // alone can both grow and shrink the crop without jumping between axes.
    double scale = (left || right) && (top || bottom)
        ? (width * rw + height * rh) / (rw * rw + rh * rh)
        : (left || right) ? width / rw : height / rh;
    scale = std::clamp(scale, 0.0, std::min(availableWidth / rw, availableHeight / rh));
    const int w = std::clamp(qRound(rw * scale), 1, int(availableWidth));
    const int h = std::clamp(qRound(rh * scale), 1, int(availableHeight));
    const int x = left ? x1 - w : right ? x0 : qRound(centreX - w / 2.0);
    const int y = top ? y1 - h : bottom ? y0 : qRound(centreY - h / 2.0);
    return {x, y, w, h};
}
} // namespace ix
