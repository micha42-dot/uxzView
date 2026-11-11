// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/file_operations.h"
#include "core/image_export_policy.h"
#include "core/localization.h"
#include "core/preview_service.h"
#include "core/save_formats.h"
#include "core/save_options.h"
#include "main_window.h"
#include "save_options_dialog.h"
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
        !QSettings().value(QStringLiteral("navigation/askToSave"), false).toBool())
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
    if (!selection && !forceDialog && !document_.modified && !document_.path.isEmpty() &&
        canonicalSaveFormat(document_.format) == "jpeg" && QFileInfo::exists(document_.path)) return true;
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
    const auto formats = writableSaveFormats(registry_);
    const QSettings settings;
    auto target = preferredSaveTarget(path, output.format, formats,
        settings.value(QStringLiteral("save/preferOriginal"), true).toBool(),
        settings.value(QStringLiteral("save/format"), QStringLiteral("png")).toByteArray());
    if (target.format.id.isEmpty()) { showError(IX_TEXT("Kein schreibbares Bildformat verfügbar.")); return false; }
    QString selected = target.format.filter();
    bool dialog = forceDialog || target.needsDialog;
    SaveOptions saveOptions = loadSaveOptions(target.format.id);
    if (dialog) {
        QString suggested = target.suggestedPath;
        if (suggested.isEmpty()) suggested = QDir(settings.value(QStringLiteral("files/directory"), QDir::homePath()).toString())
            .filePath(QStringLiteral("Bild.") + target.format.suffix());
        // Native file dialog (unchanged standard behavior). The compression
        // companion is a separate tool window tracked next to it.
        // Fast path: reuse the already computed format list for the filter
        // string instead of scanning all codec capabilities a second time
        // inside saveFilter().
        QStringList allFilters;
        allFilters.reserve(formats.size());
        for (const auto &f : formats)
            allFilters.append(f.filter());
        QFileDialog chooser(this, IX_TEXT("Speichern unter"), suggested, allFilters.join(QStringLiteral(";;")));
        chooser.setAcceptMode(QFileDialog::AcceptSave);
        chooser.setFileMode(QFileDialog::AnyFile);
        chooser.setOption(QFileDialog::DontConfirmOverwrite);
        chooser.selectNameFilter(selected);
        chooser.setDefaultSuffix(target.format.suffix());
        SaveOptionsDialog companion(output, &chooser);
        companion.setRegistry(&registry_);
        companion.setProfiles(loadSaveProfiles());
        companion.setFormat(target.format.id);
        saveOptions = companion.options();
        // Position once beside the chooser (or at the remembered spot) and
        // then leave the companion alone: the user can move it freely, it
        // never snaps back. No follow timer, no move/resize event filter —
        // both fought the user's own dragging (visible jump-back).
        auto placeCompanion = [&] {
            companion.adjustSize();
            const QPoint remembered =
                settings.value(QStringLiteral("save/companionPos")).toPoint();
            if (!remembered.isNull()) {
                companion.move(remembered);
                return;
            }
            // The chooser has no valid frame geometry before it is shown;
            // defer the first placement until it is visible.
            const auto frame = chooser.frameGeometry();
            if (!frame.isValid())
                return;
            QPoint pos = frame.topRight() + QPoint(8, 0);
            if (pos.x() + companion.width() > screen.right()) // Fall back to the left side.
                pos = {frame.left() - companion.width() - 8, frame.top()};
            pos.setY(std::clamp(pos.y(), screen.top(), std::max(screen.top(), screen.bottom() - companion.height())));
            pos.setX(std::clamp(pos.x(), screen.left(), std::max(screen.left(), screen.right() - companion.width())));
            companion.move(pos);
        };
        auto previous = target.format;
        const auto formatForFilter = [&](const QString &filter) -> QList<SaveFormat>::const_iterator {
            // Match by canonical id: Qt may list the same codec under a
            // different label or extension order (e.g. JFIF vs JPEG).
            const auto byFilter = std::find_if(formats.cbegin(), formats.cend(),
                                               [&](const auto &f) { return f.filter() == filter; });
            if (byFilter != formats.cend())
                return byFilter;
            // Fallback: first extension token of the filter ("*.jpg ..."),
            // matched canonically.
            const auto tokens = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const auto &token : tokens) {
                auto cleaned = token;
                cleaned.remove(QLatin1Char('(')).remove(QLatin1Char(')'));
                if (cleaned.startsWith(QStringLiteral("*."))) {
                    const auto id = canonicalSaveFormat(cleaned.sliced(2).toLatin1());
                    const auto byId = std::find_if(formats.cbegin(), formats.cend(),
                                                   [&](const auto &f) { return f.id == id; });
                    if (byId != formats.cend())
                        return byId;
                }
            }
            return formats.cend();
        };
        connect(&chooser, &QFileDialog::filterSelected, &chooser, [&](const QString &filter) {
            const auto next = formatForFilter(filter);
            if (next == formats.cend()) return;
            const auto name = chooser.selectedFiles().value(0);
            if (next->id != previous.id && !name.isEmpty() && canonicalSaveFormat(QFileInfo(name).suffix().toLatin1()) == previous.id)
                chooser.selectFile(QFileInfo(name).dir().filePath(QFileInfo(name).completeBaseName() + QLatin1Char('.') + next->suffix()));
            chooser.setDefaultSuffix(next->suffix());
            previous = *next;
        });
        connect(&chooser, &QFileDialog::filterSelected, &companion, [&](const QString &filter) {
            const auto next = formatForFilter(filter);
            if (next == formats.cend())
                return;
            if (next->id != companion.options().format)
                companion.setFormat(next->id);
            saveOptions = companion.options();
        });
        connect(&companion, &SaveOptionsDialog::optionsChanged, &chooser,
                [&](const SaveOptions &updated) {
                    saveOptions = updated;
                    if (updated.format != target.format.id) {
                        const auto match = std::find_if(formats.cbegin(), formats.cend(),
                                                        [&](const auto &f) { return f.id == updated.format; });
                        if (match != formats.cend())
                            chooser.selectNameFilter(match->filter());
                    }
                });
        // No placer event filter and no follow timer: both re-moved the window
        // while the user was dragging it. Remember a user-moved position for
        // the next save instead.
        connect(&companion, &SaveOptionsDialog::movedByUser, &chooser,
                [&](const QPoint &p) { QSettings().setValue(QStringLiteral("save/companionPos"), p); });
        // The companion is shown alongside the chooser when enabled; closing
        // it via its own close button only hides it for this save, the file
        // dialog keeps running with the last options.
        // When the companion is disabled, the stored options apply silently.
        if (saveCompanionEnabled())
            companion.show();
        else
            companion.hide();
        // Place once the chooser is visible (valid frame geometry). After
        // that the companion is never re-moved programmatically.
        QTimer::singleShot(0, &chooser, [&] {
            if (companion.isVisible() &&
                settings.value(QStringLiteral("save/companionPos")).toPoint().isNull())
                placeCompanion();
        });
        // The native dialog runs its own modal loop; the companion is a child
        // tool window and stays interactive while it is open.
        const bool accepted = chooser.exec() == QDialog::Accepted;
        if (!accepted) return false;
        saveOptions = companion.options();
        target = selectedSaveTarget(chooser.selectedFiles().value(0), chooser.selectedNameFilter(), formats);
        path = target.suggestedPath;
        if (path.isEmpty()) return false;
        saveOptions.format = target.format.id;
        // Single persist (one fsync): the intermediate store before target
        // resolution wrote the same payload twice.
        storeSaveOptions(saveOptions);
        chooser.selectNameFilter(target.format.filter());
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
    const auto format = target.format.id;
    auto registry = registry_;
    CodecRegistry::EncodeOptions encode;
    if (!dialog) {
        // No companion shown: reuse the stored/last settings for this format.
        saveOptions = loadSaveOptions(format);
        saveOptions.format = format;
    }
    encode.quality = saveOptions.targetSizeEnabled && saveOptions.autoQuality >= 0
                         ? saveOptions.autoQuality
                         : saveOptions.quality;
    encode.progressive = saveOptions.progressive;
    encode.noSubsampling = saveOptions.noSubsampling;
    encode.interlaced = saveOptions.interlaced;
    encode.compressionLevel = format == "png" ? saveOptions.pngLevel : -1;
    encode.keepExif = saveOptions.keepExif;
    encode.keepIptc = saveOptions.keepIptc;
    encode.keepXmp = saveOptions.keepXmp;
    encode.keepIcc = saveOptions.keepIcc;
    // Ask mode: resolve transparency handling with the user before encoding.
    // White flattens (legacy default), Keep preserves pixels for formats
    // that store alpha; either choice is then applied silently.
    // The scan short-circuits on format first, so the (sampled, cheap) pixel
    // check only runs when it can matter.
    if (saveOptions.alphaMode == SaveOptions::AlphaMode::Ask &&
        format != "jpeg" && !alphaWritable(format) && imageHasRealTransparency(output.pixels)) {
        QMessageBox ask(this);
        ask.setWindowTitle(IX_TEXT("Transparenz"));
        ask.setText(IX_TEXT("Das Bild enthält Transparenz, die dieses Format nicht speichern kann."));
        ask.setInformativeText(IX_TEXT("Auf Weiß reduzieren oder Originalpixel behalten?"));
        auto *whiteButton = ask.addButton(IX_TEXT("Auf Weiß reduzieren"), QMessageBox::AcceptRole);
        auto *keepButton = ask.addButton(IX_TEXT("Pixel behalten"), QMessageBox::AcceptRole);
        ask.addButton(QMessageBox::Cancel);
        ask.setDefaultButton(whiteButton);
        ask.exec();
        const auto clicked = ask.clickedButton();
        if (clicked != whiteButton && clicked != keepButton)
            return false;
        saveOptions.alphaMode = clicked == whiteButton ? SaveOptions::AlphaMode::White
                                                       : SaveOptions::AlphaMode::Keep;
        storeSaveOptions(saveOptions);
    }
    encode.alphaWhite = saveOptions.alphaMode == SaveOptions::AlphaMode::Keep ? 0 : 1;
    // Losses the user explicitly opted out of (deselected metadata) are not
    // warned about; the global switch still suppresses everything.
    auto losses = CodecRegistry::exportLosses(output, format, &encode);
    const QSettings lossSettings;
    if (!lossSettings.value(QStringLiteral("save/warnLosses"), true).toBool())
        losses.clear();
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
    watcher.setFuture(QtConcurrent::run([=] {
        // Target-size optimization runs first (memory buffers only, never the
        // destination). The final save below validates the actual file size.
        // Cancellation flows through cancelSave, shared with the progress
        // dialog, so aborting the save also aborts the optimization.
        // A fresh companion result is reused only for full-image saves; a
        // selection crops different pixels, so it is always re-optimized.
        int effectiveQuality = encode.quality;
        if (saveOptions.targetSizeEnabled &&
            (selection || saveOptions.autoQuality < 0)) {
            auto optimized = optimizeForTargetSize(
                output, saveOptions, registry, {},
                *static_cast<const std::atomic_bool *>(cancelSave.get()));
            if (!optimized.quality) {
                SaveResult failed{optimized.error, {}};
                return failed;
            }
            effectiveQuality = *optimized.quality;
        }
        auto effective = encode;
        effective.quality = effectiveQuality;
        SaveResult result{registry.saveWithOptions(output, path, format, effective, true,
                                                   targetRevision,
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
        const bool wrap = QSettings().value(QStringLiteral("navigation/wrap"), true).toBool();
        const auto next = nextPathAfterRemoval(files_, fileIndex_, source, wrap);
        const auto nextInOrder = nextPathAfterRemoval(files_, fileIndex_, source, false);
        const auto previous = previousPathAfterRemoval(files_, fileIndex_, source);
        const auto removedIndex = fileIndex_;
        const bool stayEmpty =
            afterDeleteStaysEmpty(QSettings().value(QStringLiteral("navigation/afterDelete"), 1).toInt());
        // The trash move is a filesystem round trip with its own progress window.
        // The black state must not wait for it, so it is shown right after the
        // confirmation; a failed move restores exactly what was on screen. The
        // "open next image" variant keeps the current picture until the move is done.
        const QImage shownImage = view_->image();
        const QRect shownSelection = view_->selection();
        const QSize shownAspect = view_->selectionAspectRatio();
        const double shownZoom = view_->zoom();
        const auto shownFit = view_->fit();
        if (stayEmpty) {
            view_->setUnavailable(true);
            view_->setImage({});
        }
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
            // Keep the folder and removed slot for both policies. Installing a
            // pathless document would clear files_ before automatic traversal,
            // and would strand manual navigation at a no-wrap edge.
            ImageDocument removed;
            removed.path = source;
            installDocument(std::move(removed));
            // installDocument clears the previous removal state.
            removedDocumentPath_ = source;
            resumeNextPath_ = nextInOrder;
            resumePreviousPath_ = previous;
            removedFileIndex_ = removedIndex;
            updateStatus();
            if (!stayEmpty && !next.isEmpty() && !deferredOpen_) {
                auto traversal = FolderTraversal::start(files_, files_.indexOf(next) - 1, 1, wrap, source);
                if (traversal) openRequested({traversal->path(), 0, false, std::move(traversal)});
            }
        } else if (stayEmpty) {
            // The move failed, so the file is still there: put the picture back
            // with the zoom it had before the early black state.
            view_->setUnavailable(false);
            view_->setImage(shownImage);
            if (shownFit == ImageView::Fit::Original)
                view_->setZoom(shownZoom);
            else
                view_->setFit(shownFit);
            view_->setSelectionAspectRatio(shownAspect);
            view_->setSelection(shownSelection);
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
