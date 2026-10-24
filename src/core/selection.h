// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <QImage>
#include <QRect>
#include <functional>
namespace ix {
QRect centeredMaximalSelection(QSize image, QSize ratio);
QImage applySelectionEffect(const QImage &source, QRect selection, bool inverted,
                            const std::function<QImage(const QImage &)> &filter);
} // namespace ix
