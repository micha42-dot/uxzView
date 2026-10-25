// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_loading.h"
#include "localization.h"
#include "preview_service.h"
#include "processing.h"
#include <QColorSpace>
#include <stdexcept>
namespace ix {
QImage displayImage(const QImage &image, bool manageColor, bool sharpen) {
    QImage out = image;
    if (manageColor && image.colorSpace().isValid() && image.colorSpace() != QColorSpace(QColorSpace::SRgb))
        out = image.convertedToColorSpace(QColorSpace::SRgb);
    if (sharpen && !out.isNull())
        out = convolve(out, {0, -1, 0, -1, 5, -1, 0, -1, 0}, 3);
    if (out.isNull())
        throw std::runtime_error(IX_TEXT("Bildanzeige konnte nicht berechnet werden.").toStdString());
    return out;
}
PreparedImage loadImage(const CodecRegistry &registry, const ImageLoadRequest &request,
                        const std::function<void(QImage)> &preview) {
    auto result = ImageLoadCache::read(registry, request.path, request.page, [&] {
        // A cached original is already ready: avoid decoding/publishing a
        // smaller RAW preview before returning those full-resolution pixels.
        if (request.rawPreview && preview && request.page == 0 && isRawImage(request.path)) {
            auto pixels = loadPreview(request.path, {1600, 1100});
            if (!pixels.isNull()) preview(std::move(pixels));
        }
    });
    if (!result.ok()) return {{}, {}, result.error};
    auto display = displayImage(result.document.pixels, request.manageColor, request.sharpenDisplay);
    return {std::move(result.document), std::move(display), {}};
}
} // namespace ix
