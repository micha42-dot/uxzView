// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <QSize>

namespace ix {
inline QSize fitPixelSize(QSize source, QSize bounds) {
    if (source.isEmpty() || bounds.isEmpty()) return {};
    // QSize can truncate the shorter axis to zero for panoramas and narrow scans.
    return source.scaled(bounds, Qt::KeepAspectRatio).expandedTo({1, 1});
}
} // namespace ix
