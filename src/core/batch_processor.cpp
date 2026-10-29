// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "batch_processor.h"
#include "file_operations.h"
#include "file_revision.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <optional>
namespace ix {
void BatchFileChanges::add(const BatchEntry &entry) {
    if (entry.output.isEmpty() || (!entry.moved && !entry.error.isEmpty())) return;
    affected_.insert(entry.output);
    if (!entry.moved) return;
    affected_.insert(entry.input);
    const auto previous = originalForCurrent_.find(entry.input);
    QString original = entry.input;
    if (previous != originalForCurrent_.end()) {
        original = previous.value();
        originalForCurrent_.erase(previous);
    }
    originalForCurrent_.insert(entry.output, original);
    auto current = currentForOriginal_.find(original);
    if (current == currentForOriginal_.end())
        currentForOriginal_.insert(original, {entry.output, entry.sourceRevision, entry.targetRevision});
    else {
        // A rewrite between moves breaks the proof that loaded pixels still
        // match, even though the original's final path must still be followed.
        if (current->targetRevision != entry.sourceRevision) current->sourceRevision.clear();
        current->path = entry.output;
        current->targetRevision = entry.targetRevision;
    }
}
QString BatchFileChanges::resolve(const QString &original) const {
    const auto current = currentForOriginal_.constFind(original);
    return current == currentForOriginal_.cend() ? original : current->path;
}
std::optional<BatchRelocation> BatchFileChanges::relocation(const QString &original) const {
    const auto current = currentForOriginal_.constFind(original);
    if (current == currentForOriginal_.cend()) return std::nullopt;
    return current.value();
}
QString BatchProcessor::outputName(const QString &input, const QString &pattern, int index,
                                   const QByteArray &ext) {
    QString name = pattern;
    name.replace(QStringLiteral("{name}"), QFileInfo(input).completeBaseName());
    QRegularExpression rx(QStringLiteral("#+"));
    auto match = rx.match(name);
    if (match.hasMatch())
        name.replace(match.capturedStart(), match.capturedLength(),
                     QString::number(index).rightJustified(match.capturedLength(), QLatin1Char('0')));
    if (name.isEmpty() || name.contains(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]"))) ||
        name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')) || name == QStringLiteral(".") ||
        name == QStringLiteral("..") ||
        QRegularExpression(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
                           QRegularExpression::CaseInsensitiveOption)
            .match(name)
            .hasMatch())
        return {};
    return name + QLatin1Char('.') + QString::fromLatin1(ext);
}
QList<BatchEntry> BatchProcessor::run(const BatchOptions &o, const std::atomic_bool &cancel,
                                      const std::function<void(int, int)> &progress) {
    std::optional<CodecRegistry> registry;
    if (!o.renameOnly) registry.emplace();
    FileOperations operations;
    QList<BatchEntry> results;
    QSet<QString> reserved;
    QDir dir(o.outputDirectory);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));
    for (int i = 0; i < o.inputs.size(); ++i) {
        if (cancel.load())
            break;
        BatchEntry e{o.inputs[i], {}, {}};
        auto ext = o.renameOnly ? QFileInfo(e.input).suffix().toLatin1() : o.format;
        auto name = outputName(e.input, o.namePattern, o.start + i, ext);
        if (name.isEmpty()) {
            e.error = QStringLiteral("Ungültiges Namensmuster.");
        } else {
            e.output = dir.absoluteFilePath(name);
            auto key = e.output.toCaseFolded();
            if (QFileInfo::exists(e.output) || reserved.contains(key))
                e.error = QStringLiteral("Ziel existiert bereits; nicht überschrieben.");
            else {
                reserved.insert(key);
                if (o.renameOnly) {
                    const auto result = operations.perform(QStringLiteral("move"), e.input, e.output);
                    e.error = result.error;
                    e.moved = result.applied;
                    e.sourceRevision = result.sourceRevision;
                    e.targetRevision = result.targetRevision;
                } else {
                    const auto sourceRevision = imageFileKey(e.input);
                    auto image = registry->read(e.input);
                    if (!image.ok())
                        e.error = image.error;
                    else {
                        if (o.resizeTo.isValid()) {
                            ResizeOptions resize;
                            resize.size =
                                image.document.pixels.size().scaled(o.resizeTo, Qt::KeepAspectRatio);
                            resize.dpi = image.document.dpiX();
                            try {
                                image.document.pixels = resample(image.document.pixels, resize, &cancel);
                                image.document.modified = true;
                            } catch (const std::exception &ex) {
                                e.error = QString::fromUtf8(ex.what());
                            }
                        }
                        if (e.error.isEmpty() && !cancel.load()) {
                            e.error = registry->save(image.document, e.output, o.format, o.quality, o.allowLoss,
                                QString(), [&] {
                                    if (cancel.load()) return QStringLiteral("Abgebrochen.");
                                    if (sourceRevision != imageFileKey(e.input))
                                        return QStringLiteral("Quelle wurde während der Verarbeitung verändert.");
                                    return QString();
                                });
                        } else if (cancel.load())
                            e.error = QStringLiteral("Abgebrochen.");
                    }
                }
            }
        }
        results.append(e);
        if (progress)
            progress(i + 1, o.inputs.size());
    }
    return results;
}
} // namespace ix
