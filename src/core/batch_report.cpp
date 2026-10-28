// SPDX-License-Identifier: GPL-3.0-or-later
#include "batch_report.h"
#include "localization.h"
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QUuid>
namespace ix {
PipelineSummary pipelineSummary(const QList<PipelineFile> &entries) {
    PipelineSummary result;
    for (const auto &entry : entries) {
        if (!entry.error.isEmpty()) { ++result.failed; continue; }
        ++result.succeeded;
        result.inputBytes += entry.inputBytes;
        result.outputBytes += entry.outputBytes;
    }
    return result;
}
QString writePipelineReport(const QList<PipelineFile> &entries, const QString &directory, QString &path) {
    path.clear();
    if (!QDir(directory).exists()) return IX_TEXT("Protokoll: Zielordner fehlt.");
    const auto destination = QDir(directory).filePath(QStringLiteral("uxzView-batch-%1.csv")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const auto quote = [](QString text) {
        // CSV opened in spreadsheets must not interpret file names as formulas.
        if (!text.isEmpty() && QStringLiteral("=+-@\t\r\n").contains(text.front()))
            text.prepend(QLatin1Char('\''));
        text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + text + QLatin1Char('"');
    };
    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return file.errorString();
    bool complete = false;
    const auto cleanup = qScopeGuard([&] {
        file.close();
        if (!complete) QFile::remove(destination);
    });
    QByteArray data("\xef\xbb\xbfsource,target,result,input_bytes,output_bytes\r\n");
    const auto flushBuffer = [&] {
        if (file.write(data) != data.size()) return false;
        data.clear();
        return true;
    };
    for (const auto &entry : entries) {
        const auto result = entry.error.isEmpty() ? QStringLiteral("OK") : localized(entry.error);
        data += (quote(entry.input) + QLatin1Char(',') + quote(entry.output) + QLatin1Char(',') + quote(result)
            + QLatin1Char(',') + QString::number(entry.inputBytes) + QLatin1Char(',')
            + QString::number(entry.outputBytes) + QStringLiteral("\r\n")).toUtf8();
        if (data.size() >= 64 * 1024 && !flushBuffer()) return file.errorString();
    }
    if (!flushBuffer() || !file.flush()) return file.errorString();
    complete = true;
    path = destination;
    return {};
}
} // namespace ix
