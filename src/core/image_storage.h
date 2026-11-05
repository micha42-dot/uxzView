// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "memory_policy.h"
#include <QImage>
namespace ix {
struct ImageStorageOptions {
    bool enabled = true;
    qint64 maxBytes = 65536 * MiB;
    QString root;
    qint64 spillThresholdBytes = 0; // Test seam; production uses the RAM budget.
};
ImageStorageOptions imageStorageOptions();
void configureImageStorage(const ImageStorageOptions &options);
void configureImageStorageFromSettings();
qint64 imageBufferBytes(QSize size, QImage::Format format);
bool canAllocateImage(QSize size, QImage::Format format, qint64 residentBytes = 0);
QImage allocateImage(QSize size, QImage::Format format, qint64 residentBytes = 0, bool forceFile = false);
bool imageIsFileBacked(const QImage &image);
QString imageBackingFile(const QImage &image);
QImage copyImageRegion(const QImage &image, QRect region);
QImage transformImageOrientation(const QImage &image, int transformation);
qint64 mappedImageBytes();
// Large backing files released on the GUI thread are deleted asynchronously;
// this waits until queued removals have completed (tests and shutdown).
void flushImageStorageDeletes();
// Only recognized, unlocked buffers in this application's private directory.
int cleanupStaleImageStorage(const QString &root);
} // namespace ix
