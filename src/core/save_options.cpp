// SPDX-License-Identifier: GPL-3.0-or-later
#include "save_options.h"
#include "codec_registry.h"
#include "image_storage.h"
#include "jpegli_encoder.h"
#include "localization.h"
#include "memory_policy.h"
#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QImageWriter>
#include <QJsonObject>
#include <QSettings>
#include <algorithm>
#include <cmath>
namespace ix {
namespace {
QHash<QByteArray, SaveOptions> gOptionsCache;
QJsonObject gProfilesCache;
bool gProfilesCached = false;
qint64 encodeSize(const ImageDocument &document, const SaveOptions &options, int quality,
                  const CodecRegistry &registry, QString &error) {
    Q_UNUSED(registry);
    if (options.format == "jpeg" && jpegliEnabled()) {
        // Measure with the real production encoder (jpegli), not the Qt
        // fallback writer: the optimizer must predict the actual save path.
        // Encode errors (e.g. image too large for the memory budget) abort
        // the search instead of throwing through the worker thread.
        QByteArray encoded;
        QBuffer buffer(&encoded);
        if (!buffer.open(QIODevice::WriteOnly)) {
            error = buffer.errorString();
            return -1;
        }
        ix::JpegliOptions jpegOptions;
        jpegOptions.progressive = options.progressive;
        jpegOptions.noSubsampling = options.noSubsampling;
        jpegOptions.flattenAlpha = true;
        try {
            error = writeJpegli(buffer, document.pixels, quality, jpegOptions);
        } catch (const std::exception &ex) {
            error = localized(QString::fromUtf8(ex.what()));
        } catch (...) {
            error = IX_TEXT("Speicher nicht verfügbar.");
        }
        if (!error.isEmpty())
            return -1;
        return encoded.size();
    }
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly)) {
        error = buffer.errorString();
        return -1;
    }
    // Memory guard + exception barrier: aggressive targets on huge images
    // can OOM inside third-party writers (libwebp/libavif). Without this,
    // a bad_alloc escapes through the QtConcurrent worker and terminates
    // the process. Mirror the jpegli budget check above.
    const qint64 resident = imageIsFileBacked(document.pixels) ? 0 : document.pixels.sizeInBytes();
    const qint64 working = qint64(document.pixels.width()) * 64 + qint64(document.pixels.height()) * 64;
    if (resident + working > imageMemoryLimit()) {
        error = imageSizeLimitError(document.pixels.size(), 64);
        return -1;
    }
    try {
        QImageWriter writer(&buffer, options.format);
        if (options.format == "png")
            writer.setQuality(options.pngLevel * 10 - 1); // 0-9 -> Qt 0-100 approx.
        else if (options.lossless)
            // Lossless-capable writers (WebP/AVIF/JXL): quality 100 selects the
            // lossless path, matching the actual save below.
            writer.setQuality(100);
        else
            writer.setQuality(quality);
        // Effort maps to QImageWriter compression (0 = fast …; writers without
        // compression support ignore it). Mirrors the save path below.
        if (options.format == "webp" || options.format == "avif" || options.format == "heif" ||
            options.format == "heic" || options.format == "jxl" || options.format == "png" ||
            options.format == "tiff")
            writer.setCompression(options.effort);
        if (!writer.canWrite()) {
            error = writer.errorString();
            return -1;
        }
        if (!writer.write(document.pixels)) {
            error = writer.errorString();
            return -1;
        }
    } catch (const std::exception &ex) {
        error = localized(QString::fromUtf8(ex.what()));
        return -1;
    } catch (...) {
        error = IX_TEXT("Speicher nicht verfügbar.");
        return -1;
    }
    return encoded.size();
}
} // namespace
SaveCapabilities saveCapabilities(const QByteArray &format) {
    SaveCapabilities caps;
    caps.format = format;
    if (format == "jpeg") {
        caps.quality = true;
        caps.defaultQuality = 95;
        // Progressive and chroma-subsampling control are jpegli-only. Qt's
        // built-in writer has no subsampling switch, so neither option is
        // offered while the experimental encoder is off (Settings >
        // Experimentell). This is what makes the JPEG row appear only after
        // the user explicitly activates jpegli.
        caps.progressive = jpegliEnabled();
        caps.subsampling = jpegliEnabled();
        caps.targetSize = true;
        caps.metadataExif = caps.metadataIptc = caps.metadataXmp = true;
        caps.metadataIcc = caps.orientation = true;
    } else if (format == "webp") {
        caps.quality = true;
        caps.defaultQuality = 95;
        caps.lossless = true;
        caps.effort = true;
        caps.minEffort = 0;
        caps.maxEffort = 100;
        caps.defaultEffort = 50;
        caps.effortLabel = QStringLiteral("Aufwand");
        caps.targetSize = true;
        caps.metadataExif = caps.metadataIptc = caps.metadataXmp = true;
        caps.metadataIcc = true;
    } else if (format == "avif" || format == "heif" || format == "heic") {
        caps.quality = true;
        caps.defaultQuality = 95;
        caps.lossless = true;
        caps.effort = true;
        caps.minEffort = 0;
        caps.maxEffort = 100;
        caps.defaultEffort = 50;
        caps.effortLabel = QStringLiteral("Aufwand");
        caps.targetSize = true;
        caps.metadataExif = caps.metadataIptc = caps.metadataXmp = true;
        caps.metadataIcc = true;
    } else if (format == "png") {
        caps.pngLevel = true;
        caps.metadataExif = caps.metadataIptc = caps.metadataXmp = true;
        caps.metadataIcc = true;
    } else if (format == "gif") {
        caps.interlaced = true;
    } else if (format == "tiff" || format == "bmp") {
        caps.metadataExif = caps.metadataIptc = caps.metadataXmp = caps.metadataIcc = true;
    }
    return caps;
}
QJsonObject SaveOptions::toJson() const {
    return {{QStringLiteral("format"), QString::fromLatin1(format)},
            {QStringLiteral("quality"), quality},
            {QStringLiteral("lossless"), lossless},
            {QStringLiteral("effort"), effort},
            {QStringLiteral("progressive"), progressive},
            {QStringLiteral("noSubsampling"), noSubsampling},
            {QStringLiteral("interlaced"), interlaced},
            {QStringLiteral("pngLevel"), pngLevel},
            {QStringLiteral("alphaMode"), int(alphaMode)},
            {QStringLiteral("keepExif"), keepExif},
            {QStringLiteral("keepIptc"), keepIptc},
            {QStringLiteral("keepXmp"), keepXmp},
            {QStringLiteral("keepIcc"), keepIcc},
            {QStringLiteral("fixOrientation"), fixOrientation},
            {QStringLiteral("targetSizeEnabled"), targetSizeEnabled},
            {QStringLiteral("targetBytes"), double(targetBytes)}};
}
SaveOptions SaveOptions::fromJson(const QJsonObject &object) {
    SaveOptions options;
    options.format = object.value(QStringLiteral("format")).toString(QStringLiteral("jpeg")).toLatin1();
    options.quality = std::clamp(object.value(QStringLiteral("quality")).toInt(95), 0, 100);
    options.lossless = object.value(QStringLiteral("lossless")).toBool(false);
    options.effort = std::clamp(object.value(QStringLiteral("effort")).toInt(50), 0, 100);
    options.progressive = object.value(QStringLiteral("progressive")).toBool(false);
    options.noSubsampling = object.value(QStringLiteral("noSubsampling")).toBool(false);
    options.interlaced = object.value(QStringLiteral("interlaced")).toBool(false);
    options.pngLevel = std::clamp(object.value(QStringLiteral("pngLevel")).toInt(6), 0, 9);
    options.alphaMode = static_cast<SaveOptions::AlphaMode>(
        std::clamp(object.value(QStringLiteral("alphaMode")).toInt(0), 0, 2));
    options.keepExif = object.value(QStringLiteral("keepExif")).toBool(true);
    options.keepIptc = object.value(QStringLiteral("keepIptc")).toBool(true);
    options.keepXmp = object.value(QStringLiteral("keepXmp")).toBool(true);
    options.keepIcc = object.value(QStringLiteral("keepIcc")).toBool(true);
    options.fixOrientation = object.value(QStringLiteral("fixOrientation")).toBool(true);
    options.targetSizeEnabled = object.value(QStringLiteral("targetSizeEnabled")).toBool(false);
    options.targetBytes =
        qint64(std::clamp(object.value(QStringLiteral("targetBytes")).toDouble(150 * 1024), 1024.0, 2e9));
    return options;
}

