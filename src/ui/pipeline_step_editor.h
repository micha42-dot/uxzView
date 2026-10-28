// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/batch_pipeline.h"
class QWidget;
namespace ix {
QString pipelineStepTitle(const PipelineStep &step);
bool editPipelineStep(PipelineStep &step, QWidget *parent);
} // namespace ix
