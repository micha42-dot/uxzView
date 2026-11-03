// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "codec_registry.h"
#include <functional>
#include <atomic>
#include <optional>
namespace ix {
struct ImageLoadRequest {
    QString path;
    int page = 0;
    bool manageColor = true;
    bool sharpenDisplay = false;
    bool rawPreview = true;
};
struct PreparedImage {
    ImageDocument document;
    QImage display;
    QString error;
};
// Presentation pixels never replace the document's original precision/profile.
// Throws on failed display conversion; the owning worker translates exceptions.
QImage displayImage(const QImage &image, bool manageColor, bool sharpen);
// Call on a worker with value inputs. Preview runs synchronously on that worker;
// GUI consumers must queue delivery and validate their generation/lifetime.
// No settings, widget access or document installation happens here.
PreparedImage loadImage(const CodecRegistry &registry, const ImageLoadRequest &request,
                        const std::function<void(QImage)> &preview = {});
// Nullopt means cancelled, distinct from an unreadable image. Native decoders
// remain non-interruptible; cancellation skips work at pipeline boundaries.
std::optional<PreparedImage> loadImage(const CodecRegistry &registry, const ImageLoadRequest &request,
                        const std::function<void(QImage)> &preview, const std::atomic_bool *cancel);
} // namespace ix
