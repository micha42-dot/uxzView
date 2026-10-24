// SPDX-License-Identifier: GPL-3.0-or-later
#include "action_search.h"
#include "core/localization.h"
#include <QtWidgets>
namespace ix {
ActionSearch::ActionSearch(QList<QAction *> actions, QWidget *parent)
    : QLineEdit(parent), actions_(std::move(actions)) {
    setObjectName(QStringLiteral("actionSearch"));
    setPlaceholderText(IX_TEXT("Bearbeitung suchen …"));
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
    connect(this, &QLineEdit::textEdited, this, [this] { updateMatches(); });
    connect(completion_, qOverload<const QModelIndex &>(&QCompleter::activated), this, &ActionSearch::run);
}
void ActionSearch::updateMatches() {
    model_->clear();
    if (text().trimmed().isEmpty())
        return;
    const QHash<QString, QString> aliases{
        {QStringLiteral("colors"), QStringLiteral("helligkeit kontrast gamma farbe sättigung weißabgleich")},
        {QStringLiteral("resize"), QStringLiteral("skalieren auflösung pixel dpi bildgröße")},
        {QStringLiteral("crop"), QStringLiteral("zuschneiden ausschneiden rahmen")},
        {QStringLiteral("rotate"), QStringLiteral("drehen winkel ausrichten")},
        {QStringLiteral("batch"), QStringLiteral("batch stapel konvertieren wasserzeichen webp kette")},
        {QStringLiteral("library"),
         QStringLiteral("bewertung sterne farbmarkierung duplikate metadaten suchen")},
        {QStringLiteral("canvas"), QStringLiteral("rand rahmen leinwand")},
        {QStringLiteral("zxing-cpp"), QStringLiteral("qr barcode code lesen scan reader")},
        {QStringLiteral("tesseract-ocr"), QStringLiteral("ocr text erkennen recognition")}};
    for (auto *action : actions_) {
        QString label = action->text();
        label.remove(QLatin1Char('&'));
        auto *item = new QStandardItem(label);
        item->setData(QVariant::fromValue(static_cast<QObject *>(action)), Qt::UserRole);
        item->setData(label + QLatin1Char(' ') + aliases.value(action->data().toString()), Qt::UserRole + 1);
        item->setEnabled(action->isEnabled());
        model_->appendRow(item);
    }
    completion_->setCompletionPrefix(text());
    if (hasFocus())
        completion_->complete();
}
void ActionSearch::run(const QModelIndex &index) {
    if (invoking_)
        return;
    auto *action = qobject_cast<QAction *>(index.data(Qt::UserRole).value<QObject *>());
    if (!action || !action->isEnabled())
        return;
    invoking_ = true;
    clear();
    completion_->popup()->hide();
    emit finished();
    QTimer::singleShot(0, this, [this] { invoking_ = false; });
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
        if (!index.isValid())
            index = completion_->completionModel()->index(0, 0);
        run(index);
        event->accept();
        return;
    }
    QLineEdit::keyPressEvent(event);
}
} // namespace ix
