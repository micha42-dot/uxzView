// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialog_layout.h"
#include "core/app_storage.h"
#include "pipeline_dialog.h"
#include "resize_filters.h"
#include "core/codec_registry.h"
#include "core/localization.h"
#include <QtConcurrent>
#include <QtWidgets>
#include <algorithm>
namespace ix {
namespace {
QString title(const PipelineStep &s) {
    auto p = s.parameters;
    if (s.type == QStringLiteral("resize"))
        return IX_TEXT("Größe ändern: %1 × %2")
            .arg(p.value(QStringLiteral("width")).toInt())
            .arg(p.value(QStringLiteral("height")).toInt());
    if (s.type == QStringLiteral("sharpen"))
        return IX_TEXT("Schärfen: %1").arg(p.value(QStringLiteral("amount")).toInt(20));
    if (s.type == QStringLiteral("watermark"))
        return IX_TEXT("Wasserzeichen: ") +
               p.value(QStringLiteral("text"))
                   .toString(QFileInfo(p.value(QStringLiteral("image")).toString()).fileName());
    return IX_TEXT("Als %1 speichern (Qualität %2)")
        .arg(p.value(QStringLiteral("format")).toString().toUpper())
        .arg(p.value(QStringLiteral("quality")).toInt(95));
}
bool editStep(PipelineStep &step, QWidget *parent) {
    QDialog d(parent);
    d.setWindowTitle(IX_TEXT("Verarbeitungsschritt"));
    auto *v = dialogLayout(&d);
    auto *form = dialogFormLayout();
    v->addLayout(form);
    QJsonObject p = step.parameters;
    auto spin = [&](QString label, int value, int low, int high) {
        auto *s = new QSpinBox;
        s->setRange(low, high);
        s->setValue(value);
        form->addRow(label, s);
        return s;
    };
    std::function<QJsonObject()> values;
    if (step.type == QStringLiteral("resize")) {
        auto *w = spin(IX_TEXT("Maximale Breite:"), p.value(QStringLiteral("width")).toInt(2048), 1, 100000),
             *h = spin(IX_TEXT("Maximale Höhe:"), p.value(QStringLiteral("height")).toInt(2048), 1, 100000);
        auto *proportional = new QCheckBox(IX_TEXT("Seitenverhältnis erhalten"));
        proportional->setChecked(p.value(QStringLiteral("proportional")).toBool(true));
        form->addRow(proportional);
        auto *filter = new QComboBox;
        filter->addItems(resizeFilterLabels());
        filter->setCurrentIndex(std::clamp(p.value(QStringLiteral("filter")).toInt(5), 0, filter->count() - 1));
        form->addRow(IX_TEXT("Filter:"), filter);
        values = [=] {
            return QJsonObject{{QStringLiteral("width"), w->value()},
                               {QStringLiteral("height"), h->value()},
                               {QStringLiteral("proportional"), proportional->isChecked()},
                               {QStringLiteral("filter"), filter->currentIndex()}};
        };
    } else if (step.type == QStringLiteral("sharpen")) {
        auto *amount = spin(IX_TEXT("Stärke:"), p.value(QStringLiteral("amount")).toInt(20), 1, 99);
        values = [=] { return QJsonObject{{QStringLiteral("amount"), amount->value()}}; };
    } else if (step.type == QStringLiteral("watermark")) {
        auto *text = new QLineEdit(p.value(QStringLiteral("text")).toString(IX_TEXT("Wasserzeichen")));
        form->addRow(IX_TEXT("Text:"), text);
        auto *image = new QLineEdit(p.value(QStringLiteral("image")).toString());
        auto *choose = new QPushButton(IX_TEXT("Bild statt Text..."));
        form->addRow(choose, image);
        QObject::connect(choose, &QPushButton::clicked, &d, [=, &d] {
            auto path = QFileDialog::getOpenFileName(&d, IX_TEXT("Wasserzeichen-Bild"), {},
                                                     CodecRegistry().openFilter());
            if (!path.isEmpty())
                image->setText(path);
        });
        auto *size = spin(IX_TEXT("Schriftgröße (Pixel):"), p.value(QStringLiteral("fontSize")).toInt(32), 1,
                          1000),
             *margin = spin(IX_TEXT("Rand (Pixel):"), p.value(QStringLiteral("margin")).toInt(20), 0, 10000);
        auto *opacity = new QDoubleSpinBox;
        opacity->setRange(0, 1);
        opacity->setSingleStep(.05);
        opacity->setValue(p.value(QStringLiteral("opacity")).toDouble(.65));
        form->addRow(IX_TEXT("Deckkraft:"), opacity);
        auto *color = new QLineEdit(p.value(QStringLiteral("color")).toString(QStringLiteral("#ffffff")));
        form->addRow(IX_TEXT("Textfarbe (#RRGGBB):"), color);
        auto *anchor = new QComboBox;
        anchor->addItem(IX_TEXT("Rechts unten"), QStringLiteral("bottomRight"));
        anchor->addItem(IX_TEXT("Mitte"), QStringLiteral("center"));
        anchor->addItem(IX_TEXT("Links oben"), QStringLiteral("topLeft"));
        anchor->setCurrentIndex(std::max(0, anchor->findData(p.value(QStringLiteral("anchor")).toString())));
        form->addRow(IX_TEXT("Position:"), anchor);
        values = [=] {
            return QJsonObject{{QStringLiteral("text"), text->text()},
                               {QStringLiteral("image"), image->text()},
                               {QStringLiteral("fontSize"), size->value()},
                               {QStringLiteral("margin"), margin->value()},
                               {QStringLiteral("opacity"), opacity->value()},
                               {QStringLiteral("color"), color->text()},
                               {QStringLiteral("anchor"), anchor->currentData().toString()}};
        };
    } else {
        auto *format = new QComboBox;
        for (const auto &cap : CodecRegistry().capabilities())
            if (cap.write)
                format->addItem(cap.name, cap.extensions.first());
        format->setCurrentIndex(std::max(
            0, format->findData(p.value(QStringLiteral("format")).toString(QStringLiteral("webp")))));
        form->addRow(IX_TEXT("Format:"), format);
        auto *quality = spin(IX_TEXT("Qualität:"), p.value(QStringLiteral("quality")).toInt(95), 0, 100);
        values = [=] {
            return QJsonObject{{QStringLiteral("format"), format->currentData().toString()},
                               {QStringLiteral("quality"), quality->value()}};
        };
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return false;
    step.parameters = values();
    return true;
}
} // namespace
PipelineDialog::PipelineDialog(const QStringList &files, QWidget *parent, QString journalRoot)
    : QDialog(parent), cancel_(std::make_shared<std::atomic_bool>(false)) {
    journalRoot_ = std::move(journalRoot);
    setObjectName(QStringLiteral("pipelineDialog"));
    setWindowTitle(IX_TEXT("üxzView – Stapelverarbeitung"));
    resize(1050, 780);
    auto *layout = dialogLayout(this);
    editor_ = new QWidget;
    auto *editLayout = new QVBoxLayout(editor_);
    editLayout->setContentsMargins(0, 0, 0, 0);
    auto *top = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    left->addWidget(
        new QLabel(IX_TEXT("Verarbeitungskette – ziehen zum Anordnen, doppelklicken zum Bearbeiten")));
    chain_ = new QListWidget;
    chain_->setObjectName(QStringLiteral("pipelineChain"));
    chain_->setDragDropMode(QAbstractItemView::InternalMove);
    left->addWidget(chain_);
    auto *stepButtons = new QHBoxLayout;
    auto *add = new QPushButton(IX_TEXT("Schritt hinzufügen")),
         *remove = new QPushButton(IX_TEXT("Entfernen"));
    stepButtons->addWidget(add);
    stepButtons->addWidget(remove);
    left->addLayout(stepButtons);
    auto *profiles = new QHBoxLayout;
    auto *save = new QPushButton(IX_TEXT("Profil speichern...")),
         *load = new QPushButton(IX_TEXT("Profil laden..."));
    profiles->addWidget(save);
    profiles->addWidget(load);
    left->addLayout(profiles);
    top->addLayout(left, 1);
    auto *right = new QVBoxLayout;
    inputs_ = new QListWidget;
    inputs_->setObjectName(QStringLiteral("pipelineInputs"));
    inputs_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    inputs_->addItems(files);
    inputs_->setMaximumHeight(110);
    if (inputs_->count())
        inputs_->setCurrentRow(0);
    right->addWidget(inputs_);
    auto *fileButtons = new QHBoxLayout;
    auto *addFiles = new QPushButton(IX_TEXT("Dateien hinzufügen...")),
         *removeFiles = new QPushButton(IX_TEXT("Aus Liste entfernen"));
    fileButtons->addWidget(addFiles);
    fileButtons->addWidget(removeFiles);
    right->addLayout(fileButtons);
    auto *imageHeadings = new QHBoxLayout;
    imageHeadings->addWidget(new QLabel(IX_TEXT("Vorher")));
    imageHeadings->addWidget(new QLabel(IX_TEXT("Nachher (tatsächliches Exportformat)")));
    right->addLayout(imageHeadings);
    auto *images = new QHBoxLayout;
    before_ = new QLabel(IX_TEXT("Vorher"));
    after_ = new QLabel(IX_TEXT("Nachher – echte Exportvorschau"));
    before_->setObjectName(QStringLiteral("pipelineBefore"));
    after_->setObjectName(QStringLiteral("pipelineAfter"));
    for (auto *label : {before_, after_}) {
        label->setFrameShape(QFrame::Box);
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumSize(210, 150);
        label->setMaximumHeight(220);
        images->addWidget(label);
    }
    right->addLayout(images);
    top->addLayout(right, 2);
    editLayout->addLayout(top);
    auto *form = dialogFormLayout();
    output_ = new QLineEdit(files.isEmpty()
                                ? QString()
                                : QFileInfo(files.first()).dir().filePath(QStringLiteral("üxzView-Export")));
    output_->setObjectName(QStringLiteral("pipelineOutput"));
    auto *choose = new QPushButton(IX_TEXT("Zielordner..."));
    form->addRow(choose, output_);
    pattern_ = new QLineEdit(QStringLiteral("{name}_####"));
    pattern_->setObjectName(QStringLiteral("pipelinePattern"));
    form->addRow(IX_TEXT("Name ({name}, ####):"), pattern_);
    threads_ = new QSpinBox;
    threads_->setRange(1, 4);
    threads_->setValue(std::clamp(QSettings().value(QStringLiteral("batch/parallelism"), 2).toInt(), 1, 4));
    form->addRow(IX_TEXT("Parallele Dateien (Speicherbedarf beachten):"), threads_);
    editLayout->addLayout(form);
    overwrite_ =
        new QCheckBox(IX_TEXT("Vorhandene Ziele ersetzen und Originale im internen Papierkorb sichern"));
    loss_ = new QCheckBox(
        IX_TEXT("Exportverluste (z. B. Farbtiefe oder einzelne Seite statt Animation) zulassen"));
    overwrite_->setObjectName(QStringLiteral("pipelineOverwrite"));
    editLayout->addWidget(overwrite_);
    loss_->setObjectName(QStringLiteral("pipelineAllowLoss"));
    editLayout->addWidget(loss_);
    layout->addWidget(editor_);
    planTable_ = new QTableWidget(0, 3);
    planTable_->setObjectName(QStringLiteral("pipelinePlan"));
    planTable_->setHorizontalHeaderLabels(
        {IX_TEXT("Quelle"), IX_TEXT("Ziel"), IX_TEXT("Geplante Aktion / Ergebnis")});
    planTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    planTable_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(planTable_, 1);
    status_ = new QLabel(IX_TEXT("Vor dem Start Datei auswählen und Vorschau / Dateiplan prüfen."));
    status_->setObjectName(QStringLiteral("pipelineStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    progress_ = new QProgressBar;
    progress_->setRange(0, 1);
    layout->addWidget(progress_);
    auto *buttons = new QHBoxLayout;
    preview_ = new QPushButton(IX_TEXT("Vorschau / Dateiplan prüfen"));
    preview_->setObjectName(QStringLiteral("pipelinePreview"));
    start_ = new QPushButton(IX_TEXT("Starten"));
    start_->setObjectName(QStringLiteral("pipelineStart"));
    start_->setEnabled(false);
    auto *close = new QPushButton(IX_TEXT("Schließen"));
    buttons->addWidget(preview_);
    buttons->addStretch();
    buttons->addWidget(start_);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    addStep({QStringLiteral("resize"),
             {{QStringLiteral("width"), 2048},
              {QStringLiteral("height"), 2048},
              {QStringLiteral("proportional"), true}}});
    addStep({QStringLiteral("export"),
             {{QStringLiteral("format"),
               CodecRegistry::canWrite("webp") ? QStringLiteral("webp") : QStringLiteral("png")},
              {QStringLiteral("quality"), 95}}});
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
        if (editStep(s, this)) {
            int row = chain_->count();
            if (s.type != QStringLiteral("export") && row > 0)
                --row;
            auto *item = new QListWidgetItem(title(s));
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
        if (editStep(s, this)) {
            item->setText(title(s));
            item->setData(Qt::UserRole, QJsonObject{{QStringLiteral("type"), s.type},
                                                    {QStringLiteral("parameters"), s.parameters}});
            invalidate();
        }
    });
    connect(chain_->model(), &QAbstractItemModel::rowsMoved, this, [this] { invalidate(); });
    connect(addFiles, &QPushButton::clicked, this, [this] {
        auto files =
            QFileDialog::getOpenFileNames(this, IX_TEXT("Bilddateien"), {}, CodecRegistry().openFilter());
        for (const auto &path : files)
            if (inputs_->findItems(path, Qt::MatchExactly).isEmpty())
                inputs_->addItem(path);
        if (inputs_->currentRow() < 0 && inputs_->count())
            inputs_->setCurrentRow(0);
        invalidate();
    });
    connect(removeFiles, &QPushButton::clicked, this, [this] {
        qDeleteAll(inputs_->selectedItems());
        invalidate();
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
    connect(inputs_, &QListWidget::currentRowChanged, this, [this] { invalidate(); });
    connect(save, &QPushButton::clicked, this, [this] {
        auto p = job().profile;
        auto error = p.validate();
        if (!error.isEmpty()) {
            status_->setText(error);
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
            status_->setText(file.errorString());
        else
            status_->setText(IX_TEXT("Profil gespeichert."));
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
            status_->setText(IX_TEXT("Profil konnte nicht gelesen werden."));
            return;
        }
        QString error;
        auto p = PipelineProfile::fromJson(QJsonDocument::fromJson(file.readAll()).object(), error);
        if (!error.isEmpty()) {
            status_->setText(error);
            return;
        }
        chain_->clear();
        for (auto s : p.steps)
            addStep(s);
        invalidate();
    });
    connect(preview_, &QPushButton::clicked, this, &PipelineDialog::review);
    connect(start_, &QPushButton::clicked, this, &PipelineDialog::run);
    connect(close, &QPushButton::clicked, this, &PipelineDialog::reject);
    connect(&reviewWorker_, &QFutureWatcherBase::finished, this, [this] {
        auto r = reviewWorker_.result();
        setWorking(false);
        if (cancel_->load()) {
            clearReview();
            status_->setText(IX_TEXT("Abgebrochen."));
            if (closing_) QDialog::reject();
            return;
        }
        reviewed_ = r.plan;
        showPlan(reviewed_);
        if (!r.preview.before.isNull())
            before_->setPixmap(QPixmap::fromImage(
                r.preview.before.scaled(320, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        if (!r.preview.after.isNull())
            after_->setPixmap(QPixmap::fromImage(
                r.preview.after.scaled(320, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        status_->setText(
            r.preview.error.isEmpty()
                ? IX_TEXT("Vorschau nach vollständiger Verarbeitung und Export/Decoder-Runde: %1 × %2 "
                          "→ %3 × %4. Ersetzungen werden gesichert.")
                      .arg(r.preview.originalSize.width())
                      .arg(r.preview.originalSize.height())
                      .arg(r.preview.resultSize.width())
                      .arg(r.preview.resultSize.height())
                : localized(r.preview.error));
        bool valid = false;
        for (const auto &e : reviewed_)
            valid |= e.error.isEmpty();
        start_->setEnabled(valid && r.preview.error.isEmpty() && !cancel_->load());
        if (closing_)
            QDialog::reject();
    });
    connect(&runWorker_, &QFutureWatcherBase::finished, this, [this] {
        QList<PipelineFile> entries;
        QString error;
        try { entries = runWorker_.result(); }
        catch (const std::exception &exception) { error = QString::fromUtf8(exception.what()); }
        catch (...) { error = IX_TEXT("Vorgang fehlgeschlagen."); }
        setWorking(false);
        reviewed_.clear();
        showPlan(entries, true);
        int ok = 0;
        for (const auto &e : entries)
            if (e.error.isEmpty())
                ++ok;
        status_->setText(error.isEmpty() ? IX_TEXT("%1 von %2 Dateien verarbeitet. Erfolgreiche Schritte stehen im "
                                 "Datei-Journal und können dort rückgängig gemacht werden.")
                             .arg(ok)
                             .arg(entries.size()) : localized(error));
        start_->setEnabled(false);
        if (closing_)
            QDialog::reject();
    });
}
PipelineDialog::~PipelineDialog() {
    cancel_->store(true);
    reviewWorker_.waitForFinished();
    runWorker_.waitForFinished();
}
void PipelineDialog::reject() {
    if (working_) {
        closing_ = true;
        cancel_->store(true);
        status_->setText(IX_TEXT("Abbruch angefordert – laufende Decoder werden beendet."));
        return;
    }
    QDialog::reject();
}
void PipelineDialog::addStep(PipelineStep s) {
    auto *item = new QListWidgetItem(title(s), chain_);
    item->setData(Qt::UserRole, QJsonObject{{QStringLiteral("type"), s.type},
                                            {QStringLiteral("parameters"), s.parameters}});
}
PipelineJob PipelineDialog::job() const {
    PipelineJob j;
    j.journalRoot = journalRoot_;
    for (int i = 0; i < inputs_->count(); ++i)
        j.inputs.append(inputs_->item(i)->text());
    for (int i = 0; i < chain_->count(); ++i) {
        auto o = chain_->item(i)->data(Qt::UserRole).toJsonObject();
        j.profile.steps.append(
            {o.value(QStringLiteral("type")).toString(), o.value(QStringLiteral("parameters")).toObject()});
    }
    j.outputDirectory = output_->text();
    j.namePattern = pattern_->text();
    j.overwrite = overwrite_->isChecked();
    j.allowLoss = loss_->isChecked();
    j.parallelism = threads_->value();
    return j;
}
void PipelineDialog::clearReview() {
    reviewed_.clear();
    showPlan({});
    before_->clear();
    after_->clear();
    start_->setEnabled(false);
}
void PipelineDialog::invalidate() {
    clearReview();
    status_->setText(IX_TEXT("Einstellungen geändert – Vorschau und Dateiplan erneut prüfen."));
}
void PipelineDialog::setWorking(bool value) {
    working_ = value;
    editor_->setEnabled(!value);
    preview_->setEnabled(!value);
    start_->setEnabled(value);
    start_->setText(value ? IX_TEXT("Abbrechen") : IX_TEXT("Starten"));
    progress_->setRange(value ? 0 : 0, value ? 0 : 1);
}
void PipelineDialog::showPlan(const QList<PipelineFile> &files, bool completed) {
    planTable_->setRowCount(files.size());
    for (int i = 0; i < files.size(); ++i) {
        auto &e = files[i];
        QString status = e.error.isEmpty() ? (completed    ? IX_TEXT("Fertig")
                                              : e.replaces ? IX_TEXT("Wird ersetzt – Sicherung + Datei-Undo")
                                                           : IX_TEXT("Neu anlegen"))
                                           : localized(e.error);
        planTable_->setItem(i, 0, new QTableWidgetItem(QFileInfo(e.input).fileName()));
        planTable_->setItem(i, 1, new QTableWidgetItem(e.output));
        planTable_->setItem(i, 2, new QTableWidgetItem(status));
    }
    planTable_->resizeColumnsToContents();
}
void PipelineDialog::review() {
    if (working_) return;
    clearReview();
    auto j = job();
    if (j.inputs.isEmpty()) {
        status_->setText(IX_TEXT("Bitte Dateien hinzufügen."));
        return;
    }
    auto error = j.profile.validate();
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    cancel_->store(false);
    setWorking(true);
    auto path = j.inputs.value(std::max(0, inputs_->currentRow()));
    auto token = cancel_;
    reviewWorker_.setFuture(QtConcurrent::run([j, path, token] {
        try {
            auto plan = BatchPipeline::plan(j, token.get());
            if (token->load()) return PipelineReview{};
            return PipelineReview{plan, BatchPipeline::preview(j, path, *token, plan)};
        } catch (const std::exception &error) {
            PipelineReview result;
            result.preview.error = QString::fromUtf8(error.what());
            return result;
        } catch (...) {
            PipelineReview result;
            result.preview.error = IX_TEXT("Vorgang fehlgeschlagen.");
            return result;
        }
    }));
}
void PipelineDialog::run() {
    if (working_) {
        cancel_->store(true);
        return;
    }
    if (reviewed_.isEmpty())
        return;
    auto j = job();
    auto plan = reviewed_;
    cancel_->store(false);
    setWorking(true);
    auto token = cancel_;
    QPointer<PipelineDialog> self(this);
    runWorker_.setFuture(QtConcurrent::run([j, plan, token, self] {
        return BatchPipeline::run(j, plan, *token, [self](int n, int total) {
            if (self)
                QMetaObject::invokeMethod(
                    self,
                    [self, n, total] {
                        if (self) {
                            self->progress_->setRange(0, total);
                            self->progress_->setValue(n);
                        }
                    },
                    Qt::QueuedConnection);
        });
    }));
}
} // namespace ix
