// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipeline_result_model.h"
#include "core/localization.h"
#include <QFileInfo>
#include <utility>
namespace ix {
void PipelineResultModel::setEntries(QList<PipelineFile> entries, bool completed) {
    beginResetModel();
    entries_ = std::move(entries);
    completed_ = completed;
    endResetModel();
}
int PipelineResultModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(entries_.size());
}
int PipelineResultModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : 4;
}
QVariant PipelineResultModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case 0: return IX_TEXT("Quelle");
    case 1: return IX_TEXT("Ziel");
    case 2: return IX_TEXT("Aktion");
    case 3: return IX_TEXT("Ergebnis");
    default: return {};
    }
}
QVariant PipelineResultModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size() ||
        index.column() < 0 || index.column() >= 4 ||
        (role != Qt::DisplayRole && role != Qt::ToolTipRole)) return {};
    const auto &entry = entries_.at(index.row());
    switch (index.column()) {
    case 0: return role == Qt::ToolTipRole ? entry.input : QFileInfo(entry.input).fileName();
    case 1: return role == Qt::ToolTipRole ? entry.output : QFileInfo(entry.output).fileName();
    case 2: return entry.replaces ? IX_TEXT("Ersetzen mit Sicherung") : IX_TEXT("Neu anlegen");
    default: return !entry.error.isEmpty() ? localized(entry.error)
        : completed_ ? IX_TEXT("Fertig") : IX_TEXT("Bereit");
    }
}
} // namespace ix
