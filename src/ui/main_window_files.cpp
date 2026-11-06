// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/file_operations.h"
#include "core/image_export_policy.h"
#include "core/localization.h"
#include "core/preview_service.h"
#include "main_window.h"
#include "worker_wait.h"
#include "file_navigation.h"
#include <QScopeGuard>
#include <QSettings>
#include <QtConcurrent>
#include <QtWidgets>
namespace ix {
namespace {
struct SourceFileState {
    QString revision;
    QDateTime modified;
    qint64 bytes = 0;
};
SourceFileState observeSource(const QString &path) {
    const QFileInfo file(path);
    return {imageFileKey(path), file.lastModified(), file.size()};
}
void applySource(ImageDocument &document, const SourceFileState &source) {
    document.sourceRevision = source.revision;
    document.sourceModified = source.modified;
    document.fileBytes = source.bytes;
}
} // namespace
bool MainWindow::confirmDiscard() {
    if (!document_.modified ||
        !QSettings().value(QStringLiteral("navigation/askToSave"), true).toBool())
        return true;
    slideshow_.stop();
    stopAnimation();
    auto answer = QMessageBox::question(
        this, QStringLiteral("üxzView"), IX_TEXT("Änderungen am aktuellen Bild speichern?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return saveImage();
    return answer == QMessageBox::Discard;
}
bool MainWindow::saveImage(bool selection, bool forceDialog) {
    if (document_.empty() || activity_.busy())
        return false;
    activity_.begin(DocumentActivity::Phase::Saving);
    refreshState();
    auto release = qScopeGuard([this] {
        activity_.finish();
        refreshState();
    });
    stopAnimation();
    ImageDocument output = document_;
    const QRect selectedArea = view_->selection();
    QString path = output.path;
    QString selected = QStringLiteral("PNG (*.png)");
    const bool writable = CodecRegistry::canWrite(QFileInfo(path).suffix().toLatin1());
    bool dialog = forceDialog || path.isEmpty() || !writable;
    if (dialog) {
        QString suggested = path;
        if (!path.isEmpty() && !writable)
            suggested = QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName() +
                                                       QStringLiteral(".png"));
        // A single app-side question below covers Save, Save As and externally
        // changed files; the native chooser must not confirm overwrites itself.
        path = QFileDialog::getSaveFileName(
            this, IX_TEXT("Speichern unter"),
            path.isEmpty() ? QSettings().value(QStringLiteral("files/directory")).toString() +
                                 QStringLiteral("/Bild.png")
                           : suggested,
            registry_.saveFilter(), &selected, QFileDialog::DontConfirmOverwrite);
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).suffix().isEmpty()) {
            auto match = QRegularExpression(QStringLiteral("\\*\\.([a-zA-Z0-9]+)")).match(selected);
            path += QLatin1Char('.') + (match.hasMatch() ? match.captured(1) : QStringLiteral("png"));
        }
    }
    const auto targetRevision = QFileInfo::exists(path) ? imageFileKey(path) : QString();
    // The chooser above never confirms overwrites, so ask exactly once here,
    // including Save As onto a different existing file.
    if (!targetRevision.isEmpty()) {
        const bool conflict = !document_.sourceRevision.isEmpty() &&
                              QFileInfo(path).absoluteFilePath() == document_.path &&
                              targetRevision != document_.sourceRevision;
        const auto question = conflict
            ? IX_TEXT("Die Datei wurde seit dem Öffnen auf dem Datenträger geändert. "
                      "Die neuere Datei trotzdem überschreiben?\n%1").arg(path)
            : IX_TEXT("Vorhandene Datei überschreiben?\n%1").arg(path);
        if (QMessageBox::question(this, IX_TEXT("Speichern"), question) !=
            QMessageBox::Yes)
            return false;
    }
    if (selection) {
        // Selection copies can themselves be multi-GB mapped images. Prepare
        // original pixels in a worker before calculating export losses.
        struct SelectionResult { QImage pixels; QString error; };
        auto result = runGuiWorker([pixels = output.pixels, selectedArea] {
            try {
                return SelectionResult{copyImageRegion(pixels, selectedArea), {}};
            } catch (const std::exception &error) {
                return SelectionResult{{}, localized(QString::fromUtf8(error.what()))};
            }
        });
        if (!result.error.isEmpty()) { showError(result.error); return false; }
        output.pixels = std::move(result.pixels);
        output.modified = true;
    }
    auto format = QFileInfo(path).suffix().toLower().toLatin1();
    auto losses = CodecRegistry::exportLosses(output, format);
    if (!losses.isEmpty() &&
        QMessageBox::warning(this, IX_TEXT("Hinweise zum Ausgabeformat"),
                             losses.join(QStringLiteral("\n\n")) + IX_TEXT("\n\nTrotzdem speichern?"),
                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return false;
    // Progress windows stay silent for fast writes. Saving is cancellable: the
    // cancel flag reaches CodecRegistry::save through beforeCommit, so a click
    // discards the staged QSaveFile instead of publishing a partial export.
    const QString abortText = IX_TEXT("Abgebrochen.");
    auto cancelSave = std::make_shared<std::atomic_bool>(false);
    QProgressDialog progress(IX_TEXT("Bild wird gespeichert …"), IX_TEXT("Abbrechen"), 0, 0, this);
    progress.setWindowModality(Qt::ApplicationModal);
    connect(&progress, &QProgressDialog::canceled, &progress, [cancelSave] { cancelSave->store(true); });
    struct SaveResult { QString error; SourceFileState source; };
    QFutureWatcher<SaveResult> watcher;
    connect(&watcher, &QFutureWatcherBase::finished, &progress, &QProgressDialog::accept);
    auto registry = registry_;
    watcher.setFuture(QtConcurrent::run([=] {
        SaveResult result{registry.save(output, path, format, 95, true, targetRevision,
                                        [cancelSave, abortText] {
                                            return cancelSave->load() ? abortText : QString();
                                        }),
                          {}};
        if (result.error.isEmpty() && !selection)
            result.source = observeSource(path);
        return result;
    }));
    waitForGuiWorker(watcher, progress, true);
    const auto result = watcher.future().takeResult();
    if (!result.error.isEmpty()) {
        // A user cancellation is not an error: the document keeps its edits.
        if (result.error == abortText)
            return false;
        showError(result.error);
        return false;
    }
    updateRecent(QStringLiteral("saved"), path);
    if (!selection) {
        const auto &metadata = output.metadata;
        const bool writtenMetadata = !exportNeedsStreaming(output.pixels) &&
                                     metadata && metadata->data && metadataWritable(format) &&
                                     (metadata->hasExif || metadata->hasIptc || metadata->hasXmp);
        document_.path = QFileInfo(path).absoluteFilePath();
        document_.modified = false;
        document_.page = 0;
        document_.pageCount = 1;
        document_.animated = false;
        document_.delayMs = 100;
        document_.loopCount = 0;
        document_.format = format;
        document_.sourceHasExif = writtenMetadata;
        applySource(document_, result.source);
        history_.updateSources([&](ImageDocument &d) {
            d.modified = true;
            d.path = document_.path;
            d.format = document_.format;
            d.page = 0;
            d.pageCount = 1;
            d.animated = false;
            d.delayMs = 100;
            d.loopCount = 0;
            applySource(d, result.source);
            d.sourceHasExif = writtenMetadata;
        }, true);
        refreshFiles();
        refreshState();
        updateStatus();
    }
    return true;
}
void MainWindow::fileOperation(const QString &op) {
    if (document_.path.isEmpty() || activity_.busy())
        return;
    if (op == QStringLiteral("delete") && !confirmDiscard())
        return;
    activity_.begin(DocumentActivity::Phase::Files);
    refreshState();
    auto release = qScopeGuard([this] {
        activity_.finish();
        refreshState();
    });
    QString source = document_.path;
    if (op == QStringLiteral("delete")) {
        const auto revision = imageFileKey(source);
        if (QMessageBox::question(
                this, IX_TEXT("Löschen"),
                IX_TEXT("Datei in den Papierkorb des Betriebssystems verschieben?\n%1").arg(source)) !=
            QMessageBox::Yes)
            return;
        const auto next = nextPathAfterRemoval(files_, fileIndex_, source);
        const auto previous = previousPathAfterRemoval(files_, fileIndex_, source);
        const bool stayEmpty =
            afterDeleteStaysEmpty(QSettings().value(QStringLiteral("navigation/afterDelete"), 1).toInt());
        QProgressDialog progress(IX_TEXT("Datei wird in den Systempapierkorb verschoben …"), {}, 0, 0,
                                 this);
        progress.setCancelButton(nullptr);
        progress.setWindowModality(Qt::WindowModal);
        QFutureWatcher<FileOperations::OperationResult> watcher;
        connect(&watcher, &QFutureWatcherBase::finished, &progress, &QDialog::accept);
        watcher.setFuture(
            QtConcurrent::run([source, revision] {
                return FileOperations().perform(QStringLiteral("delete"), source, {}, revision);
            }));
        waitForGuiWorker(watcher, progress);
        const auto result = watcher.future().takeResult();
        if (result.applied) {
            ImageLoadCache::invalidate({source});
            if (stayEmpty) {
                // Show the deliberate black state instead of the following image.
                // The document keeps its path, so the folder listing (and the
                // status bar, which now names the removed file) survive; the first
                // navigation resumes from the slot this file held.
                ImageDocument removed;
                removed.path = source;
                installDocument(std::move(removed));
                // installDocument() ends any previous black state, so the resume
                // slot for this removal is recorded afterwards.
                removedDocumentPath_ = source;
                resumeNextPath_ = next;
                resumePreviousPath_ = previous;
                // The install above ran before the slot was known, so the status
                // bar still says "unreadable file"; refresh it for "deleted".
                updateStatus();
            } else {
                installDocument({});
                if (!next.isEmpty() && !deferredOpen_)
                    openPath(next, 0, false);
            }
        }
        if (!result.error.isEmpty()) showError(result.error);
        return;
    }
    QString target;
    if (op == QStringLiteral("rename")) {
        bool ok = false;
        QString name = QInputDialog::getText(this, IX_TEXT("Umbenennen"), IX_TEXT("Neuer Dateiname:"),
                                             QLineEdit::Normal, QFileInfo(source).fileName(), &ok);
        if (!ok)
            return;
        if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
            showError(IX_TEXT("Ungültiger Dateiname."));
            return;
        }
        target = QFileInfo(source).dir().absoluteFilePath(name);
    } else {
        auto directory = QFileDialog::getExistingDirectory(
            this, op == QStringLiteral("move") ? IX_TEXT("Verschieben") : IX_TEXT("Kopieren"),
            QFileInfo(source).absolutePath());
        if (directory.isEmpty())
            return;
        target = QDir(directory).filePath(QFileInfo(source).fileName());
    }
    if (QFileInfo::exists(target)) {
        showError(IX_TEXT("Zieldatei existiert bereits."));
        return;
    }
    QProgressDialog progress(IX_TEXT("Datei wird verarbeitet …"), QString(), 0, 0, this);
    progress.setCancelButton(nullptr);
    progress.setWindowModality(Qt::ApplicationModal);
    struct FileResult { FileOperations::OperationResult operation; bool sourceUnchanged; SourceFileState target; };
    QFutureWatcher<FileResult> watcher;
    connect(&watcher, &QFutureWatcherBase::finished, &progress, &QDialog::accept);
    const auto revision = document_.sourceRevision;
    watcher.setFuture(QtConcurrent::run([=] {
        // Check after the chooser/name dialog and after worker scheduling.
        // Another editor may have changed the source while the dialog was open.
        auto operation = FileOperations().perform(
            op == QStringLiteral("fileCopy") ? QStringLiteral("copy") : QStringLiteral("move"), source, target);
        FileResult result{operation, false, {}};
        if (operation.applied && op != QStringLiteral("fileCopy")) {
            result.target = observeSource(target);
            result.sourceUnchanged = operation.sourceRevision == revision &&
                                     operation.targetRevision == result.target.revision;
        }
        return result;
    }));
    waitForGuiWorker(watcher, progress);
    const auto result = watcher.future().takeResult();
    if (!result.operation.applied) {
        if (!result.operation.error.isEmpty()) showError(result.operation.error);
        return;
    }
    ImageLoadCache::invalidate(op == QStringLiteral("fileCopy") ? QStringList{target} : QStringList{source, target});
    if (op != QStringLiteral("fileCopy")) {
        history_.updateSources([&](ImageDocument &d) { d.path = target; });
        document_.path = target;
        if (result.sourceUnchanged) {
            applySource(document_, result.target);
            history_.updateSources([&](ImageDocument &entry) { applySource(entry, result.target); });
        } else
            reconcileFilePath();
        updateStatus();
    }
    refreshFiles(true);
    if (!result.operation.error.isEmpty()) showError(result.operation.error);
    if (!result.operation.warning.isEmpty())
        QMessageBox::warning(this, QStringLiteral("üxzView"), localized(result.operation.warning));
}
} // namespace ix
