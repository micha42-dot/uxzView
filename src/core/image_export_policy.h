// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "image_storage.h"
namespace ix {
// Shared by encoding, loss confirmation and the GUI's post-save source state.
inline bool exportNeedsStreaming(const QImage &image) {
    return imageIsFileBacked(image) || image.sizeInBytes() > imageMemoryLimit() / 4;
}
} // namespace ix
