// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "codec_registry.h"
#include "processing.h"
#include <QHash>
#include <QSet>
#include <atomic>
#include <functional>
#include <optional>
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
};
struct BatchEntry {
    QString input, output, error;
    bool moved = false;
    QString sourceRevision = {}, targetRevision = {};
};
struct BatchRelocation {
    QString path, sourceRevision, targetRevision;
};
// Transient result summary. Track file identity through sequential renames,
// including paths freed and reused by a different file in the same dialog.
class BatchFileChanges {
  public:
    void add(const BatchEntry &entry);
    QString resolve(const QString &original) const;
    std::optional<BatchRelocation> relocation(const QString &original) const;
    QStringList affectedPaths() const { return affected_.values(); }
  private:
    QHash<QString, QString> originalForCurrent_;
    QHash<QString, BatchRelocation> currentForOriginal_;
    QSet<QString> affected_;
};
class BatchProcessor {
  public:
    static QList<BatchEntry> run(const BatchOptions &, const std::atomic_bool &cancel,
                                 const std::function<void(int, int)> &progress = {});
    static QString outputName(const QString &input, const QString &pattern, int index,
                              const QByteArray &extension);
};
} // namespace ix
