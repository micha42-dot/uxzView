// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "batch_pipeline.h"
namespace ix {
struct PipelineSummary {
    int succeeded = 0, failed = 0;
    qint64 inputBytes = 0, outputBytes = 0;
};
// Byte totals include only committed, successful outputs.
PipelineSummary pipelineSummary(const QList<PipelineFile> &entries);
// Create a unique UTF-8 CSV; never replace an image or an earlier report.
QString writePipelineReport(const QList<PipelineFile> &entries, const QString &directory, QString &path);
} // namespace ix
