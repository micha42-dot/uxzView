// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <QByteArray>
#include <QMap>
#include <QSize>
#include <QString>
#include <memory>
class QIODevice;
namespace ix {
class Metadata {
  public:
    struct Data;
    std::shared_ptr<Data> data;
    QMap<QString, QString> tags;
    bool hasExif = false, hasIptc = false, hasXmp = false;
    qint64 nativeBytes = 0; // Bounded estimate of native tag/value/data-area storage.
};
std::shared_ptr<const Metadata> readMetadata(const QString &path, QString &warning);
// Same parser over an already-open device or an in-memory snapshot, so callers
// that already hold the file bytes (e.g. the in-memory JPEG decode) do not
// reopen the file for Exiv2.
std::shared_ptr<const Metadata> readMetadata(QIODevice &device, const QString &path, QString &warning);
std::shared_ptr<const Metadata> readMetadata(const QByteArray &data, const QString &path, QString &warning);
QByteArray attachMetadata(QByteArray encoded, const Metadata &metadata, QSize pixelSize, double dpiX,
                          double dpiY, bool modified, QString &error);
bool metadataWritable(QByteArray format);
} // namespace ix
