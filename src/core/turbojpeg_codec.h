// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "codec_registry.h"
class QBuffer;
namespace ix {
// Baseline and progressive JPEG decode via libjpeg-turbo for straight,
// unrotated, sRGB sources. Every other JPEG path delegates to Qt/libjpeg.
class TurboJpegCodec final : public ImageCodec {
public:
    bool accepts(const QString &path) const override;
    ImageResult read(const QString &path, int page) const override;
    // Qt already advertises the JPEG capability (including ICC/rotated/CMYK
    // variants handled by the fallback), so no separate row is reported.
    QList<FormatCapability> capabilities() const override { return {}; }
private:
    static ImageResult qtJpegFallback(QBuffer *io, const QByteArray &buffer,
                                     const QString &path, qint64 fileBytes);
};
} // namespace ix