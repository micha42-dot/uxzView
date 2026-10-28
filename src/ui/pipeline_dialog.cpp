// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialog_layout.h"
#include "core/app_storage.h"
#include "core/batch_report.h"
#include "pipeline_dialog.h"
#include "batch_progress.h"
#include "pipeline_step_editor.h"
#include "pipeline_result_model.h"
#include "core/codec_registry.h"
#include "core/localization.h"
#include <QtConcurrent>
#include <QtWidgets>
#include <algorithm>
namespace ix {
PipelineDialog::PipelineDialog(const QStringList &files, QWidget *parent, QString journalRoot)
    : QDialog(parent), cancel_(std::make_shared<std::atomic_bool>(false)) {
    journalRoot_ = std::move(journalRoot);
    setObjectName(QStringLiteral("pipelineDialog"));
    setWindowTitle(IX_TEXT("üxzView – Stapelverarbeitung"));
    resize(std::min(1080, screen()->availableGeometry().width() - 48),
           std::min(760, screen()->availableGeometry().height() - 64));
    auto *layout = dialogLayout(this);
    tabs_ = new QTabWidget;
    tabs_->setObjectName(QStringLiteral("pipelineTabs"));
    layout->addWidget(tabs_, 1);
    editor_ = new QWidget;
    tabs_->addTab(editor_, IX_TEXT("Verarbeitung"));
    auto *grid = new QGridLayout(editor_);
    grid->setContentsMargins(12, 12, 12, 12);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    for (int col = 0; col < 4; ++col) grid->setColumnStretch(col, 1);
    grid->setRowStretch(1, 1);
    grid->setRowMinimumHeight(3, 4);
    grid->setRowMinimumHeight(7, 4);
    search_ = new QLineEdit;
    search_->setObjectName(QStringLiteral("pipelineSearch"));
    search_->setPlaceholderText(IX_TEXT("Dateien suchen"));
    search_->setClearButtonEnabled(true);
    format_ = new QComboBox;
    format_->setObjectName(QStringLiteral("pipelineFormat"));
    format_->addItem(IX_TEXT("Alle Formate"), QStringList());
    for (const auto &cap : CodecRegistry().capabilities())
        if (cap.read) format_->addItem(cap.name, cap.extensions);
    grid->addWidget(search_, 0, 0, 1, 3);
    grid->addWidget(format_, 0, 3);
    inputs_ = new QListWidget;
    inputs_->setObjectName(QStringLiteral("pipelineInputs"));
    inputs_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    inputs_->setMinimumHeight(42);
    inputs_->addItems(files);
    if (inputs_->count()) inputs_->setCurrentRow(0);
    grid->addWidget(inputs_, 1, 0, 1, 4);
    auto *addFiles = new QPushButton(IX_TEXT("Dateien hinzufügen..."));
    auto *removeFiles = new QPushButton(IX_TEXT("Aus Liste entfernen"));
    count_ = new QLabel;
    count_->setObjectName(QStringLiteral("pipelineCount"));
    count_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(addFiles, 2, 0);
    grid->addWidget(removeFiles, 2, 1);
    grid->addWidget(count_, 2, 2, 1, 2);
    grid->addWidget(new QLabel(IX_TEXT("Verarbeitungskette")), 4, 0, 1, 4);
    chain_ = new QListWidget;
    chain_->setObjectName(QStringLiteral("pipelineChain"));
    chain_->setDragDropMode(QAbstractItemView::InternalMove);
    chain_->setMinimumHeight(42);
    chain_->setMaximumHeight(76);
    grid->addWidget(chain_, 5, 0, 1, 4);
    auto *add = new QPushButton(IX_TEXT("Schritt hinzufügen"));
    auto *remove = new QPushButton(IX_TEXT("Entfernen"));
    auto *save = new QPushButton(IX_TEXT("Profil speichern..."));
    auto *load = new QPushButton(IX_TEXT("Profil laden..."));
    save->setObjectName(QStringLiteral("pipelineSaveProfile"));
    grid->addWidget(add, 6, 0);
    grid->addWidget(remove, 6, 1);
    grid->addWidget(save, 6, 2);
    grid->addWidget(load, 6, 3);
    output_ = new QLineEdit(files.isEmpty() ? QString()
        : QFileInfo(files.first()).dir().filePath(QStringLiteral("üxzView-Export")));
    output_->setObjectName(QStringLiteral("pipelineOutput"));
    auto *choose = new QPushButton(IX_TEXT("Zielordner..."));
    choose->setObjectName(QStringLiteral("pipelineChooseOutput"));
    grid->addWidget(choose, 8, 0);
    grid->addWidget(output_, 8, 1, 1, 3);
    pattern_ = new QLineEdit(QStringLiteral("{name}_####"));
    pattern_->setObjectName(QStringLiteral("pipelinePattern"));
    threads_ = new QSpinBox;
    threads_->setRange(1, 4);
    threads_->setValue(std::clamp(QSettings().value(QStringLiteral("batch/parallelism"), 2).toInt(), 1, 4));
    grid->addWidget(new QLabel(IX_TEXT("Name ({name}, ####):")), 9, 0);
    grid->addWidget(pattern_, 9, 1);
    grid->addWidget(new QLabel(IX_TEXT("Parallele Dateien:")), 9, 2);
    grid->addWidget(threads_, 9, 3);
    overwrite_ = new QCheckBox(IX_TEXT("Vorhandene Dateien ersetzen (mit Sicherung)"));
    overwrite_->setObjectName(QStringLiteral("pipelineOverwrite"));
    loss_ = new QCheckBox(IX_TEXT("Verluste beim Export zulassen"));
    loss_->setObjectName(QStringLiteral("pipelineAllowLoss"));
    grid->addWidget(overwrite_, 10, 0, 1, 2);
    grid->addWidget(loss_, 10, 2, 1, 2);
    log_ = new QCheckBox(IX_TEXT("Protokoll im Zielordner speichern (CSV)"));
    log_->setObjectName(QStringLiteral("pipelineLog"));
    grid->addWidget(log_, 11, 0, 1, 4);
    validation_ = new QLabel;
    validation_->setObjectName(QStringLiteral("pipelineValidation"));
    validation_->setWordWrap(true);
    validation_->hide();
    grid->addWidget(validation_, 12, 0, 1, 4);
    results_ = new QWidget;
    auto *resultLayout = new QVBoxLayout(results_);
    resultLayout->setContentsMargins(12, 12, 12, 12);
    resultLayout->setSpacing(8);
    planModel_ = new PipelineResultModel(this);
    planTable_ = new QTableView;
    planTable_->setModel(planModel_);
    planTable_->setObjectName(QStringLiteral("pipelinePlan"));
    planTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    planTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    planTable_->verticalHeader()->hide();
    planTable_->setWordWrap(false);
    planTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultLayout->addWidget(planTable_, 1);
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("pipelineStatus"));
    status_->setWordWrap(true);
    status_->hide();
    resultLayout->addWidget(status_);
    progress_ = new QProgressBar;
    progress_->setRange(0, 1);
    progress_->hide();
    resultLayout->addWidget(progress_);
    tabs_->addTab(results_, IX_TEXT("Ergebnis"));
    auto *buttons = new QHBoxLayout;
    start_ = new QPushButton(IX_TEXT("Starten"));
    start_->setObjectName(QStringLiteral("pipelineStart"));
    start_->setEnabled(false);
    auto *close = new QPushButton(IX_TEXT("Schließen"));
    close->setObjectName(QStringLiteral("pipelineClose"));
    buttons->addStretch();
    buttons->addWidget(start_);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(add, &QPushButton::clicked, this, [this] {
        bool ok = false;
        QStringList labels{IX_TEXT("Größe ändern"), IX_TEXT("Schärfen"), IX_TEXT("Wasserzeichen"),
                           IX_TEXT("Speichern")};
        auto label = QInputDialog::getItem(this, IX_TEXT("Schritt hinzufügen"), IX_TEXT("Aktion:"), labels, 0,
                                           false, &ok);
        if (!ok)
            return;
        PipelineStep s{QStringList{QStringLiteral("resize"), QStringLiteral("sharpen"),
                                   QStringLiteral("watermark"),
                                   QStringLiteral("export")}[labels.indexOf(label)],
                       {}};
        if (editPipelineStep(s, this)) {
            int row = chain_->count();
            if (s.type != QStringLiteral("export") && row > 0 &&
                chain_->item(row - 1)->data(Qt::UserRole).toJsonObject()
                    .value(QStringLiteral("type")).toString() == QStringLiteral("export")) --row;
            auto *item = new QListWidgetItem(pipelineStepTitle(s));
            item->setData(Qt::UserRole, QJsonObject{{QStringLiteral("type"), s.type},
                                                    {QStringLiteral("parameters"), s.parameters}});
            chain_->insertItem(row, item);
            invalidate();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        delete chain_->takeItem(chain_->currentRow());
        invalidate();
    });
    connect(chain_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        auto o = item->data(Qt::UserRole).toJsonObject();
        PipelineStep s{o.value(QStringLiteral("type")).toString(),
                       o.value(QStringLiteral("parameters")).toObject()};
        if (editPipelineStep(s, this)) {
            item->setText(pipelineStepTitle(s));
            item->setData(Qt::UserRole, QJsonObject{{QStringLiteral("type"), s.type},
                                                    {QStringLiteral("parameters"), s.parameters}});
            invalidate();
        }
    });
    connect(chain_->model(), &QAbstractItemModel::rowsMoved, this, [this] { invalidate(); });
    connect(addFiles, &QPushButton::clicked, this, [this] {
        QFileDialog picker(this, IX_TEXT("Bilddateien"),
            inputs_->count() ? QFileInfo(inputs_->item(0)->text()).absolutePath() : QString());
        picker.setFileMode(QFileDialog::ExistingFiles);
        picker.setViewMode(QFileDialog::Detail);
        picker.setNameFilters(CodecRegistry().openFilter().split(QStringLiteral(";;")));
        const auto extensions = format_->currentData().toStringList();
        if (!extensions.isEmpty())
            for (const auto &filter : picker.nameFilters())
                if (filter.startsWith(format_->currentText() + QStringLiteral(" ("))) {
                    picker.selectNameFilter(filter); break;
                }
        if (picker.exec() != QDialog::Accepted) return;
        const auto files = picker.selectedFiles();
        QSet<QString> known;
        known.reserve(inputs_->count() + files.size());
        for (int i = 0; i < inputs_->count(); ++i) known.insert(inputs_->item(i)->text());
        for (const auto &path : files) {
            if (known.contains(path)) continue;
            known.insert(path);
            inputs_->addItem(path);
        }
        if (inputs_->currentRow() < 0 && inputs_->count())
            inputs_->setCurrentRow(0);
        filterInputs();
    });
    connect(removeFiles, &QPushButton::clicked, this, [this] {
        for (auto *item : inputs_->selectedItems())
            if (!item->isHidden()) delete item;
        filterInputs();
    });
    connect(choose, &QPushButton::clicked, this, [this] {
        auto dir = QFileDialog::getExistingDirectory(this, IX_TEXT("Zielordner"), output_->text());
        if (!dir.isEmpty())
            output_->setText(dir);
    });
    for (auto *field : {output_, pattern_})
        connect(field, &QLineEdit::textChanged, this, [this] { invalidate(); });
    for (auto *check : {overwrite_, loss_})
        connect(check, &QCheckBox::toggled, this, [this] { invalidate(); });
    connect(search_, &QLineEdit::textChanged, this, &PipelineDialog::filterInputs);
    connect(format_, &QComboBox::currentIndexChanged, this, &PipelineDialog::filterInputs);
    connect(chain_->model(), &QAbstractItemModel::rowsInserted, this, [this] { invalidate(); });
    connect(chain_->model(), &QAbstractItemModel::rowsRemoved, this, [this] { invalidate(); });
    connect(chain_->model(), &QAbstractItemModel::dataChanged, this, [this] { invalidate(); });
    connect(save, &QPushButton::clicked, this, [this] {
        auto p = profile();
        auto error = p.validate();
        if (!error.isEmpty()) {
            setValidation(error);
            return;
        }
        auto folder = AppStorage::dataRoot() +
                      QStringLiteral("/profiles");
        QDir().mkpath(folder);
        auto path = QFileDialog::getSaveFileName(this, IX_TEXT("Kette speichern"),
                                                 folder + QStringLiteral("/pipeline.json"),
                                                 IX_TEXT("üxzView-Profil (*.json)"));
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        auto bytes = QJsonDocument(p.toJson()).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            setValidation(file.errorString());
        else
            setValidation(IX_TEXT("Profil gespeichert."));
    });
    connect(load, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(
            this, IX_TEXT("Kette laden"),
            AppStorage::dataRoot() +
                QStringLiteral("/profiles"),
            IX_TEXT("üxzView-Profil (*.json)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
            setValidation(IX_TEXT("Profil konnte nicht gelesen werden."));
            return;
        }
        QString error;
        auto p = PipelineProfile::fromJson(QJsonDocument::fromJson(file.readAll()).object(), error);
        if (!error.isEmpty()) {
            setValidation(error);
            return;
        }
        chain_->clear();
        for (auto s : p.steps)
            addStep(s);
        invalidate();
    });
    connect(start_, &QPushButton::clicked, this, &PipelineDialog::run);
    connect(close, &QPushButton::clicked, this, &PipelineDialog::reject);
    connect(&runWorker_, &QFutureWatcherBase::finished, this, [this] {
        PipelineRunResult result;
        QString error;
        try { result = runWorker_.result(); }
        catch (const std::exception &exception) { error = QString::fromUtf8(exception.what()); }
        catch (...) { error = IX_TEXT("Vorgang fehlgeschlagen."); }
        setWorking(false);
        const auto &entries = result.entries;
        showPlan(entries, true);
        const auto summary = pipelineSummary(entries);
        const auto delta = summary.inputBytes - summary.outputBytes;
        const auto percent = summary.inputBytes ? 100.0 * delta / summary.inputBytes : 0.0;
        setStatus(error.isEmpty()
            ? IX_TEXT("%1 erfolgreich, %2 fehlgeschlagen. %3 → %4 (Größenänderung: %5 %).")
                .arg(summary.succeeded).arg(summary.failed)
                .arg(uiLocale().formattedDataSize(summary.inputBytes))
                .arg(uiLocale().formattedDataSize(summary.outputBytes))
                .arg(uiLocale().toString(-percent, 'f', 1))
            : localized(error));
        if (cancel_->load()) setStatus(IX_TEXT("Abgebrochen.") + QLatin1Char(' ' ) + status_->text());
        if (!result.reportError.isEmpty()) setStatus(status_->text() + QLatin1Char('\n') + result.reportError);
        else if (!result.reportPath.isEmpty()) setStatus(status_->text() + QLatin1Char('\n') + result.reportPath);
        invalidate(false);
        progress_->setValue(1);
        if (closing_)
            QDialog::reject();
    });
    filterInputs();
}
PipelineDialog::~PipelineDialog() {
    cancel_->store(true);
    runWorker_.waitForFinished();
}
void PipelineDialog::reject() {
    if (working_) {
        closing_ = true;
        cancel_->store(true);
        setStatus(IX_TEXT("Abbruch angefordert – laufende Decoder werden beendet."));
        return;
    }
    QDialog::reject();
}
void PipelineDialog::addStep(PipelineStep s) {
    auto *item = new QListWidgetItem(pipelineStepTitle(s), chain_);
    item->setData(Qt::UserRole, QJsonObject{{QStringLiteral("type"), s.type},
                                            {QStringLiteral("parameters"), s.parameters}});
}
PipelineProfile PipelineDialog::profile() const {
    PipelineProfile profile;
    for (int i = 0; i < chain_->count(); ++i) {
        const auto object = chain_->item(i)->data(Qt::UserRole).toJsonObject();
        profile.steps.append({object.value(QStringLiteral("type")).toString(),
                              object.value(QStringLiteral("parameters")).toObject()});
    }
    return profile;
}
PipelineJob PipelineDialog::job() const {
    PipelineJob j;
    j.journalRoot = journalRoot_;
    for (int i = 0; i < inputs_->count(); ++i)
        if (!inputs_->item(i)->isHidden()) j.inputs.append(inputs_->item(i)->text());
    j.profile = profile();
    j.outputDirectory = output_->text();
    j.namePattern = pattern_->text();
    j.overwrite = overwrite_->isChecked();
    j.allowLoss = loss_->isChecked();
    j.parallelism = threads_->value();
    return j;
}
void PipelineDialog::filterInputs() {
    const auto extensions = format_->currentData().toStringList();
    int visible = 0;
    for (int i = 0; i < inputs_->count(); ++i) {
        auto *item = inputs_->item(i);
        const QFileInfo info(item->text());
        const bool show = info.fileName().contains(search_->text(), Qt::CaseInsensitive) &&
            (extensions.isEmpty() || extensions.contains(info.suffix(), Qt::CaseInsensitive));
        item->setHidden(!show);
        visible += show;
    }
    count_->setText(IX_TEXT("%1 von %2 Dateien").arg(visible).arg(inputs_->count()));
    invalidate();
}
void PipelineDialog::setValidation(const QString &text) {
    validation_->setText(text);
    validation_->setVisible(!text.isEmpty());
}
void PipelineDialog::setStatus(const QString &text) {
    status_->setText(text);
    status_->setVisible(!text.isEmpty());
}
void PipelineDialog::invalidate(bool clearResults) {
    if (working_) return;
    if (clearResults) {
        showPlan({});
        setStatus({});
        progress_->hide();
    }
    const auto error = profile().validate();
    bool hasInput = false;
    for (int i = 0; i < inputs_->count() && !hasInput; ++i) hasInput = !inputs_->item(i)->isHidden();
    start_->setEnabled(hasInput && !output_->text().trimmed().isEmpty() && error.isEmpty());
    if (clearResults) setValidation(chain_->count() && !error.isEmpty() ? localized(error) : QString());
}
void PipelineDialog::setWorking(bool value) {
    working_ = value;
    if (!value) progressMailbox_.reset();
    editor_->setEnabled(!value);
    start_->setEnabled(true);
    start_->setText(value ? IX_TEXT("Abbrechen") : IX_TEXT("Starten"));
    progress_->setRange(0, value ? 0 : 1);
    if (value) { progress_->show(); tabs_->setCurrentWidget(results_); }
}
void PipelineDialog::showPlan(const QList<PipelineFile> &files, bool completed) {
    planModel_->setEntries(files, completed);
}
void PipelineDialog::run() {
    if (working_) {
        cancel_->store(true);
        return;
    }
    auto j = job();
    if (j.inputs.isEmpty() || j.outputDirectory.trimmed().isEmpty() || !j.profile.validate().isEmpty()) return;
    showPlan({});
    setStatus(IX_TEXT("Dateien werden verarbeitet …"));
    cancel_->store(false);
    setWorking(true);
    auto token = cancel_;
    QPointer<PipelineDialog> self(this);
    const bool writeLog = log_->isChecked();
    auto mailbox = std::make_shared<BatchProgress>(int(j.inputs.size()));
    progressMailbox_ = mailbox;
    runWorker_.setFuture(QtConcurrent::run([j, token, self, writeLog, mailbox] {
        const auto plan = BatchPipeline::plan(j, token.get());
        if (self) QMetaObject::invokeMethod(self, [self, plan, mailbox] {
            if (self && self->working_ && self->progressMailbox_ == mailbox) self->showPlan(plan);
        }, Qt::QueuedConnection);
        PipelineRunResult result;
        result.entries = BatchPipeline::run(j, plan, *token, [self, mailbox](int n, int) {
            if (mailbox->publish(n) && self)
                QMetaObject::invokeMethod(
                    self,
                    [self, mailbox] {
                        const auto completed = mailbox->take();
                        if (self && self->working_ && self->progressMailbox_ == mailbox) {
                            self->progress_->setRange(0, mailbox->total());
                            self->progress_->setValue(completed);
                        }
                    },
                    Qt::QueuedConnection);
        });
        if (writeLog && !result.entries.isEmpty())
            result.reportError = writePipelineReport(result.entries, j.outputDirectory, result.reportPath);
        return result;
    }));
}
} // namespace ix
