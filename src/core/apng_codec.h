// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "codec_registry.h"
namespace ix {
struct ApngCacheStats {
    quint64 filesParsed = 0, framesComposited = 0;
    qint64 retainedBytes = 0, budgetBytes = 0;
};
// APNG is decoded independently; PNG pixel decoding still uses Qt/libpng.
class ApngCodec final : public ImageCodec {
  public:
    static ApngCacheStats cacheStats();
    static void clearSession();
    bool accepts(const QString &path) const override;
    ImageResult read(const QString &path, int page) const override;
    QList<FormatCapability> capabilities() const override;
};
} // namespace ix
