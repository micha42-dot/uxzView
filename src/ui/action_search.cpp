// SPDX-License-Identifier: GPL-3.0-or-later
#include "action_search.h"
#include "core/localization.h"
#include <QtWidgets>
namespace ix {
namespace {
void updateItem(QStandardItem *item, const QAction *action) {
    static const QHash<QString, QString> aliases{
        {QStringLiteral("colors"), QStringLiteral("helligkeit kontrast gamma farbe sättigung weißabgleich")},
        {QStringLiteral("resize"), QStringLiteral("skalieren auflösung pixel dpi bildgröße")},
        {QStringLiteral("crop"), QStringLiteral("zuschneiden ausschneiden rahmen")},
        {QStringLiteral("rotate"), QStringLiteral("drehen winkel ausrichten")},
        {QStringLiteral("batch"), QStringLiteral("batch stapel konvertieren wasserzeichen webp kette")},
        {QStringLiteral("library"), QStringLiteral("bewertung sterne farbmarkierung duplikate metadaten suchen")},
        {QStringLiteral("canvas"), QStringLiteral("rand rahmen leinwand")},
        {QStringLiteral("zxing-cpp"), QStringLiteral("qr barcode code lesen scan reader")},
        {QStringLiteral("tesseract-ocr"), QStringLiteral("ocr text erkennen recognition")}};
    auto label = action->text();
    label.remove(QLatin1Char('&'));
    item->setText(label);
    item->setData(label + QLatin1Char(' ') + aliases.value(action->data().toString()), Qt::UserRole + 1);
    item->setEnabled(action->isEnabled());
}
} // namespace
ActionSearch::ActionSearch(QList<QAction *> actions, QWidget *parent)
    : QLineEdit(parent) {
    setObjectName(QStringLiteral("actionSearch"));
    setPlaceholderText(IX_TEXT("Befehl suchen …"));
    setClearButtonEnabled(true);
    setMinimumWidth(130);
    setMaximumWidth(230);
    model_ = new QStandardItemModel(this);
    completion_ = new QCompleter(model_, this);
    completion_->setCaseSensitivity(Qt::CaseInsensitive);
    completion_->setFilterMode(Qt::MatchContains);
    completion_->setCompletionRole(Qt::UserRole + 1);
    completion_->setMaxVisibleItems(10);
    setCompleter(completion_);
    for (auto *action : actions) {
        if (!action)
            continue;
        auto *item = new QStandardItem;
        item->setEditable(false);
        item->setData(QVariant::fromValue(static_cast<QObject *>(action)), Qt::UserRole);
        updateItem(item, action);
        model_->appendRow(item);
        const QPersistentModelIndex row(item->index());
        connect(action, &QAction::changed, this, [this, row, action] {
            if (row.isValid())
                updateItem(model_->itemFromIndex(row), action);
        });
        connect(action, &QObject::destroyed, this, [this, row] {
            if (row.isValid())
                model_->removeRow(row.row());
        });
    }
    connect(this, &QLineEdit::textEdited, this, [this] { updateMatches(); });
    connect(completion_, qOverload<const QModelIndex &>(&QCompleter::activated), this, &ActionSearch::run);
}
void ActionSearch::updateMatches() {
    completion_->setCompletionPrefix(text().trimmed());
    if (text().trimmed().isEmpty()) {
        completion_->popup()->hide();
        return;
    }
    if (hasFocus())
        completion_->complete();
}
void ActionSearch::run(const QModelIndex &index) {
    if (invoking_ || text().trimmed().isEmpty())
        return;
    QPointer<QAction> action = qobject_cast<QAction *>(index.data(Qt::UserRole).value<QObject *>());
    if (!action || !action->isEnabled())
        return;
    invoking_ = true;
    clear();
    completion_->popup()->hide();
    QTimer::singleShot(0, this, [this] { invoking_ = false; });
    emit finished();
    if (action)
        action->trigger();
}
void ActionSearch::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        clear();
        completion_->popup()->hide();
        emit finished();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        auto index = completion_->popup()->currentIndex();
        if (!index.isValid() || !index.flags().testFlag(Qt::ItemIsEnabled)) {
            auto *matches = completion_->completionModel();
            index = {};
            for (int row = 0; row < matches->rowCount(); ++row) {
                const auto candidate = matches->index(row, 0);
                if (candidate.flags().testFlag(Qt::ItemIsEnabled)) {
                    index = candidate;
                    break;
                }
            }
        }
        run(index);
        event->accept();
        return;
    }
    QLineEdit::keyPressEvent(event);
}
} // namespace ix
