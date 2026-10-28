// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/batch_pipeline.h"
#include <QAbstractTableModel>
namespace ix {
// Retain shared batch results; format cells only when a view requests them.
class PipelineResultModel final : public QAbstractTableModel {
  public:
    using QAbstractTableModel::QAbstractTableModel;
    void setEntries(QList<PipelineFile> entries, bool completed);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  private:
    QList<PipelineFile> entries_;
    bool completed_ = false;
};
} // namespace ix
