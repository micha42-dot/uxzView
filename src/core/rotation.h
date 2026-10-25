// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "image_document.h"
namespace ix {
struct RotationOptions {
    double angle = 45;
    bool keepCanvas = false;
    bool originalAsBackground = false;
    bool fastMethod = false;
    bool antialias = true;
    bool selectOriginal = false;
    bool sharpen = false;
    int sharpenAmount = 20;
    QColor background = Qt::black;
};
struct RotationResult {
    QImage pixels;
    QRect selection;
};
RotationResult rotateImage(const QImage &source, const RotationOptions &options);
QRect maximalOriginalRect(QSize source, QSize result, double angle);
} // namespace ix
