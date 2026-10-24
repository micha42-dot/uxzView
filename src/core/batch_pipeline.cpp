// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_storage.h"
#include "batch_pipeline.h"
#include "batch_processor.h"
#include "file_journal.h"
#include "preview_service.h"
#include "processing.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QPainter>
#include <QScopeGuard>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <thread>
#include <vector>
namespace ix {
namespace {
void checkCancel(const std::atomic_bool &cancel) {
    if (cancel.load())
        throw std::runtime_error("Abgebrochen.");
}
QHash<QString, QImage> loadResources(const PipelineProfile &profile, const QHash<QString, QString> &expected,
                                     bool requireReview = false) {
    QHash<QString, QImage> resources;
    for (const auto &step : profile.steps) {
        const auto path = step.parameters.value(QStringLiteral("image")).toString();
        if (step.type != QStringLiteral("watermark") || path.isEmpty() || resources.contains(path))
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
            throw std::runtime_error("Wasserzeichen fehlt oder ist größer als 64 MiB.");
        auto bytes = file.readAll();
        const auto hash =
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        if (requireReview && expected.value(path) != hash)
            throw std::runtime_error(
                "Wasserzeichen wurde seit der Vorschau geändert. Bitte den Plan neu prüfen.");
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        if (reader.size().isValid() && !safeSize(reader.size()))
            throw std::runtime_error("Wasserzeichen überschreitet das Bildspeicherlimit.");
        auto image = reader.read();
        if (image.isNull() || !safeSize(image.size(), std::max(8, image.depth() / 8)))
            throw std::runtime_error("Wasserzeichen-Bild konnte nicht gelesen werden.");
        resources.insert(path, std::move(image));
    }
    return resources;
}
void watermark(QImage &image, const QJsonObject &p, const QHash<QString, QImage> *resources) {
    const double opacity = p.value(QStringLiteral("opacity")).toDouble(.65);
    const int margin = p.value(QStringLiteral("margin")).toInt(20);
    QImage result = image;
    if (result.format() == QImage::Format_Indexed8 || result.depth() < 24)
        result = result.convertToFormat(imageWorkingFormat(result));
    QPainter painter(&result);
    if (!painter.isActive())
        throw std::runtime_error("Wasserzeichen konnte nicht gezeichnet werden.");
    painter.setOpacity(opacity);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto anchor = p.value(QStringLiteral("anchor")).toString(QStringLiteral("bottomRight"));
    const QRect area = QRect(margin, margin, std::max(1, image.width() - 2 * margin),
                             std::max(1, image.height() - 2 * margin));
    int alignment = anchor == QStringLiteral("topLeft")  ? Qt::AlignTop | Qt::AlignLeft
                    : anchor == QStringLiteral("center") ? Qt::AlignCenter
                                                         : Qt::AlignBottom | Qt::AlignRight;
    auto path = p.value(QStringLiteral("image")).toString();
    if (!path.isEmpty()) {
        auto mark = resources ? resources->value(path) : QImage(path);
        if (mark.isNull())
            throw std::runtime_error("Wasserzeichen-Bild konnte nicht gelesen werden.");
        if (mark.colorSpace().isValid() && image.colorSpace().isValid() &&
            mark.colorSpace() != image.colorSpace()) {
            mark = mark.convertedToColorSpace(image.colorSpace());
            if (mark.isNull())
                throw std::runtime_error("Wasserzeichen-Farbprofil konnte nicht konvertiert werden.");
        }
        auto size = mark.size().scaled(
            QSize(std::max(1, int(area.width() * .35)), std::max(1, int(area.height() * .35))),
            Qt::KeepAspectRatio);
        QPoint pos = anchor == QStringLiteral("topLeft") ? area.topLeft()
                     : anchor == QStringLiteral("center")
                         ? QPoint(area.x() + (area.width() - size.width()) / 2,
                                  area.y() + (area.height() - size.height()) / 2)
                         : QPoint(area.right() - size.width() + 1, area.bottom() - size.height() + 1);
        painter.drawImage(QRect(pos, size), mark);
    } else {
        QFont font;
        font.setPixelSize(p.value(QStringLiteral("fontSize")).toInt(32));
        painter.setFont(font);
        QColor color(p.value(QStringLiteral("color")).toString(QStringLiteral("#ffffff")));
        if (!color.isValid())
            color = Qt::white;
        painter.setPen(color);
        painter.drawText(area, alignment | Qt::TextWordWrap, p.value(QStringLiteral("text")).toString());
    }
    painter.end();
    inheritImageProperties(image, result);
    image = std::move(result);
}
} // namespace
QJsonObject PipelineProfile::toJson() const {
    QJsonArray items;
    for (const auto &s : steps)
        items.append(
            QJsonObject{{QStringLiteral("type"), s.type}, {QStringLiteral("parameters"), s.parameters}});
    return {{QStringLiteral("schema"), 1}, {QStringLiteral("name"), name}, {QStringLiteral("steps"), items}};
}
PipelineProfile PipelineProfile::fromJson(const QJsonObject &object, QString &error) {
    PipelineProfile profile;
    if (object.value(QStringLiteral("schema")).toInt() != 1) {
        error = QStringLiteral("Unbekannte Profilversion.");
        return profile;
    }
    profile.name = object.value(QStringLiteral("name")).toString();
    for (auto value : object.value(QStringLiteral("steps")).toArray()) {
        auto o = value.toObject();
        profile.steps.append(
            {o.value(QStringLiteral("type")).toString(), o.value(QStringLiteral("parameters")).toObject()});
    }
    error = profile.validate();
    return profile;
}
QString PipelineProfile::validate() const {
    if (steps.isEmpty() || steps.size() > 32)
        return QStringLiteral("Eine Kette benötigt 1 bis 32 Schritte.");
    int exports = 0;
    for (int i = 0; i < steps.size(); ++i) {
        const auto &s = steps[i];
        const auto &p = s.parameters;
        if (s.type == QStringLiteral("resize")) {
            QSize size(p.value(QStringLiteral("width")).toInt(), p.value(QStringLiteral("height")).toInt());
            if (!safeSize(size))
                return QStringLiteral("Ungültige Zielgröße.");
        } else if (s.type == QStringLiteral("sharpen")) {
            int amount = p.value(QStringLiteral("amount")).toInt(20);
            if (amount < 1 || amount > 99)
                return QStringLiteral("Schärfen: Stärke muss 1 bis 99 sein.");
        } else if (s.type == QStringLiteral("watermark")) {
            if (p.value(QStringLiteral("text")).toString().isEmpty() &&
                p.value(QStringLiteral("image")).toString().isEmpty())
                return QStringLiteral("Wasserzeichen benötigt Text oder ein Bild.");
            double opacity = p.value(QStringLiteral("opacity")).toDouble(.65);
            int font = p.value(QStringLiteral("fontSize")).toInt(32),
                margin = p.value(QStringLiteral("margin")).toInt(20);
            if (!std::isfinite(opacity) || opacity < 0 || opacity > 1 || font < 1 || font > 1000 ||
                margin < 0 || margin > 10000)
                return QStringLiteral("Ungültige Wasserzeichenparameter.");
        } else if (s.type == QStringLiteral("export")) {
            ++exports;
            if (i != steps.size() - 1)
                return QStringLiteral("Speichern muss der letzte Schritt sein.");
            auto format = p.value(QStringLiteral("format")).toString().toLatin1();
            if (!CodecRegistry::canWrite(format))
                return QStringLiteral("Zielformat ist in diesem Build nicht schreibbar.");
            int quality = p.value(QStringLiteral("quality")).toInt(95);
            if (quality < 0 || quality > 100)
                return QStringLiteral("Qualität muss 0 bis 100 sein.");
        } else
            return QStringLiteral("Unbekannter Verarbeitungsschritt: ") + s.type;
    }
    return exports == 1 ? QString() : QStringLiteral("Genau ein Speicherschritt ist erforderlich.");
}
QList<PipelineFile> BatchPipeline::plan(const PipelineJob &job) {
    QList<PipelineFile> result;
    auto validation = job.profile.validate();
    if (job.outputDirectory.isEmpty())
        validation = QStringLiteral("Zielordner fehlt.");
    auto identity = [](const QString &path) {
        QFileInfo info(path);
        auto identity = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
#ifdef Q_OS_WIN
        return identity.toCaseFolded();
#else
        return identity;
#endif
    };
    QSet<QString> destinations, sources;
    QHash<QString, QString> resourceHashes;
    for (const auto &step : job.profile.steps) {
        const auto path = step.parameters.value(QStringLiteral("image")).toString();
        if (step.type != QStringLiteral("watermark") || path.isEmpty())
            continue;
        sources.insert(identity(path));
        const auto info = QFileInfo(path);
        auto hash = info.isFile() && info.size() <= 64 * 1024 * 1024 ? sha256File(path) : QString();
        if (hash.isEmpty())
            validation = QStringLiteral("Wasserzeichen fehlt oder ist größer als 64 MiB.");
        resourceHashes.insert(path, hash);
    }
    for (const auto &input : job.inputs)
        sources.insert(identity(input));
    auto format =
        job.profile.steps.isEmpty()
            ? QByteArray()
            : job.profile.steps.last().parameters.value(QStringLiteral("format")).toString().toLatin1();
    for (int i = 0; i < job.inputs.size(); ++i) {
        PipelineFile entry;
        entry.resourceHashes = resourceHashes;
        entry.input = QFileInfo(job.inputs[i]).absoluteFilePath();
        entry.inputStamp = imageFileKey(entry.input);
        entry.error = validation;
        const auto name = BatchProcessor::outputName(entry.input, job.namePattern, i + 1, format);
        entry.output = QDir(job.outputDirectory).absoluteFilePath(name);
        entry.replaces = QFileInfo::exists(entry.output);
        entry.targetStamp = entry.replaces ? imageFileKey(entry.output) : QString();
        entry.targetHash = entry.replaces ? sha256File(entry.output) : QString();
        auto key = identity(entry.output);
        if (entry.error.isEmpty()) {
            if (!QFileInfo(entry.input).isFile())
                entry.error = QStringLiteral("Quelle fehlt.");
            else if (name.isEmpty())
                entry.error = QStringLiteral("Ungültiges Namensmuster.");
            else if (sources.contains(key))
                entry.error =
                    QStringLiteral("Ziel ist eine Eingabedatei; bitte einen anderen Zielpfad wählen.");
            else if (destinations.contains(key))
                entry.error = QStringLiteral("Mehrere Eingaben ergeben dasselbe Ziel.");
            else if (entry.replaces && !job.overwrite)
                entry.error = QStringLiteral("Ziel existiert; wird übersprungen.");
        }
        destinations.insert(key);
        result.append(entry);
    }
    return result;
}
ImageDocument BatchPipeline::transform(ImageDocument d, const PipelineProfile &profile,
                                       const std::atomic_bool &cancel, bool allowLoss,
                                       const QHash<QString, QImage> *resources) {
    auto error = profile.validate();
    if (!error.isEmpty())
        throw std::runtime_error(error.toStdString());
    if (d.empty())
        throw std::runtime_error("Kein Bild geladen.");
    for (const auto &s : profile.steps) {
        checkCancel(cancel);
        auto p = s.parameters;
        if (s.type == QStringLiteral("resize") || s.type == QStringLiteral("sharpen")) {
            if (!allowLoss && d.pixels.pixelFormat().typeInterpretation() == QPixelFormat::FloatingPoint)
                throw std::runtime_error("HDR-Verarbeitung benötigt die explizite Verlustfreigabe.");
        }
        if (s.type == QStringLiteral("resize")) {
            ResizeOptions o;
            QSize size(p.value(QStringLiteral("width")).toInt(), p.value(QStringLiteral("height")).toInt());
            o.size = p.value(QStringLiteral("proportional")).toBool(true)
                         ? d.pixels.size().scaled(size, Qt::KeepAspectRatio)
                         : size;
            o.dpi = d.dpiX();
            o.filter = ResampleFilter(std::clamp(p.value(QStringLiteral("filter")).toInt(5), 0, 6));
            o.fastDownscale = p.value(QStringLiteral("fast")).toBool(true);
            d.pixels = resample(d.pixels, o, &cancel);
        } else if (s.type == QStringLiteral("sharpen")) {
            double v = p.value(QStringLiteral("amount")).toInt(20) / 20.0;
            d.pixels = convolve(d.pixels, {0, -v, 0, -v, 1 + 4 * v, -v, 0, -v, 0}, 3);
        } else if (s.type == QStringLiteral("watermark"))
            watermark(d.pixels, p, resources);
    }
    checkCancel(cancel);
    d.modified = true;
    return d;
}
PipelinePreview BatchPipeline::preview(const PipelineJob &job, const QString &path,
                                       const std::atomic_bool &cancel,
                                       const QList<PipelineFile> &reviewedPlan) {
    PipelinePreview out;
    try {
        CodecRegistry registry;
        auto read = registry.read(path);
        if (!read.ok()) {
            out.error = read.error;
            return out;
        }
        out.originalSize = read.document.pixels.size();
        out.before = read.document.pixels.scaled(500, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        auto resources = loadResources(job.profile,
                                       reviewedPlan.isEmpty() ? QHash<QString, QString>()
                                                              : reviewedPlan.first().resourceHashes,
                                       !reviewedPlan.isEmpty());
        auto image = transform(read.document, job.profile, cancel, job.allowLoss, &resources);
        out.resultSize = image.pixels.size();
        const auto previewRoot = job.journalRoot.isEmpty()
                                     ? AppStorage::cacheRoot() +
                                           QStringLiteral("/previews")
                                     : job.journalRoot + QStringLiteral("/previews");
        if (!QDir().mkpath(previewRoot))
            throw std::runtime_error("Vorschauordner konnte nicht angelegt werden.");
        QTemporaryDir temporary(previewRoot + QStringLiteral("/review-XXXXXX"));
        if (!temporary.isValid())
            throw std::runtime_error("Vorschauordner konnte nicht angelegt werden.");
        auto p = job.profile.steps.last().parameters;
        auto format = p.value(QStringLiteral("format")).toString().toLatin1();
        auto file = temporary.filePath(QStringLiteral("preview.") + QString::fromLatin1(format));
        out.error =
            registry.save(image, file, format, p.value(QStringLiteral("quality")).toInt(95), job.allowLoss);
        if (!out.error.isEmpty())
            return out;
        auto encoded = registry.read(file);
        if (!encoded.ok()) {
            out.error = encoded.error;
            return out;
        }
        out.after = encoded.document.pixels.scaled(500, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } catch (const std::exception &e) {
        out.error = QString::fromUtf8(e.what());
    }
    return out;
}
QList<PipelineFile> BatchPipeline::run(const PipelineJob &job, const QList<PipelineFile> &plan,
                                       const std::atomic_bool &cancel,
                                       const std::function<void(int, int)> &progress) {
    auto validation = job.profile.validate();
    if (!validation.isEmpty()) {
        auto result = plan;
        for (auto &e : result)
            e.error = validation;
        return result;
    }
    std::vector<PipelineFile> results(plan.begin(), plan.end());
    QHash<QString, QImage> resources;
    try {
        if (plan.isEmpty())
            return {};
        resources = loadResources(job.profile, plan.first().resourceHashes, true);
        for (const auto &entry : plan)
            if (entry.resourceHashes != plan.first().resourceHashes)
                throw std::runtime_error("Verarbeitungskette und Ressourcenplan stimmen nicht überein.");
    } catch (const std::exception &e) {
        auto failed = plan;
        for (auto &entry : failed)
            entry.error = QString::fromUtf8(e.what());
        return failed;
    }
    std::atomic_int next = 0, done = 0;
    const int threads = std::clamp(job.parallelism, 1, 4);
    std::vector<std::thread> workers;
    auto joinWorkers = qScopeGuard([&] {
        for (auto &worker : workers)
            if (worker.joinable())
                worker.join();
    });
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&] {
            CodecRegistry registry;
            FileJournal journal(job.journalRoot);
            while (!cancel.load()) {
                int i = next.fetch_add(1);
                if (i >= int(results.size()))
                    break;
                auto &entry = results[size_t(i)];
                if (entry.error.isEmpty())
                    try {
                        checkCancel(cancel);
                        if (imageFileKey(entry.input) != entry.inputStamp)
                            throw std::runtime_error("Quelle wurde seit der Vorschau geändert.");
                        auto read = registry.read(entry.input);
                        if (!read.ok())
                            throw std::runtime_error(read.error.toStdString());
                        auto image = transform(read.document, job.profile, cancel, job.allowLoss, &resources);
                        auto p = job.profile.steps.last().parameters;
                        auto format = p.value(QStringLiteral("format")).toString().toLatin1();
                        if (!QDir().mkpath(QFileInfo(entry.output).absolutePath()))
                            throw std::runtime_error("Zielordner konnte nicht angelegt werden.");
                        auto stage = QFileInfo(entry.output)
                                         .dir()
                                         .filePath(QStringLiteral(".ixview-") +
                                                   QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                                   QLatin1Char('.') + QString::fromLatin1(format));
                        auto cleanup = qScopeGuard([stage] { QFile::remove(stage); });
                        entry.error =
                            registry.save(image, stage, format, p.value(QStringLiteral("quality")).toInt(95),
                                          job.allowLoss);
                        if (entry.error.isEmpty()) {
                            if (cancel.load())
                                entry.error = QStringLiteral("Abgebrochen.");
                            else
                                entry.error = journal.installOutput(stage, entry.output, job.overwrite,
                                                                    entry.targetStamp, entry.targetHash);
                        }
                        QFile::remove(stage);
                    } catch (const std::exception &e) {
                        entry.error = QString::fromUtf8(e.what());
                    }
                int completed = done.fetch_add(1) + 1;
                if (progress)
                    progress(completed, int(results.size()));
            }
        });
    for (auto &worker : workers)
        worker.join();
    workers.clear();
    for (int i = next.load(); i < int(results.size()); ++i)
        results[size_t(i)].error = QStringLiteral("Abgebrochen; nicht gestartet.");
    return QList<PipelineFile>(results.begin(), results.end());
}
} // namespace ix
