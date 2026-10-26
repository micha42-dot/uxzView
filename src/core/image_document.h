// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "metadata.h"
#include <QDateTime>
#include <QImage>
#include <QMap>
#include <QString>
#include <QVector>
namespace ix {
struct ImageDocument {
    QImage pixels;
    QString path;
    QString sourceRevision;
    QByteArray format;
    QMap<QString, QString> text;
    int page = 0;
    int pageCount = 1;
    int delayMs = 100;
    int loopCount = 0;
    bool animated = false;
    bool modified = false;
    bool sourceHasExif = false;
    QString metadataWarning;
    std::shared_ptr<const Metadata> metadata;
    qint64 fileBytes = 0;
    QDateTime sourceModified;
    double dpiX() const;
    double dpiY() const;
    bool empty() const {
        return pixels.isNull();
    }
};
struct ImageResult {
    ImageDocument document;
    QString error;
    bool ok() const {
        return error.isEmpty() && !document.empty();
    }
};
constexpr qint64 ImageMemoryLimit = 512LL * 1024 * 1024;
bool safeSize(QSize size, int bytesPerPixel = 8);
int imageChannelBits(const QImage &image);
QImage::Format imageWorkingFormat(const QImage &image, bool alpha = true);
void inheritImageProperties(const QImage &source, QImage &target);
} // namespace ix
