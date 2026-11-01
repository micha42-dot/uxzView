// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QColor>
#include <algorithm>
#include <cmath>
namespace ix::test {
// Original scalar implementation, kept independent of the scanline fast paths.
inline QRect autoCropReference(const QImage &image, int tolerance = 0) {
    if (image.isNull()) return {};
    const auto background = image.pixelColor(0, 0);
    int left=image.width(), right=-1, top=image.height(), bottom=-1;
    for (int y=0;y<image.height();++y)
        for (int x=0;x<image.width();++x) {
            const auto color=image.pixelColor(x,y);
            if (std::abs(color.red()-background.red())>tolerance ||
                std::abs(color.green()-background.green())>tolerance ||
                std::abs(color.blue()-background.blue())>tolerance ||
                std::abs(color.alpha()-background.alpha())>tolerance) {
                left=std::min(left,x); right=std::max(right,x);
                top=std::min(top,y); bottom=std::max(bottom,y);
            }
        }
    return right<left ? image.rect() : QRect(QPoint(left,top),QPoint(right,bottom));
}
} // namespace ix::test
