// SPDX-License-Identifier: GPL-3.0-or-later
#include "crop_ratio_editor.h"
#include "crop_ratios.h"
#include "core/localization.h"
#include <QtWidgets>
namespace ix {
CropRatioEditor::CropRatioEditor(const QStringList &ratios, QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    table_ = new QTableWidget(0, 2);
    table_->setObjectName(QStringLiteral("crop/customRatios"));
    table_->setAccessibleName(IX_TEXT("Eigene Seitenverhältnisse"));
    table_->setHorizontalHeaderLabels({IX_TEXT("Breite"), IX_TEXT("Höhe")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setMaximumHeight(260);
    table_->setMinimumHeight(150);
    layout->addWidget(table_);
    auto *buttons = new QHBoxLayout;
    add_ = new QPushButton(IX_TEXT("Hinzufügen"));
    add_->setObjectName(QStringLiteral("addCropRatio"));
    remove_ = new QPushButton(IX_TEXT("Entfernen"));
    remove_->setObjectName(QStringLiteral("removeCropRatio"));
    buttons->addWidget(add_);
    buttons->addWidget(remove_);
    buttons->addStretch();
    layout->addLayout(buttons);
    connect(add_, &QPushButton::clicked, this, [this] {
        append(1, 1);
        table_->setCurrentCell(table_->rowCount() - 1, 0);
        table_->scrollToBottom();
        table_->cellWidget(table_->rowCount() - 1, 0)->setFocus();
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (table_->currentRow() >= 0) table_->removeRow(table_->currentRow());
        updateButtons();
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    QStringList normalized;
    parseCropRatios(ratios.join(QLatin1Char('\n')), normalized);
    for (const auto &ratio : normalized) {
        const auto size = cropRatioSize(ratio);
        append(size.width(), size.height());
    }
    updateButtons();
}
void CropRatioEditor::append(int width, int height) {
    if (table_->rowCount() >= 32) return;
    const int row = table_->rowCount();
    table_->insertRow(row);
    for (int column = 0; column < 2; ++column) {
        auto *item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table_->setItem(row, column, item);
        auto *spin = new QSpinBox;
        spin->setRange(1, 10000);
        spin->setValue(column == 0 ? width : height);
        spin->setAccessibleName(column == 0 ? IX_TEXT("Breite") : IX_TEXT("Höhe"));
        spin->setFrame(false);
        spin->installEventFilter(this);
        for (auto *child : spin->findChildren<QLineEdit *>()) child->installEventFilter(this);
        table_->setCellWidget(row, column, spin);
        table_->setRowHeight(row, std::max(32, spin->sizeHint().height() + 4));
    }
    updateButtons();
}
void CropRatioEditor::updateButtons() {
    add_->setEnabled(table_->rowCount() < 32);
    remove_->setEnabled(table_->currentRow() >= 0);
}
bool CropRatioEditor::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress)
        for (int row = 0; row < table_->rowCount(); ++row)
            for (int column = 0; column < 2; ++column) {
                auto *cell = table_->cellWidget(row, column);
                if (object == cell || cell->isAncestorOf(qobject_cast<QWidget *>(object))) {
                    table_->setCurrentCell(row, column);
                    updateButtons();
                    return false;
                }
            }
    return QWidget::eventFilter(object, event);
}
QStringList CropRatioEditor::ratios() {
    QStringList raw, normalized;
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto *width = qobject_cast<QSpinBox *>(table_->cellWidget(row, 0));
        auto *height = qobject_cast<QSpinBox *>(table_->cellWidget(row, 1));
        width->interpretText();
        height->interpretText();
        raw.append(cropRatioLabel({width->value(), height->value()}));
    }
    parseCropRatios(raw.join(QLatin1Char('\n')), normalized);
    return normalized;
}
}
