// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <functional>
#include "image_document.h"
#include <memory>
#include <optional>
class QIODevice;
namespace ix {
bool isRawFormat(const QByteArray &format);
bool alphaWritable(const QByteArray &format);
struct FormatCapability {
    QString name;
    QStringList extensions;
    bool read = false;
    bool write = false;
    QString library;
};
class ImageCodec {
  public:
    virtual ~ImageCodec() = default;
    virtual bool accepts(const QString &path) const = 0;
    virtual ImageResult read(const QString &path, int page) const = 0;
    virtual QList<FormatCapability> capabilities() const = 0;
    virtual QStringList extensions() const {
        QStringList result;
        for (const auto &cap : capabilities())
            if (cap.read)
                result.append(cap.extensions);
        return result;
    }
};
class CodecRegistry {
  public:
    CodecRegistry();
    void add(std::shared_ptr<ImageCodec> codec);
    ImageResult read(const QString &path, int page = 0) const;
    struct EncodeOptions {
        int quality = 95;
        bool progressive = false;   // Honored by the jpegli path below.
        bool noSubsampling = false; // Honored by the jpegli path below.
        bool interlaced = false;    // Requested for GIF; Qt's QImageWriter
                                    // exposes no interlace switch, so the flag
                                    // is accepted but currently a no-op (see
                                    // saveWithOptions).
        bool lossless = false;      // Quality 100 on capable writers (WebP/AVIF/JXL).
        int effort = 50;            // Mapped to QImageWriter compression where supported.
        int compressionLevel = -1;  // PNG 0-9 mapped to writer quality; -1 keeps default.
        bool keepExif = true, keepIptc = true, keepXmp = true, keepIcc = true;
        // PNG->JPEG alpha handling: White flattens onto white (legacy default),
        // Keep preserves the pixels for encoders that store alpha. Ask is
        // resolved by the caller before encoding (never reaches the encoder).
        int alphaWhite = 1;
        // Orientation pixels are already applied by the reader (autoTransform);
        // the flag is persisted for the upcoming tag-normalization step and
        // currently guards no additional transform to avoid double rotation.
        bool fixOrientation = true;
    };
    // Nullopt captures the destination at entry. An empty expected revision
    // requires absence; a supplied revision spans an earlier UI confirmation.
    QString save(const ImageDocument &document, const QString &path, const QByteArray &format = {},
                 int quality = 95, bool allowLoss = false,
                 const std::optional<QString> &expectedTarget = std::nullopt,
                 const std::function<QString()> &beforeCommit = {}) const;
    QString saveWithOptions(const ImageDocument &document, const QString &path,
                            const QByteArray &format, const EncodeOptions &encode,
                            bool allowLoss = false,
                            const std::optional<QString> &expectedTarget = std::nullopt,
                            const std::function<QString()> &beforeCommit = {}) const;
    // Shared encoding path for staged file output and target-size measurement.
    QString encodeImage(const ImageDocument &document, QIODevice &destination,
                        const QByteArray &format, const EncodeOptions &encode) const;
    QList<FormatCapability> capabilities() const;
    QStringList extensions() const;
    QString openFilter() const;
    QString saveFilter() const;
    // Cached filter strings: building the save/open filter walks every codec
    // capability and joins dozens of patterns. The set is fixed at startup,
    // so compute once and reuse on every Save As (IrfanView shows its dialog
    // instantly for the same reason — no per-open scans).
    const QString &cachedSaveFilter() const;
    const QString &cachedOpenFilter() const;
    static bool canWrite(const QByteArray &format);
    // encode == nullptr: warn about every loss (legacy callers, batch review).
    // With options: types the user explicitly deselected are not warned about.
    static QStringList exportLosses(const ImageDocument &document, const QByteArray &format,
                                    const EncodeOptions *encode = nullptr);

  private:
    QList<std::shared_ptr<ImageCodec>> codecs_;
    mutable QString cachedSaveFilter_, cachedOpenFilter_;
};
} // namespace ix
