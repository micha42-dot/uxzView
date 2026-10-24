// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "codec_registry.h"
#include "processing.h"
#include <atomic>
#include <functional>
namespace ix {
struct BatchOptions {
    QStringList inputs;
    QString outputDirectory;
    QByteArray format = "png";
    QString namePattern = QStringLiteral("{name}_####");
    int start = 1, quality = 95;
    bool renameOnly = false;
    QSize resizeTo;
    bool allowLoss = false;
    QString journalRoot;
};
struct BatchEntry {
    QString input, output, error;
};
class BatchProcessor {
  public:
    static QList<BatchEntry> run(const BatchOptions &, const std::atomic_bool &cancel,
                                 const std::function<void(int, int)> &progress = {});
    static QString outputName(const QString &input, const QString &pattern, int index,
                              const QByteArray &extension);
};
} // namespace ix
