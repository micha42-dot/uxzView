// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_loading.h"
#include "image_storage.h"
#include "localization.h"
#include "preview_service.h"
#include "processing.h"
#include <QColorSpace>
#include <QElapsedTimer>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>
#include <QThreadPool>
#include <memory>
#include <stdexcept>
#include <cstring>
namespace ix {
namespace {
bool needsColorConversion(const QImage &image, bool manageColor) {
    return manageColor && image.colorSpace().isValid() && image.colorSpace() != QColorSpace(QColorSpace::SRgb);
}
}
QImage displayImage(const QImage &image, bool manageColor, bool sharpen) {
    QImage out = image;
    if (needsColorConversion(image, manageColor))
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
    auto result = loadImage(registry, request, preview, nullptr);
    return std::move(*result);
}
std::optional<PreparedImage> loadImage(const CodecRegistry &registry, const ImageLoadRequest &request,
                        const std::function<void(QImage)> &preview, const std::atomic_bool *cancel) {
    if (cancel && cancel->load()) return {};
    const bool profile = qEnvironmentVariableIntValue("IX_PROFILE_IMAGE_LOAD") != 0;
    QElapsedTimer timer;
    if (profile) {
        timer.start();
        qInfo("ix-load worker started");
    }
    auto result = ImageLoadCache::read(registry, request.path, request.page, [&] {
        if (cancel && cancel->load()) return;
        // A cached original is already ready: avoid decoding/publishing a
        // smaller RAW preview before returning those full-resolution pixels.
        if (request.rawPreview && preview && request.page == 0 && isRawImage(request.path)) {
            auto pixels = loadPreview(request.path, {1600, 1100}, cancel);
            if (!pixels.isNull() && !(cancel && cancel->load())) preview(std::move(pixels));
        }
    }, cancel);
    if (!result || (cancel && cancel->load())) return {};
    if (profile) qInfo("ix-load worker decode %.3f ms", timer.nsecsElapsed() / 1e6);
    if (!result->ok()) return PreparedImage{{}, {}, result->error};
    auto display = result->document.pixels;
    if (request.sharpenDisplay || needsColorConversion(display, request.manageColor)) {
        display = ImageLoadCache::prepareDisplay(request.path, request.page, result->document,
            request.manageColor, request.sharpenDisplay, [&] {
                if (cancel && cancel->load()) return QImage();
                return displayImage(result->document.pixels, request.manageColor, request.sharpenDisplay);
            });
    }
    if (cancel && cancel->load()) return {};
    if (profile) qInfo("ix-load worker prepared %.3f ms", timer.nsecsElapsed() / 1e6);
    return PreparedImage{std::move(result->document), std::move(display), {}};
}
namespace {
// Speculative decodes outlive neither finishPrewarm() nor the process; the pool
// is leaked on purpose so an abnormal exit never runs its destructor while a
// worker still references the codec registry.
QThreadPool &prewarmPool() {
    static QThreadPool *pool = [] {
        auto *created = new QThreadPool;
        created->setMaxThreadCount(1);
        created->setExpiryTimeout(10000);
        return created;
    }();
    return *pool;
}
} // namespace
void prewarmImage(const QString &path) {
    if (path.isEmpty() || !QFileInfo(path).isFile())
        return;
    const QSettings settings;
    // Exactly the request the command-line open will issue, so the cached
    // original and the prepared display match what the window installs.
    const ImageLoadRequest request{path, 0, settings.value(QStringLiteral("color/manage"), true).toBool(),
                                   settings.value(QStringLiteral("view/sharpenDisplay"), false).toBool(), false};
    prewarmPool().start([request] {
        try {
            // Plugin discovery finished on the main thread before this worker
            // starts, so the registry is built here, off the startup path.
            const CodecRegistry registry;
            (void)loadImage(registry, request, {}, nullptr);
        } catch (...) {
            // A speculative decode never reports; the open supplies diagnostics.
        }
    });
}
void finishPrewarm() { prewarmPool().waitForDone(); }
} // namespace ix
