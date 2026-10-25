// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <QRect>
#include <QString>
namespace ix {
enum class JpegTransform { Left, Right, Rotate180, Horizontal, Vertical, Crop };
QString transformJpeg(const QString &source, const QString &target, JpegTransform operation, QRect crop = {});
} // namespace ix
