// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Central save/compression options: per-format encoder capabilities, the
// persisted user settings, named profiles, and the target-file-size optimizer.
#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <QSize>
#include <atomic>
#include <functional>
#include <optional>
class QImage;
namespace ix {
struct ImageDocument;
class CodecRegistry;
// Encoder capabilities per format id (canonical, e.g. "jpeg", "webp").
struct SaveCapabilities {
    QByteArray format;
    bool quality = false;      // Manual quality slider (0-100).
    int defaultQuality = 95;
    bool lossless = false;      // Lossless toggle (WebP/AVIF/PNG-style).
    bool effort = false;        // Compression effort/speed control.
    int minEffort = 0, maxEffort = 100, defaultEffort = 50;
    QString effortLabel;
    bool progressive = false;   // JPEG progressive.
    bool subsampling = false;   // JPEG chroma subsampling control.
    bool interlaced = false;    // GIF interlace.
    bool targetSize = false;    // Target-size optimizer available.
    bool metadataExif = false, metadataIptc = false, metadataXmp = false;
    bool metadataIcc = false, orientation = false;
    bool pngLevel = false;      // PNG compression level.
};
SaveCapabilities saveCapabilities(const QByteArray &format);
// Whether the compression companion appears next to Save As (default on).
bool saveCompanionEnabled();
void setSaveCompanionEnabled(bool enabled);
QByteArray defaultSaveFormat();
// All user-facing compression settings for one export.
struct SaveOptions {
    QByteArray format = "jpeg";
    int quality = 95;               // Manual quality 0-100 (lossy formats).
    bool lossless = false;          // Lossless mode where supported.
    int effort = 50;                // Encoder effort/speed.
    bool progressive = false;       // JPEG progressive.
    bool noSubsampling = false;     // JPEG: disable chroma subsampling.
    bool interlaced = false;        // GIF interlaced.
    int pngLevel = 6;               // PNG compression level 0-9.
    enum class AlphaMode { White = 0, Keep = 1, Ask = 2 };
    AlphaMode alphaMode = AlphaMode::White; // PNG->JPEG transparency handling.
    bool keepExif = true, keepIptc = true, keepXmp = true, keepIcc = true;
    // Orientation is already applied to pixels on decode; the flag is stored
    // for a future tag-normalization step and currently changes nothing.
    bool fixOrientation = true;
    bool targetSizeEnabled = false;
    qint64 targetBytes = 150 * 1024; // Target file size in bytes.
    // Effective quality after target-size optimization (display only).
    int autoQuality = -1;
    QJsonObject toJson() const;
    static SaveOptions fromJson(const QJsonObject &object);
    bool operator==(const SaveOptions &) const = default;
};
SaveOptions defaultSaveOptions(const QByteArray &format);
// Persistence helpers (QSettings-backed, portable-compatible).
SaveOptions loadSaveOptions(const QByteArray &format);
void storeSaveOptions(const SaveOptions &options);
// Named profiles: map profile name -> options JSON.
QJsonObject loadSaveProfiles();
void storeSaveProfiles(const QJsonObject &profiles);
QString saveProfileError(const QString &name, const QJsonObject &profiles, bool exists);
// Target-size optimization: binary search + refinement over the real encoder.
// Encodes to memory buffers only; never touches the destination file.
// Returns the highest quality whose output fits, or nullopt when unreachable.
// Progress callback receives (step, steps); cancel aborts early.
struct TargetSizeResult {
    std::optional<int> quality;
    qint64 bytes = -1;
    QString error;
};
TargetSizeResult optimizeForTargetSize(const ImageDocument &document, const SaveOptions &options,
                                       const CodecRegistry &registry,
                                       const std::function<bool(int, int)> &progress,
                                       const std::atomic_bool &cancel);
} // namespace ix
