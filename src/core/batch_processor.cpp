// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "batch_processor.h"
#include "file_journal.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QUuid>
namespace ix {
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
    CodecRegistry registry;
    FileJournal journal(o.journalRoot);
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
                    e.error = journal.perform(QStringLiteral("move"), e.input, e.output);
                } else {
                    auto image = registry.read(e.input);
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
                            auto stage =
                                dir.absoluteFilePath(QStringLiteral(".ixview-") +
                                                     QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                                     QLatin1Char('.') + QString::fromLatin1(o.format));
                            auto cleanup = qScopeGuard([stage] { QFile::remove(stage); });
                            e.error = registry.save(image.document, stage, o.format, o.quality, o.allowLoss);
                            if (e.error.isEmpty()) {
                                if (cancel.load())
                                    e.error = QStringLiteral("Abgebrochen.");
                                else
                                    e.error = journal.installOutput(stage, e.output, false, {});
                            }
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
