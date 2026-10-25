// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "image_document.h"
#include <QHash>
#include <QJsonObject>
#include <atomic>
#include <functional>
namespace ix {
class CodecRegistry;
struct PipelineStep {
    QString type;
    QJsonObject parameters;
};
struct PipelineProfile {
    QString name = QStringLiteral("Neue Verarbeitungskette");
    QList<PipelineStep> steps;
    QJsonObject toJson() const;
    static PipelineProfile fromJson(const QJsonObject &object, QString &error);
    QString validate() const;
};
struct PipelineJob {
    QStringList inputs;
    QString outputDirectory, namePattern = QStringLiteral("{name}_####");
    PipelineProfile profile;
    bool overwrite = false, allowLoss = false;
    int parallelism = 2;
    QString journalRoot;
};
struct PipelineFile {
    QString input, output, error, inputStamp, targetStamp;
    QString targetHash;
    QHash<QString, QString> resourceHashes;
    bool replaces = false;
};
struct PipelinePreview {
    QImage before, after;
    QSize originalSize, resultSize;
    QString error;
};
class BatchPipeline {
  public:
    static QList<PipelineFile> plan(const PipelineJob &job);
    static ImageDocument transform(ImageDocument image, const PipelineProfile &profile,
                                   const std::atomic_bool &cancel, bool allowLoss = false,
                                   const QHash<QString, QImage> *resources = nullptr);
    static PipelinePreview preview(const PipelineJob &job, const QString &input,
                                   const std::atomic_bool &cancel,
                                   const QList<PipelineFile> &reviewedPlan = {});
    // Optional configured codecs are copied into workers; run joins all workers
    // before returning. Custom readers must support the requested parallelism.
    static QList<PipelineFile> run(const PipelineJob &job, const QList<PipelineFile> &plan,
                                   const std::atomic_bool &cancel,
                                   const std::function<void(int, int)> &progress = {},
                                   const CodecRegistry *codecs = nullptr);
};
} // namespace ix
