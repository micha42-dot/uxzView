// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QLineEdit>
class QAction;
class QCompleter;
class QStandardItemModel;
namespace ix {
class ActionSearch : public QLineEdit {
    Q_OBJECT
  public:
    explicit ActionSearch(QList<QAction *> actions, QWidget *parent = nullptr);
  signals:
    void finished();

  protected:
    void keyPressEvent(QKeyEvent *event) override;

  private:
    QCompleter *completion_;
    QStandardItemModel *model_;
    bool invoking_ = false;
    void updateMatches();
    void run(const QModelIndex &index);
};
} // namespace ix
