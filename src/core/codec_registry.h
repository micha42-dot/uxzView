// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <functional>
#include "image_document.h"
#include <memory>
#include <optional>
namespace ix {
bool isRawFormat(const QByteArray &format);
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
    // Nullopt captures the destination at entry. An empty expected revision
    // requires absence; a supplied revision spans an earlier UI confirmation.
    QString save(const ImageDocument &document, const QString &path, const QByteArray &format = {},
                 int quality = 95, bool allowLoss = false,
                 const std::optional<QString> &expectedTarget = std::nullopt,
                 const std::function<QString()> &beforeCommit = {}) const;
    QList<FormatCapability> capabilities() const;
    QStringList extensions() const;
    QString openFilter() const;
    QString saveFilter() const;
    static bool canWrite(const QByteArray &format);
    static QStringList exportLosses(const ImageDocument &document, const QByteArray &format);

  private:
    QList<std::shared_ptr<ImageCodec>> codecs_;
};
} // namespace ix
