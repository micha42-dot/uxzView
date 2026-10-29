// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "image_document.h"
namespace ix {
// Conservative retained-buffer estimates, capped above the largest supported
// single-image/cache budget. These are not process RAM or allocator statistics.
qint64 imageMemoryCost(const QImage &image);
qint64 metadataMemoryCost(const Metadata &metadata);
qint64 documentPropertiesMemoryCost(const ImageDocument &document);
qint64 documentMemoryCost(const ImageDocument &document);
}
