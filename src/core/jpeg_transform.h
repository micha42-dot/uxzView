// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <QRect>
#include <QString>
#include <optional>
namespace ix {
enum class JpegTransform { Left, Right, Rotate180, Horizontal, Vertical, Crop };
// Omitted revisions capture state at entry. An empty expected target requires
// absence. Explicit revisions span chooser/worker scheduling boundaries.
QString transformJpeg(const QString &source, const QString &target, JpegTransform operation, QRect crop = {},
                      const std::optional<QString> &expectedTarget = std::nullopt,
                      const std::optional<QString> &expectedSource = std::nullopt);
} // namespace ix
