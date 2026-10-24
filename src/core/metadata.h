// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <QByteArray>
#include <QMap>
#include <QSize>
#include <QString>
#include <memory>
namespace ix {
class Metadata {
  public:
    struct Data;
    std::shared_ptr<Data> data;
    QMap<QString, QString> tags;
    bool hasExif = false, hasIptc = false, hasXmp = false;
};
std::shared_ptr<const Metadata> readMetadata(const QString &path, QString &warning);
QByteArray attachMetadata(QByteArray encoded, const Metadata &metadata, QSize pixelSize, double dpiX,
                          double dpiY, bool modified, QString &error);
bool metadataWritable(QByteArray format);
} // namespace ix
