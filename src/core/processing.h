// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "image_document.h"
#include <atomic>
namespace ix {
enum class ResampleFilter { Hermite, Triangle, Bell, Mitchell, BSpline, Lanczos, Nearest };
struct ResizeOptions {
    QSize size;
    double dpi = 96;
    ResampleFilter filter = ResampleFilter::Lanczos;
    bool sharpen = false;
    bool fastDownscale = true;
    bool linearLight = false;
};
QImage resample(const QImage &source, const ResizeOptions &options, const std::atomic_bool *cancel = nullptr);
QImage rotate(const QImage &source, double degrees);
QImage grayscale(const QImage &source);
QImage negative(const QImage &source, int channel = -1);
QImage channel(const QImage &source, int channel);
QImage swapChannels(const QImage &source, int first, int second);
struct ColorAdjust {
    double brightness = 0, contrast = 0, gamma = 1, saturation = 0;
    int red = 0, green = 0, blue = 0;
};
QImage adjustColors(const QImage &source, const ColorAdjust &adjust);
QImage convolve(const QImage &source, const QVector<double> &kernel, int side, double divisor = 1,
                double offset = 0);
QImage autoCorrect(const QImage &source);
QRect autoCropRect(const QImage &source, int tolerance = 0);
QImage canvas(const QImage &source, int left, int right, int top, int bottom, QColor background);
} // namespace ix
