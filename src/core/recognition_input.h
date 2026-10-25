// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <atomic>
namespace ix {
struct RecognitionLanguage {
    QString code;
    QString modelFile;
};
// Worker-only preparation for the existing OCR/QR protocols. The caller owns a
// fresh temporary directory and holds ToolUse leases until the process exits.
// On failure/cancellation partial files remain in that directory for its owner
// to remove. No UI, settings, process launch or source-image mutation occurs here.
QString prepareRecognitionInput(const QImage &image, QRect area, const QString &directory,
                                const QList<RecognitionLanguage> &languages,
                                const std::atomic_bool &cancel);
} // namespace ix
