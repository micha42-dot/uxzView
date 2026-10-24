// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "resize_model.h"
#include <algorithm>
#include <cmath>
namespace ix {
namespace {
int dim(double v) {
    if (!std::isfinite(v))
        return 1;
    return int(std::clamp(std::round(v), 1.0, 100000.0));
}
} // namespace
ResizeModel::ResizeModel(QSize s, double dpi)
    : original_(s.expandedTo(QSize(1, 1))), size_(original_), dpi_(dpi > 0 ? dpi : 96), originalDpi_(dpi_) {}
double ResizeModel::dpi() const {
    return adaptDpi ? originalDpi_ * size_.width() / original_.width() : dpi_;
}
void ResizeModel::setDpi(double v) {
    double w = width(), h = height();
    dpi_ = std::clamp(v, 1.0, 100000.0);
    if (unit_ != Unit::Pixel) {
        size_ = QSize(dim(toPixels(w)), dim(toPixels(h)));
    }
}
double ResizeModel::toPixels(double v) const {
    return unit_ == Unit::Pixel ? v : v * dpi_ / (unit_ == Unit::Centimeter ? 2.54 : 1);
}
double ResizeModel::fromPixels(int v) const {
    return unit_ == Unit::Pixel ? v : v / dpi_ * (unit_ == Unit::Centimeter ? 2.54 : 1);
}
double ResizeModel::width() const {
    return fromPixels(size_.width());
}
double ResizeModel::height() const {
    return fromPixels(size_.height());
}
double ResizeModel::percentWidth() const {
    return double(size_.width()) / original_.width() * 100;
}
double ResizeModel::percentHeight() const {
    return double(size_.height()) / original_.height() * 100;
}
void ResizeModel::widthPixels(double v) {
    size_.setWidth(dim(v));
    if (proportional)
        size_.setHeight(dim(double(size_.width()) * original_.height() / original_.width()));
}
void ResizeModel::heightPixels(double v) {
    size_.setHeight(dim(v));
    if (proportional)
        size_.setWidth(dim(double(size_.height()) * original_.width() / original_.height()));
}
void ResizeModel::setWidth(double v) {
    widthPixels(toPixels(v));
}
void ResizeModel::setHeight(double v) {
    heightPixels(toPixels(v));
}
void ResizeModel::setPercentWidth(double v) {
    widthPixels(original_.width() * v / 100);
}
void ResizeModel::setPercentHeight(double v) {
    heightPixels(original_.height() * v / 100);
}
void ResizeModel::setMegapixels(double v) {
    double ratio = double(original_.width()) / original_.height();
    size_ = QSize(dim(std::sqrt(std::max(0.000001, v) * 1e6 * ratio)),
                  dim(std::sqrt(std::max(.000001, v) * 1e6 / ratio)));
}
void ResizeModel::preset(QSize s, bool keep) {
    size_ = keep ? original_.scaled(s, Qt::KeepAspectRatio) : s;
}
void ResizeModel::scale(double f) {
    size_ = QSize(dim(size_.width() * f), dim(size_.height() * f));
}
void ResizeModel::swapSides() {
    size_.transpose();
}
} // namespace ix
