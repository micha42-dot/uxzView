// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/batch_pipeline.h"
#include "core/codec_registry.h"
#include "core/image_storage.h"
#include <QCoreApplication>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <thread>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif
namespace {
qint64 privateBytes() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX info{};
    using Read = BOOL (WINAPI *)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    const auto read = reinterpret_cast<Read>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
    if (read && read(GetCurrentProcess(), reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&info), sizeof(info))) return qint64(info.PrivateUsage);
#endif
    return 0;
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if ((app.arguments().size() != 3 && app.arguments().size() != 4) ||
        (app.arguments()[2] != QStringLiteral("small") && app.arguments()[2] != QStringLiteral("large"))) return 2;
    bool validLimit = true;
    const auto limitMiB = app.arguments().size() == 4 ? app.arguments()[3].toLongLong(&validLimit) : 512;
    if (!validLimit || limitMiB < 1 || limitMiB > 1048576) return 2;
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 3;
    QCoreApplication::setOrganizationName(QStringLiteral("IxViewBatchMemoryBenchmark"));
    QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    ix::initializeImageMemoryLimit(limitMiB);
    ix::configureImageStorage({false});
    const bool large = app.arguments()[2] == QStringLiteral("large");
    const QSize sourceSize = large ? QSize(3000,2000) : QSize(64,48);
    const QSize targetSize = large ? QSize(5000,3333) : QSize(96,72);
    try {
        QImage source(sourceSize, QImage::Format_RGBA64);
        if (source.isNull()) throw std::runtime_error("Source allocation failed");
        source.setColorSpace(QColorSpace::DisplayP3);
        for (int y = 0; y < source.height(); ++y) {
            auto *row = reinterpret_cast<QRgba64 *>(source.scanLine(y));
            for (int x = 0; x < source.width(); ++x)
                row[x] = QRgba64::fromRgba64(quint16(x * 17 + y * 3), quint16(x * 7 + y * 23), quint16(x * 5 + y * 11), quint16(30000 + (x + y) % 35536));
        }
        ix::PipelineJob job;
        job.parallelism = 4; job.outputDirectory = temporary.filePath(QStringLiteral("output"));
        job.namePattern = QStringLiteral("{name}-out");
        job.profile.steps = {{QStringLiteral("resize"), {{QStringLiteral("width"), targetSize.width()},
            {QStringLiteral("height"), targetSize.height()}, {QStringLiteral("proportional"), false}, {QStringLiteral("filter"), 5}}},
            {QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
        for (int i = 0; i < 4; ++i) {
            const auto path = temporary.filePath(QStringLiteral("source-%1.png").arg(i));
            if (!source.save(path)) throw std::runtime_error("Fixture write failed");
            job.inputs.append(path);
        }
        source = {};
        const auto plan = ix::BatchPipeline::plan(job);
        for (const auto &file : plan) if (!file.error.isEmpty()) throw std::runtime_error(file.error.toStdString());
        const auto before = privateBytes();
        std::atomic<qint64> peak = before;
        std::atomic_bool stop = false, cancel = false;
        std::thread sample([&] {
            while (!stop.load()) {
                auto value = privateBytes(), maximum = peak.load();
                while (value > maximum && !peak.compare_exchange_weak(maximum, value)) {}
                QThread::msleep(5);
            }
        });
        const auto join = qScopeGuard([&] { stop = true; if (sample.joinable()) sample.join(); });
        QElapsedTimer timer; timer.start();
        const auto results = ix::BatchPipeline::run(job, plan, cancel);
        const auto elapsed = timer.nsecsElapsed() / 1e6;
        stop = true; sample.join();
        QJsonArray hashes;
        ix::CodecRegistry registry;
        for (const auto &file : results) {
            if (!file.error.isEmpty()) throw std::runtime_error(file.error.toStdString());
            const auto read = registry.read(file.output);
            if (!read.ok() || read.document.pixels.size() != targetSize || read.document.pixels.depth() != 64 ||
                read.document.pixels.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) throw std::runtime_error("Output fidelity check failed");
            QCryptographicHash hash(QCryptographicHash::Sha256);
            for (int y = 0; y < read.document.pixels.height(); ++y)
                hash.addData(QByteArrayView(reinterpret_cast<const char *>(read.document.pixels.constScanLine(y)), qsizetype(targetSize.width()) * 8));
            hashes.append(QString::fromLatin1(hash.result().toHex()));
        }
        const QJsonObject report{{QStringLiteral("elapsed_ms"), elapsed}, {QStringLiteral("private_before_bytes"), before},
            {QStringLiteral("sampled_peak_private_bytes"), peak.load()}, {QStringLiteral("output_hashes"), hashes},
            {QStringLiteral("qt"), QString::fromLatin1(qVersion())}, {QStringLiteral("memory_limit_mib"), limitMiB},
            {QStringLiteral("jobs"), 4}, {QStringLiteral("source_width"), sourceSize.width()}, {QStringLiteral("source_height"), sourceSize.height()},
            {QStringLiteral("target_width"), targetSize.width()}, {QStringLiteral("target_height"), targetSize.height()},
            {QStringLiteral("note"), QStringLiteral("Warm generated RGBA64 PNG inputs; Lanczos, alpha and Display P3 preserved. Fixture generation, planning and decoded output verification excluded. 5ms Windows private-byte sampling; zero means unsupported. Not an RSS cap or a cold-start benchmark.")}};
        QFile output(app.arguments()[1]);
        if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(report).toJson()) < 0) return 4;
    } catch (const std::exception &error) { qCritical("%s", error.what()); return 1; }
    return 0;
}
