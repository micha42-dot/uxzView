// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/file_operations.h"
#include "core/localization.h"
#include "core/preview_service.h"
#include "main_window.h"
#include <QScopeGuard>
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
bool MainWindow::confirmDiscard(bool changingImage) {
    if (!document_.modified ||
        (changingImage && !QSettings().value(QStringLiteral("navigation/askToSave"), false).toBool()))
        return true;
    slideshow_.stop();
    stopAnimation();
    auto answer = QMessageBox::question(
        this, QStringLiteral("üxzView"), IX_TEXT("Änderungen am aktuellen Bild speichern?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return saveImage();
    return true;
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
        path = QFileDialog::getSaveFileName(
            this, IX_TEXT("Speichern unter"),
            path.isEmpty() ? QSettings().value(QStringLiteral("files/directory")).toString() +
                                 QStringLiteral("/Bild.png")
                           : suggested,
            registry_.saveFilter(), &selected);
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).suffix().isEmpty()) {
            auto match = QRegularExpression(QStringLiteral("\\*\\.([a-zA-Z0-9]+)")).match(selected);
            path += QLatin1Char('.') + (match.hasMatch() ? match.captured(1) : QStringLiteral("png"));
        }
    }
    const auto targetRevision = QFileInfo::exists(path) ? imageFileKey(path) : QString();
    if (!targetRevision.isEmpty() && (!dialog || QFileInfo(path).absoluteFilePath() == document_.path)) {
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
        auto pixels = output.pixels.copy(selectedArea);
        inheritImageProperties(output.pixels, pixels);
        output.pixels = std::move(pixels);
        output.modified = true;
    }
    auto format = QFileInfo(path).suffix().toLower().toLatin1();
    auto losses = CodecRegistry::exportLosses(output, format);
    if (!losses.isEmpty() &&
        QMessageBox::warning(this, IX_TEXT("Hinweise zum Ausgabeformat"),
                             losses.join(QStringLiteral("\n\n")) + IX_TEXT("\n\nTrotzdem speichern?"),
                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return false;
    QProgressDialog progress(IX_TEXT("Bild wird gespeichert …"), QString(), 0, 0, this);
    progress.setCancelButton(nullptr);
    progress.setWindowModality(Qt::ApplicationModal);
    struct SaveResult { QString error; SourceFileState source; };
    QFutureWatcher<SaveResult> watcher;
    connect(&watcher, &QFutureWatcherBase::finished, &progress, &QProgressDialog::accept);
    auto registry = registry_;
    watcher.setFuture(QtConcurrent::run([=] {
        SaveResult result{registry.save(output, path, format, 95, true, targetRevision), {}};
        if (result.error.isEmpty() && !selection)
            result.source = observeSource(path);
        return result;
    }));
    progress.exec();
    const auto result = watcher.result();
    if (!result.error.isEmpty()) {
        showError(result.error);
        return false;
    }
    updateRecent(QStringLiteral("saved"), path);
    if (!selection) {
        const auto &metadata = output.metadata;
        const bool writtenMetadata = metadata && metadata->data && metadataWritable(format) &&
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
        QString next;
        if (files_.size() > 1)
            next = files_.at((fileIndex_ + 1) % files_.size());
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
        progress.exec();
        const auto result = watcher.future().takeResult();
        if (result.applied) {
            ImageLoadCache::invalidate({source});
            installDocument({});
            if (!next.isEmpty() && !deferredOpen_)
                openPath(next, 0, false);
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
    progress.exec();
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
}
} // namespace ix
