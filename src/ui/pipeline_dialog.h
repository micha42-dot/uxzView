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
class QTableView;
class QTabWidget;
class QPushButton;
class QProgressBar;
class QComboBox;
namespace ix {
class PipelineResultModel;
class BatchProgress;
struct PipelineRunResult {
    QList<PipelineFile> entries;
    QString reportPath, reportError;
};
class PipelineDialog : public QDialog {
    Q_OBJECT
  public:
    explicit PipelineDialog(const QStringList &files, QWidget *parent = nullptr, QString cacheRoot = {});
    ~PipelineDialog() override;

  protected:
    void reject() override;

  private:
    QListWidget *inputs_, *chain_;
    QLineEdit *output_, *pattern_, *search_;
    QComboBox *format_;
    QCheckBox *overwrite_, *loss_, *log_;
    QSpinBox *threads_;
    QLabel *status_, *count_, *validation_;
    QTableView *planTable_;
    PipelineResultModel *planModel_;
    QTabWidget *tabs_;
    QPushButton *start_;
    QProgressBar *progress_;
    QWidget *editor_, *results_;
    QFutureWatcher<PipelineRunResult> runWorker_;
    std::shared_ptr<std::atomic_bool> cancel_;
    std::shared_ptr<BatchProgress> progressMailbox_;
    QString cacheRoot_;
    bool working_ = false, closing_ = false;
    PipelineJob job() const;
    PipelineProfile profile() const;
    void setStatus(const QString &text);
    void setValidation(const QString &text);
    void addStep(PipelineStep step);
    void filterInputs();
    void invalidate(bool clearResults = true);
    void showPlan(const QList<PipelineFile> &plan, bool completed = false);
    void setWorking(bool value);
    void run();
};
} // namespace ix
