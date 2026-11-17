// SPDX-License-Identifier: GPL-3.0-or-later
#include "batch_pipeline.h"
#include "batch_processor.h"
#include "codec_registry.h"
#include "file_operations.h"
#include "file_revision.h"
#include "save_formats.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <utility>

namespace ix {
namespace {
QString identity(const QString &path) {
    const QFileInfo info(path);
    const auto result = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
#ifdef Q_OS_WIN
    return result.toCaseFolded();
#else
    return result;
#endif
}
} // namespace

QList<PipelineFile> BatchPipeline::plan(const PipelineJob &job, const std::atomic_bool *cancel) {
    const auto cancelled = [cancel] { return cancel && cancel->load(); };
    if (cancelled()) return {};
    auto validation = job.profile.validate();
    if (job.outputDirectory.isEmpty()) validation = QStringLiteral("Zielordner fehlt.");
    QSet<QString> destinations, sources;
    QHash<QString, QString> resourceHashes;
    for (const auto &step : job.profile.steps) {
        if (cancelled()) return {};
        const auto path = step.parameters.value(QStringLiteral("image")).toString();
        if (step.type != QStringLiteral("watermark") || path.isEmpty() || resourceHashes.contains(path))
            continue;
        sources.insert(identity(path));
        if (!validation.isEmpty()) continue;
        const QFileInfo info(path);
        const auto hash = info.isFile() && info.size() <= 64 * 1024 * 1024 ? sha256File(path, cancel) : QString();
        if (cancelled()) return {};
        if (hash.isEmpty()) validation = QStringLiteral("Wasserzeichen fehlt oder ist größer als 64 MiB.");
        resourceHashes.insert(path, hash);
    }
    for (const auto &input : job.inputs) {
        if (cancelled()) return {};
        sources.insert(identity(input));
    }
    // Canonicalize here as well: profiles written before the format combos
    // stored canonical ids may still contain a raw extension such as "jfif",
    // which would otherwise name every output .jfif.
    const auto format = job.profile.steps.isEmpty() ? QByteArray()
        : canonicalSaveFormat(job.profile.steps.last().parameters.value(QStringLiteral("format")).toString().toLatin1());
    // Resolve the app's own canonical suffix for that id (jpeg -> .jpg), so
    // batch outputs never carry an alias extension such as .jfif or .jpe.
    QByteArray extension = format;
    if (!format.isEmpty())
        for (const auto &f : writableSaveFormats(CodecRegistry()))
            if (f.id == format) {
                extension = f.suffix().toLatin1();
                break;
            }
    QList<PipelineFile> result;
    result.reserve(job.inputs.size());
    for (int i = 0; i < job.inputs.size(); ++i) {
        if (cancelled()) return {};
        PipelineFile entry;
        entry.resourceHashes = resourceHashes;
        entry.input = QFileInfo(job.inputs[i]).absoluteFilePath();
        entry.inputStamp = imageFileKey(entry.input);
        entry.error = validation;
        const auto name = BatchProcessor::outputName(entry.input, job.namePattern, i + 1, extension);
        entry.output = QDir(job.outputDirectory).absoluteFilePath(name);
        const QFileInfo target(entry.output);
        entry.replaces = target.exists();
        entry.targetStamp = entry.replaces ? imageFileKey(entry.output) : QString();
        const auto key = identity(entry.output);
        if (entry.error.isEmpty()) {
            if (!QFileInfo(entry.input).isFile())
                entry.error = QStringLiteral("Quelle fehlt.");
            else if (name.isEmpty())
                entry.error = QStringLiteral("Ungültiges Namensmuster.");
            else if (sources.contains(key))
                entry.error = QStringLiteral("Ziel ist eine Eingabedatei; bitte einen anderen Zielpfad wählen.");
            else if (destinations.contains(key))
                entry.error = QStringLiteral("Mehrere Eingaben ergeben dasselbe Ziel.");
            else if (entry.replaces && !job.overwrite)
                entry.error = QStringLiteral("Ziel existiert; wird übersprungen.");
            else if (target.isSymLink() || (entry.replaces && !target.isFile()))
                entry.error = QStringLiteral("Ziel existiert oder ist keine normale Datei.");
        }
        if (entry.error.isEmpty() && entry.replaces) {
            entry.targetHash = sha256File(entry.output, cancel);
            if (cancelled()) return {};
            if (entry.targetHash.isEmpty())
                entry.error = QStringLiteral("Originaldatei konnte nicht geprüft werden.");
            else if (entry.targetStamp != imageFileKey(entry.output))
                entry.error = QStringLiteral("Ziel wurde seit der Vorschau verändert. Bitte den Plan neu prüfen.");
        }
        destinations.insert(key);
        result.append(std::move(entry));
    }
    return cancelled() ? QList<PipelineFile>{} : result;
}
} // namespace ix
