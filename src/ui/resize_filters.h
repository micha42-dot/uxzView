// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/localization.h"
#include <QStringList>
namespace ix {
// Order matches ResampleFilter and the existing serialized pipeline IDs.
inline QStringList resizeFilterLabels() {
    return {IX_TEXT("Hermite (schnell)"), IX_TEXT("Triangle (bilinear)"), QStringLiteral("Bell"),
            IX_TEXT("Mitchell (bicubic)"), QStringLiteral("B-Spline"),
            IX_TEXT("Lanczos (langsam)"), IX_TEXT("Pixelverdopplung / Pixel-Art")};
}
} // namespace ix
