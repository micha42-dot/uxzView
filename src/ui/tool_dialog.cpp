// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialog_layout.h"
#include "tool_dialog.h"
#include "extension_dialog.h"
#include "core/app_storage.h"
#include "core/extension_storage.h"
#include "core/localization.h"
#include "core/tool_store.h"
#include "core/recognition_input.h"
#include <QtWidgets>
#include <QtConcurrent>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <atomic>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace ix {
namespace {
QString setupTarget(const QString &id) {
    if (id != QStringLiteral("tesseract-ocr") || ToolStore().executable(id).isEmpty() || !ToolStore().languages().isEmpty()) return id;
    const QMap<QString, QString> languages{{QStringLiteral("de"), QStringLiteral("deu")}, {QStringLiteral("en"), QStringLiteral("eng")},
        {QStringLiteral("fr"), QStringLiteral("fra")}, {QStringLiteral("ja"), QStringLiteral("jpn")}, {QStringLiteral("ko"), QStringLiteral("kor")}};
    return QStringLiteral("tessdata-") + languages.value(uiLanguage(), QStringLiteral("eng"));
}
class ToolDialog final : public QDialog {
  public:
    ToolDialog(QWidget *parent, QString id, QImage image, QRect selection)
        : QDialog(parent), id_(std::move(id)), image_(std::move(image)), selection_(selection) {
        setObjectName(QStringLiteral("imageToolDialog"));
        setWindowTitle(id_ == QStringLiteral("zxing-cpp") ? IX_TEXT("QR-/Barcodes lesen") : IX_TEXT("Text erkennen"));
        resize(700, 500);
        auto *layout = dialogLayout(this);
        if (id_ == QStringLiteral("tesseract-ocr")) {
            layout->addWidget(new QLabel(IX_TEXT("Sprachen")));
            languages_ = new QListWidget;
            languages_->setObjectName(QStringLiteral("ocrLanguages"));
            languages_->setMaximumHeight(110);
            const auto selected = QSettings().value(QStringLiteral("tools/ocrLanguages"), QStringList{QStringLiteral("eng"), QStringLiteral("deu")}).toStringList();
            populateLanguages(selected);
            layout->addWidget(languages_);
        }
        status_ = new QLabel;
        status_->setObjectName(QStringLiteral("imageToolStatus"));
        status_->setWordWrap(true);
        layout->addWidget(status_);
        output_ = new QPlainTextEdit;
        output_->setObjectName(QStringLiteral("toolResult"));
        output_->setReadOnly(true);
        layout->addWidget(output_, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        buttons->button(QDialogButtonBox::Close)->setText(IX_TEXT("Schließen"));
        run_ = buttons->addButton(IX_TEXT("Erkennen"), QDialogButtonBox::ActionRole);
        run_->setObjectName(QStringLiteral("runImageTool"));
        abort_ = buttons->addButton(IX_TEXT("Abbrechen"), QDialogButtonBox::ActionRole);
        abort_->setObjectName(QStringLiteral("cancelImageTool"));
        abort_->setEnabled(false);
        manage_ = buttons->addButton(IX_TEXT("Erweiterungen …"), QDialogButtonBox::ActionRole);
        manage_->setObjectName(QStringLiteral("manageToolExtensions"));
        connect(manage_, &QPushButton::clicked, this, [this] {
            const auto selected = selectedLanguages();
            extensionManagerDialog(this, setupTarget(id_));
            populateLanguages(selected);
            const bool engine = !ToolStore().executable(id_).isEmpty();
            const bool language = !languages_ || languages_->count() > 0;
            run_->setEnabled(engine && language);
            if (!engine) status_->setText(IX_TEXT("Erweiterung nicht verfügbar."));
            else if (!language) status_->setText(IX_TEXT("Keine OCR-Sprache ausgewählt."));
            else status_->clear();
        });
        auto *copy = buttons->addButton(IX_TEXT("Kopieren"), QDialogButtonBox::ActionRole);
        auto *save = buttons->addButton(IX_TEXT("Text speichern …"), QDialogButtonBox::ActionRole);
        copy->setObjectName(QStringLiteral("copyToolResult"));
        save->setObjectName(QStringLiteral("saveToolResult"));
        copy->setEnabled(false);
        save->setEnabled(false);
        connect(output_, &QPlainTextEdit::textChanged, this, [this, copy, save] {
            const bool available = !output_->toPlainText().trimmed().isEmpty();
            copy->setEnabled(available);
            save->setEnabled(available);
        });
        connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(output_->toPlainText()); });
        connect(save, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getSaveFileName(this, IX_TEXT("Text speichern …"), {}, QStringLiteral("Text (*.txt)"));
            if (path.isEmpty()) return;
            QSaveFile file(path);
            const auto bytes = output_->toPlainText().toUtf8();
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) status_->setText(IX_TEXT("Text konnte nicht gespeichert werden."));
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(run_, &QPushButton::clicked, this, [this] { start(); });
        connect(abort_, &QPushButton::clicked, this, [this] { cancelled_.store(true); process_.kill(); status_->setText(IX_TEXT("Abgebrochen.")); });
        layout->addWidget(buttons);
        connect(&exporter_, &QFutureWatcher<QString>::finished, this, [this] {
            QString error;
            try { error = exporter_.result(); }
            catch (const std::exception &exception) { error = QString::fromUtf8(exception.what()); }
            catch (...) { error = IX_TEXT("Bildoperation fehlgeschlagen."); }
            if (cancelled_.load()) { finish(IX_TEXT("Abgebrochen.")); return; }
            if (!error.isEmpty()) { finish(error); return; }
            launch();
        });
        connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { collect(); });
        connect(&process_, &QProcess::readyReadStandardError, this, [this] {
            errors_ += process_.readAllStandardError();
            if (errors_.size() > 65536) errors_ = errors_.right(65536);
        });
        connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish(process_.errorString());
        });
        connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
            collect();
            if (cancelled_.load()) { finish(timedOut_ ? IX_TEXT("Zeitlimit überschritten.") : IX_TEXT("Abgebrochen.")); return; }
            if (overflow_) { finish(IX_TEXT("Werkzeugausgabe ist zu groß.")); return; }
            if (exit != QProcess::NormalExit || code != 0) { finish(IX_TEXT("Werkzeug fehlgeschlagen: ") + QString::fromUtf8(errors_).left(2000)); return; }
            if (id_ == QStringLiteral("zxing-cpp")) {
                QJsonParseError error;
                const auto result = QJsonDocument::fromJson(bytes_, &error);
                if (error.error != QJsonParseError::NoError || !result.isArray()) { finish(IX_TEXT("Ungültige Werkzeugausgabe.")); return; }
                QStringList texts;
                for (const auto &entry : result.array()) {
                    const auto item = entry.toObject();
                    texts.append(item.value(QStringLiteral("format")).toString() + QStringLiteral(":\n") + item.value(QStringLiteral("text")).toString());
                }
                output_->setPlainText(texts.join(QStringLiteral("\n\n")));
            } else output_->setPlainText(QString::fromUtf8(bytes_));
            finish(output_->toPlainText().trimmed().isEmpty() ? IX_TEXT("Nichts erkannt.") : IX_TEXT("Erkennung abgeschlossen."));
        });
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, [this] { timedOut_ = true; cancelled_.store(true); process_.kill(); });
        QTimer::singleShot(0, this, [this] { start(); });
    }
    ~ToolDialog() override {
        disconnect(&exporter_, nullptr, this, nullptr);
        disconnect(&process_, nullptr, this, nullptr);
        cancelled_.store(true);
        exporter_.waitForFinished();
        process_.kill();
        if (process_.state() != QProcess::NotRunning) process_.waitForFinished(3000);
    }
  private:
    QString id_;
    QImage image_;
    QRect selection_;
    QLabel *status_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QListWidget *languages_ = nullptr;
    QPushButton *run_ = nullptr, *abort_ = nullptr, *manage_ = nullptr;
    QFutureWatcher<QString> exporter_;
    QProcess process_;
    QTimer timer_;
    std::unique_ptr<QTemporaryDir> job_;
    std::unique_ptr<ToolUse> use_;
    std::atomic_bool cancelled_{false};
    bool overflow_ = false, timedOut_ = false;
    QByteArray bytes_, errors_;
    void populateLanguages(const QStringList &selected) {
        if (!languages_) return;
        languages_->clear();
        const QMap<QString, QString> names{{QStringLiteral("deu"), IX_TEXT("Deutsch")}, {QStringLiteral("eng"), IX_TEXT("Englisch")},
            {QStringLiteral("fra"), IX_TEXT("Französisch")}, {QStringLiteral("jpn"), IX_TEXT("Japanisch")}, {QStringLiteral("kor"), IX_TEXT("Koreanisch")}};
        for (const auto &code : ToolStore().languages()) {
            auto *item = new QListWidgetItem(names.value(code, code), languages_);
            item->setData(Qt::UserRole, code);
            item->setCheckState(selected.contains(code) ? Qt::Checked : Qt::Unchecked);
        }
        if (languages_->count() && selectedLanguages().isEmpty()) languages_->item(0)->setCheckState(Qt::Checked);
    }
    QStringList selectedLanguages() const {
        QStringList codes;
        if (languages_) for (int i = 0; i < languages_->count(); ++i)
            if (languages_->item(i)->checkState() == Qt::Checked) codes.append(languages_->item(i)->data(Qt::UserRole).toString());
        return codes;
    }
    void finish(const QString &message) {
        use_.reset();
        timer_.stop(); run_->setEnabled(true); abort_->setEnabled(false); manage_->setEnabled(true);
        if (languages_) languages_->setEnabled(true);
        status_->setText(localized(message));
    }
    void collect() {
        const auto next = process_.readAllStandardOutput();
        if (bytes_.size() + next.size() > 4 * 1024 * 1024) { overflow_ = true; process_.kill(); }
        else bytes_ += next;
    }
    void start() {
        if (exporter_.isRunning() || process_.state() != QProcess::NotRunning) return;
        ToolStore store;
        if (ExtensionStorage::restartPending(ExtensionStorage::Domain::Tools)) { finish(IX_TEXT("Bitte üxzView zuerst neu starten.")); return; }
        if (store.executable(id_).isEmpty()) {
            finish(IX_TEXT("Erweiterung nicht verfügbar.")); run_->setEnabled(false); return;
        }
        const auto codes = selectedLanguages();
        if (languages_ && codes.isEmpty()) { finish(IX_TEXT("Keine OCR-Sprache ausgewählt.")); return; }
        const auto area = selection_.isEmpty() ? image_.rect() : selection_.intersected(image_.rect());
        const auto root = AppStorage::cacheRoot() + QStringLiteral("/tool-jobs");
        if (!QDir().mkpath(root)) { finish(IX_TEXT("Temporärer Ordner konnte nicht angelegt werden.")); return; }
        job_ = std::make_unique<QTemporaryDir>(root + QStringLiteral("/run-XXXXXX"));
        if (!job_->isValid()) { finish(IX_TEXT("Temporärer Ordner konnte nicht angelegt werden.")); return; }
        QString error;
        use_ = store.acquire(id_, codes, error);
        if (!use_) { finish(error); return; }
        run_->setEnabled(false); abort_->setEnabled(true); manage_->setEnabled(false);
        if (languages_) languages_->setEnabled(false);
        cancelled_.store(false); overflow_ = false; timedOut_ = false;
        bytes_.clear(); errors_.clear(); output_->clear();
        status_->setText(IX_TEXT("Bildbereich wird vorbereitet …"));
        if (languages_) QSettings().setValue(QStringLiteral("tools/ocrLanguages"), codes);
        const auto directory = job_->path();
        const auto image = image_;
        const auto modelFiles = use_->modelFiles();
        if (modelFiles.size() != codes.size()) { finish(IX_TEXT("Sprachdaten konnten nicht vorbereitet werden.")); return; }
        QList<RecognitionLanguage> models;
        for (int i = 0; i < codes.size(); ++i) models.append({codes[i], modelFiles[i]});
        // The destructor cancels and joins this worker before destroying its token/job.
        exporter_.setFuture(QtConcurrent::run([image, area, directory, models, token = &cancelled_] {
            return prepareRecognitionInput(image, area, directory, models, *token);
        }));
    }
    void launch() {
        const auto executable = use_ ? use_->executable() : QString();
        if (executable.isEmpty()) { finish(IX_TEXT("Werkzeug ist nicht verfügbar.")); return; }
        auto environment = QProcessEnvironment::systemEnvironment();
        for (const auto &key : {QStringLiteral("TESSDATA_PREFIX"), QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH")}) environment.remove(key);
        environment.insert(QStringLiteral("OMP_THREAD_LIMIT"), QStringLiteral("4"));
        process_.setProcessEnvironment(environment);
#ifdef Q_OS_WIN
        process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
            arguments->flags |= CREATE_NO_WINDOW;
        });
#endif
        process_.setWorkingDirectory(job_->path());
        QStringList args{QStringLiteral("-json"), QStringLiteral("input.ppm")};
        if (languages_) args = {QStringLiteral("input.ppm"), QStringLiteral("stdout"), QStringLiteral("--tessdata-dir"), QStringLiteral("tessdata"), QStringLiteral("-l"), selectedLanguages().join(QLatin1Char('+')), QStringLiteral("--oem"), QStringLiteral("1")};
        status_->setText(IX_TEXT("Erkennung läuft …"));
        timer_.start(languages_ ? 120000 : 30000);
        process_.start(executable, args, QIODevice::ReadOnly);
    }
};
}
void imageToolDialog(QWidget *parent, const QString &id, const QImage &image, const QRect &selection) {
    if (ExtensionStorage::restartPending(ExtensionStorage::Domain::Tools)) {
        QMessageBox::information(parent, QStringLiteral("üxzView"), IX_TEXT("Bitte üxzView zuerst neu starten."));
        return;
    }
    ToolStore store;
    const auto missing = [&] { return store.executable(id).isEmpty() || (id == QStringLiteral("tesseract-ocr") && store.languages().isEmpty()); };
    while (missing()) {
        const auto target = setupTarget(id);
        if (!extensionManagerDialog(parent, target)) return;
        if (missing() && setupTarget(id) == target) return;
    }
    ToolDialog dialog(parent, id, image, selection);
    dialog.exec();
}
}