SaveOptions defaultSaveOptions(const QByteArray &format) {
    SaveOptions options;
    options.format = format;
    const auto caps = saveCapabilities(format);
    options.quality = caps.defaultQuality;
    options.effort = caps.defaultEffort;
    return options;
}
SaveOptions loadSaveOptions(const QByteArray &format) {
    // Dialog-open fast path: options are read once per format and refreshed
    // on every store, so repeated saves skip QSettings + JSON parsing.
    if (const auto it = gOptionsCache.constFind(format); it != gOptionsCache.cend())
        return *it;
    const QSettings settings;
    const auto key = QStringLiteral("save/options/") + QString::fromLatin1(format);
    SaveOptions options = settings.contains(key)
        ? SaveOptions::fromJson(settings.value(key).toJsonObject())
        : defaultSaveOptions(format);
    gOptionsCache.insert(format, options);
    return options;
}
void storeSaveOptions(const SaveOptions &options) {
    gOptionsCache.insert(options.format, options);
    QSettings settings;
    settings.setValue(QStringLiteral("save/options/") + QString::fromLatin1(options.format),
                      options.toJson());
    settings.sync();
}
void storeSaveProfiles(const QJsonObject &profiles) {
    gProfilesCache = profiles;
    gProfilesCached = true;
    QSettings settings;
    settings.setValue(QStringLiteral("save/profiles"), profiles);
    settings.sync();
}
QJsonObject loadSaveProfiles() {
    if (!gProfilesCached) {
        gProfilesCache = QSettings().value(QStringLiteral("save/profiles")).toJsonObject();
        gProfilesCached = true;
    }
    return gProfilesCache;
}
QString saveProfileError(const QString &name, const QJsonObject &profiles, bool exists) {
    if (name.trimmed().isEmpty())
        return IX_TEXT("Bitte einen Profilnamen eingeben.");
    if (profiles.contains(name) && !exists)
        return IX_TEXT("Ein Profil mit diesem Namen existiert bereits.");
    return {};
}
TargetSizeResult optimizeForTargetSize(const ImageDocument &document, const SaveOptions &options,
                                       const CodecRegistry &registry,
                                       const std::function<bool(int, int)> &progress,
                                       const std::atomic_bool &cancel) {
    TargetSizeResult result;
    if (options.format != "jpeg" && options.format != "webp" && options.format != "avif" &&
        options.format != "heif" && options.format != "heic") {
        result.error = IX_TEXT("Zieldateigröße wird für dieses Format nicht unterstützt.");
        return result;
    }
    // Bounded binary search (<= 8 probes) plus local refinement. File size is
    // not strictly monotonic in quality, so verify each candidate and prefer
    // the highest quality that fits rather than the first fit found.
    int low = 1, high = 100, best = -1;
    qint64 bestBytes = -1;
    QString error;
    const int steps = 8;
    for (int step = 0; step < steps; ++step) {
        if (cancel.load())
            return result;
        if (progress && !progress(step, steps + 2))
            return result;
        const int quality = (low + high) / 2;
        const qint64 bytes = encodeSize(document, options, quality, registry, error);
        if (bytes < 0) {
            result.error = error;
            return result;
        }
        if (bytes <= options.targetBytes) {
            best = quality;
            bestBytes = bytes;
            low = quality + 1; // Try higher quality while staying within budget.
        } else {
            high = quality - 1;
        }
        if (low > high)
            break;
    }
    if (best >= 0) {
        for (int quality = best + 1; quality <= std::min(100, best + 5); ++quality) {
            if (cancel.load())
                return result;
            const qint64 bytes = encodeSize(document, options, quality, registry, error);
            if (bytes < 0) {
                result.error = error;
                return result;
            }
            if (bytes <= options.targetBytes) {
                best = quality;
                bestBytes = bytes;
            } else {
                break;
            }
        }
    }
    if (progress)
        progress(steps + 1, steps + 2);
    // IrfanView-style: never fail on an aggressive target. The search always
    // yields the smallest reachable quality (quality 1) instead of an error,
    // so the save simply lands as close to the target as the format allows.
    if (best < 0) {
        QString error;
        const qint64 floor = encodeSize(document, options, 1, registry, error);
        if (floor < 0) {
            result.error = error;
            return result;
        }
        result.quality = 1;
        result.bytes = floor;
        return result;
    }
    result.quality = best;
    result.bytes = bestBytes;
    return result;
}
} // namespace ix
