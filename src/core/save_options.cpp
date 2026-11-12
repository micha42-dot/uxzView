// SPDX-License-Identifier: GPL-3.0-or-later
#include "save_options.h"
#include "codec_registry.h"
#include "jpegli_encoder.h"
#include "localization.h"
#include "metadata.h"
#include <QIODevice>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QSettings>
#include <algorithm>
#include <array>
namespace ix {
namespace {
QHash<QByteArray, SaveOptions> gOptionsCache;
QJsonObject gProfilesCache;
bool gProfilesCached = false;
// Encoders write sequentially for the formats with a target-size option.
// Count their output without holding an extra copy of the compressed image.
class SizeCounter final : public QIODevice {
public:
    qint64 bytes = 0;
    bool isSequential() const override { return true; }
protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *, qint64 size) override {
        bytes += size;
        return size;
    }
};

qint64 encodeSize(const ImageDocument &document, const SaveOptions &options,
                  const CodecRegistry &registry, CodecRegistry::EncodeOptions encode,
                  int quality, QString &error) {
    SizeCounter counter;
    counter.open(QIODevice::WriteOnly);
    encode.quality = quality;
    try {
        error = registry.encodeImage(document, counter, options.format, encode);
    } catch (const std::exception &ex) {
        error = localized(QString::fromUtf8(ex.what()));
    } catch (...) {
        error = IX_TEXT("Speicher nicht verfügbar.");
    }
    return error.isEmpty() ? counter.bytes : -1;
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
        caps.progressive = caps.subsampling = jpegliEnabled();
        caps.targetSize = true;
        caps.metadataIcc = caps.orientation = true;
    } else if (format == "webp") {
        caps.quality = true;
        caps.defaultQuality = 95;
        caps.lossless = true;
        caps.effort = true;
        caps.minEffort = 0;
        caps.maxEffort = 100;
        caps.defaultEffort = 50;
        caps.targetSize = true;
        caps.metadataIcc = true;
    } else if (format == "avif" || format == "heif" || format == "heic") {
        caps.quality = true;
        caps.defaultQuality = 95;
        caps.lossless = true;
        caps.effort = true;
        caps.minEffort = 0;
        caps.maxEffort = 100;
        caps.defaultEffort = 50;
        caps.targetSize = true;
        caps.metadataIcc = true;
    } else if (format == "png") {
        caps.pngLevel = true;
        caps.metadataIcc = true;
    } else if (format == "gif") {
        caps.interlaced = true;
    } else if (format == "tiff") {
        caps.metadataIcc = true;
    }
    caps.metadataExif = caps.metadataIptc = caps.metadataXmp = metadataWritable(format);
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
    options.format = format;
    gOptionsCache.insert(format, options);
    return options;
}
void storeSaveOptions(const SaveOptions &options) {
    auto preferences = options;
    preferences.autoQuality = -1;
    gOptionsCache.insert(options.format, preferences);
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
CodecRegistry::EncodeOptions saveEncodeOptions(const SaveOptions &options) {
    CodecRegistry::EncodeOptions encode;
    encode.quality = options.targetSizeEnabled && options.autoQuality >= 0 ? options.autoQuality : options.quality;
    if (options.format == "jpeg" && jpegliEnabled()) {
        encode.progressive = options.progressive;
        encode.noSubsampling = options.noSubsampling;
    }
    encode.interlaced = options.format == "gif" && options.interlaced;
    encode.lossless = options.lossless && (options.format == "webp" || options.format == "avif" ||
                                           options.format == "heif" || options.format == "heic");
    encode.effort = options.effort;
    encode.compressionLevel = options.format == "png" ? options.pngLevel : -1;
    encode.keepExif = options.keepExif;
    encode.keepIptc = options.keepIptc;
    encode.keepXmp = options.keepXmp;
    encode.keepIcc = options.keepIcc;
    encode.alphaWhite = options.alphaMode == SaveOptions::AlphaMode::Keep ? 0 : 1;
    return encode;
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
    const auto encode = saveEncodeOptions(options);
    std::array<std::optional<qint64>, 101> measured;
    int completed = 0;
    const int maxProbes = encode.lossless ? 1 : 13;
    const auto measure = [&](int quality) -> qint64 {
        if (cancel.load()) {
            result.error = IX_TEXT("Abgebrochen.");
            return -1;
        }
        if (measured[quality])
            return *measured[quality];
        if (progress && !progress(completed, maxProbes)) {
            result.error = IX_TEXT("Abgebrochen.");
            return -1;
        }
        const auto bytes = encodeSize(document, options, registry, encode, quality, result.error);
        if (cancel.load()) {
            result.error = IX_TEXT("Abgebrochen.");
            return -1;
        }
        measured[quality] = bytes;
        ++completed;
        return bytes;
    };
    const auto finish = [&](int quality, qint64 bytes) -> TargetSizeResult {
        if (bytes < 0)
            return result;
        if (cancel.load() || (progress && !progress(maxProbes, maxProbes)))
            return {{}, -1, IX_TEXT("Abgebrochen.")};
        return {quality, bytes, {}};
    };
    if (encode.lossless)
        return finish(100, measure(100));

    // Encoded size can dip as quality increases. Check the next few levels
    // after the binary search and reuse sizes already measured there.
    int low = 1, high = 100, best = 0;
    qint64 bestBytes = -1;
    while (low <= high) {
        const int quality = (low + high) / 2;
        const auto bytes = measure(quality);
        if (bytes < 0)
            return result;
        if (bytes <= options.targetBytes) {
            best = quality;
            bestBytes = bytes;
            low = quality + 1;
        } else {
            high = quality - 1;
        }
    }
    const int upper = std::min(100, best + 5);
    for (int quality = best + 1; quality <= upper; ++quality) {
        const auto bytes = measure(quality);
        if (bytes < 0)
            return result;
        if (bytes <= options.targetBytes) {
            best = quality;
            bestBytes = bytes;
        }
    }
    return best > 0 ? finish(best, bestBytes) : finish(1, measure(1));
}
} // namespace ix
