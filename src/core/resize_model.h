// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <QSize>
namespace ix {
class ResizeModel {
  public:
    enum class Unit { Pixel, Centimeter, Inch };
    explicit ResizeModel(QSize original, double dpi = 96);
    QSize original() const {
        return original_;
    }
    QSize size() const {
        return size_;
    }
    Unit unit() const {
        return unit_;
    }
    void setUnit(Unit u) {
        unit_ = u;
    }
    bool proportional = true;
    bool adaptDpi = false;
    double dpi() const;
    void setDpi(double value);
    double width() const;
    double height() const;
    double percentWidth() const;
    double percentHeight() const;
    void setWidth(double value);
    void setHeight(double value);
    void setPercentWidth(double value);
    void setPercentHeight(double value);
    void setMegapixels(double value);
    void preset(QSize bounds, bool keepRatio = true);
    void scale(double factor);
    void swapSides();

  private:
    QSize original_, size_;
    double dpi_ = 96, originalDpi_ = 96;
    Unit unit_ = Unit::Pixel;
    double toPixels(double v) const;
    double fromPixels(int v) const;
    void widthPixels(double value);
    void heightPixels(double value);
};
} // namespace ix
