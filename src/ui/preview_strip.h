// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/preview_service.h"
#include <QWidget>
class QHBoxLayout;
class QToolButton;
class QLabel;
namespace ix {
class PreviewStrip : public QWidget {
    Q_OBJECT
  public:
    explicit PreviewStrip(QWidget *parent = nullptr);
    void setFiles(QStringList files, int current);
    void stop();
    void applySettings();
  signals:
    void openRequested(QString path);

  private:
    PreviewService service_;
    QHBoxLayout *row_;
    QLabel *currentMarker_;
    int previous_ = 2, next_ = 2;
    QStringList files_;
    int current_ = -1, generation_ = 0;
    bool flatButtons_ = false, needsRefresh_ = false;
    QHash<QString, QToolButton *> buttons_;
    void rebuild(bool reset = false);
};
} // namespace ix
