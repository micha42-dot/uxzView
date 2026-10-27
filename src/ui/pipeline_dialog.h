// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/batch_pipeline.h"
#include <QDialog>
#include <QFutureWatcher>
class QListWidget;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QLabel;
class QTableWidget;
class QPushButton;
class QProgressBar;
namespace ix {
struct PipelineReview {
    QList<PipelineFile> plan;
    PipelinePreview preview;
};
class PipelineDialog : public QDialog {
    Q_OBJECT
  public:
    explicit PipelineDialog(const QStringList &files, QWidget *parent = nullptr, QString journalRoot = {});
    ~PipelineDialog() override;

  protected:
    void reject() override;

  private:
    QListWidget *inputs_, *chain_;
    QLineEdit *output_, *pattern_;
    QCheckBox *overwrite_, *loss_;
    QSpinBox *threads_;
    QLabel *before_, *after_, *status_;
    QTableWidget *planTable_;
    QPushButton *preview_, *start_;
    QProgressBar *progress_;
    QWidget *editor_;
    QFutureWatcher<PipelineReview> reviewWorker_;
    QFutureWatcher<QList<PipelineFile>> runWorker_;
    std::shared_ptr<std::atomic_bool> cancel_;
    QList<PipelineFile> reviewed_;
    QString journalRoot_;
    bool working_ = false, closing_ = false;
    PipelineJob job() const;
    void addStep(PipelineStep step);
    void clearReview();
    void invalidate();
    void showPlan(const QList<PipelineFile> &plan, bool completed = false);
    void setWorking(bool value);
    void review();
    void run();
};
} // namespace ix
