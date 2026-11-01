// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_loading.h"
#include "image_storage.h"
#include "localization.h"
#include "preview_service.h"
#include "processing.h"
#include <QColorSpace>
#include <QElapsedTimer>
#include <QDebug>
#include <stdexcept>
#include <cstring>
namespace ix {
QImage displayImage(const QImage &image, bool manageColor, bool sharpen) {
    QImage out = image;
    if (manageColor && image.colorSpace().isValid() && image.colorSpace() != QColorSpace(QColorSpace::SRgb))
    {
        if (!imageIsFileBacked(image)) out = image.convertedToColorSpace(QColorSpace::SRgb);
        else {
            out=allocateImage(image.size(),image.format(),qint64(image.width())*32);
            for (int y=0; y<image.height(); ++y) {
                QImage row(image.constScanLine(y),image.width(),1,image.bytesPerLine(),image.format());
                row.setColorTable(image.colorTable()); row.setColorSpace(image.colorSpace());
                const auto converted=row.convertedToColorSpace(QColorSpace::SRgb);
                if (converted.isNull() || converted.format()!=out.format()) throw std::runtime_error("Farbprofil konnte nicht zeilenweise konvertiert werden.");
                std::memcpy(out.scanLine(y),converted.constBits(),size_t(image.bytesPerLine()));
            }
            inheritImageProperties(image,out); out.setColorSpace(QColorSpace::SRgb);
        }
    }
    if (sharpen && !out.isNull())
        out = convolve(out, {0, -1, 0, -1, 5, -1, 0, -1, 0}, 3);
    if (out.isNull())
        throw std::runtime_error(IX_TEXT("Bildanzeige konnte nicht berechnet werden.").toStdString());
    return out;
}
PreparedImage loadImage(const CodecRegistry &registry, const ImageLoadRequest &request,
                        const std::function<void(QImage)> &preview) {
    const bool profile = qEnvironmentVariableIntValue("IX_PROFILE_IMAGE_LOAD") != 0;
    QElapsedTimer timer;
    if (profile) {
        timer.start();
        qInfo("ix-load worker started");
    }
    auto result = ImageLoadCache::read(registry, request.path, request.page, [&] {
        // A cached original is already ready: avoid decoding/publishing a
        // smaller RAW preview before returning those full-resolution pixels.
        if (request.rawPreview && preview && request.page == 0 && isRawImage(request.path)) {
            auto pixels = loadPreview(request.path, {1600, 1100});
            if (!pixels.isNull()) preview(std::move(pixels));
        }
    });
    if (profile) qInfo("ix-load worker decode %.3f ms", timer.nsecsElapsed() / 1e6);
    if (!result.ok()) return {{}, {}, result.error};
    auto display = displayImage(result.document.pixels, request.manageColor, request.sharpenDisplay);
    if (profile) qInfo("ix-load worker prepared %.3f ms", timer.nsecsElapsed() / 1e6);
    return {std::move(result.document), std::move(display), {}};
}
} // namespace ix
