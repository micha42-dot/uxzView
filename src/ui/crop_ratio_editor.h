// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include <QStringList>
class QTableWidget;
class QPushButton;
namespace ix {
class CropRatioEditor final : public QWidget {
    Q_OBJECT
  public:
    explicit CropRatioEditor(const QStringList &ratios, QWidget *parent = nullptr);
    QStringList ratios();
  protected:
    bool eventFilter(QObject *, QEvent *) override;
  private:
    QTableWidget *table_;
    QPushButton *add_, *remove_;
    void append(int width, int height);
    void updateButtons();
};
}
