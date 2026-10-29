// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_storage.h"
#include "batch_pipeline.h"
#include "file_operations.h"
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
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <vector>
namespace ix {
namespace {
void checkCancel(const std::atomic_bool &cancel) {
    if (cancel.load())
        throw std::runtime_error("Abgebrochen.");
}
QHash<QString, QImage> loadResources(const PipelineProfile &profile, const QHash<QString, QString> &expected,
                                     const std::atomic_bool &cancel, bool requireReview = false) {
    QHash<QString, QImage> resources;
    for (const auto &step : profile.steps) {
        checkCancel(cancel);
        const auto path = step.parameters.value(QStringLiteral("image")).toString();
        if (step.type != QStringLiteral("watermark") || path.isEmpty() || resources.contains(path))
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
            throw std::runtime_error("Wasserzeichen fehlt oder ist größer als 64 MiB.");
        auto bytes = file.readAll();
        checkCancel(cancel);
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
        checkCancel(cancel);
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
        checkCancel(cancel);
        CodecRegistry registry;
        auto read = registry.read(path);
        checkCancel(cancel);
        if (!read.ok()) {
            out.error = read.error;
            return out;
        }
        out.originalSize = read.document.pixels.size();
        out.before = read.document.pixels.scaled(500, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        auto resources = loadResources(job.profile,
                                       reviewedPlan.isEmpty() ? QHash<QString, QString>()
                                                              : reviewedPlan.first().resourceHashes,
                                       cancel, !reviewedPlan.isEmpty());
        auto image = transform(std::move(read.document), job.profile, cancel, job.allowLoss, &resources);
        out.resultSize = image.pixels.size();
        const auto previewRoot = job.cacheRoot.isEmpty()
                                     ? AppStorage::cacheRoot() +
                                           QStringLiteral("/previews")
                                     : job.cacheRoot + QStringLiteral("/previews");
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
        image = {}; // The encoded preview can now be decoded without retaining full output pixels.
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
                                       const std::function<void(int, int)> &progress,
                                       const CodecRegistry *codecs) {
    if (cancel.load()) {
        auto result = plan;
        for (auto &entry : result) entry.error = QStringLiteral("Abgebrochen; nicht gestartet.");
        return result;
    }
    auto validation = job.profile.validate();
    if (!validation.isEmpty()) {
        auto result = plan;
        for (auto &e : result)
            e.error = validation;
        return result;
    }
    const auto eligible = std::count_if(plan.cbegin(), plan.cend(), [](const PipelineFile &entry) {
        return entry.error.isEmpty();
    });
    // Nothing will be converted: preserve each skip/error and avoid decoding
    // resources or constructing codecs/threads for an empty workload.
    if (!eligible) return plan;
    std::vector<PipelineFile> results(plan.begin(), plan.end());
    QHash<QString, QImage> resources;
    try {
        if (plan.isEmpty())
            return {};
        resources = loadResources(job.profile, plan.first().resourceHashes, cancel, true);
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
    const int threads = std::min(int(eligible), std::clamp(job.parallelism, 1, 4));
    std::vector<std::thread> workers;
    auto joinWorkers = qScopeGuard([&] {
        for (auto &worker : workers)
            if (worker.joinable())
                worker.join();
    });
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&] {
            auto registry = codecs ? *codecs : CodecRegistry();
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
                        entry.inputBytes = QFileInfo(entry.input).size();
                        auto read = registry.read(entry.input);
                        if (!read.ok())
                            throw std::runtime_error(read.error.toStdString());
                        if (imageFileKey(entry.input) != entry.inputStamp)
                            throw std::runtime_error("Quelle wurde während des Lesens verändert.");
                        auto image = transform(std::move(read.document), job.profile, cancel, job.allowLoss, &resources);
                        auto p = job.profile.steps.last().parameters;
                        auto format = p.value(QStringLiteral("format")).toString().toLatin1();
                        if (!QDir().mkpath(QFileInfo(entry.output).absolutePath()))
                            throw std::runtime_error("Zielordner konnte nicht angelegt werden.");
                        const auto beforeCommit = [&] {
                            if (cancel.load()) return QStringLiteral("Abgebrochen.");
                            if (imageFileKey(entry.input) != entry.inputStamp)
                                return QStringLiteral("Quelle wurde während der Verarbeitung verändert.");
                            return QString();
                        };
                        if (!entry.targetHash.isEmpty() && sha256File(entry.output) != entry.targetHash)
                            entry.error = QStringLiteral("Zielinhalt wurde seit der Vorschau verändert. Bitte den Plan neu prüfen.");
                        else if (const auto error = beforeCommit(); !error.isEmpty()) entry.error = error;
                        else entry.error = registry.save(image, entry.output, format,
                            p.value(QStringLiteral("quality")).toInt(95), job.allowLoss,
                            entry.targetStamp, beforeCommit);
                        if (entry.error.isEmpty()) entry.outputBytes = QFileInfo(entry.output).size();
                    } catch (const std::exception &e) {
                        entry.error = QString::fromUtf8(e.what());
                    } catch (...) {
                        entry.error = QStringLiteral("Vorgang fehlgeschlagen.");
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
