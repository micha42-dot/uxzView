// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation_fixture.h"
#include "convolution_reference.h"
#include "auto_crop_reference.h"
#include "pixel_processing_reference.h"
#include "resample_reference.h"
#include "core/apng_codec.h"
#include "core/codec_registry.h"
#include "core/file_operations.h"
#include "core/batch_pipeline.h"
#include "core/directory_index.h"
#include "core/image_loading.h"
#include "core/image_storage.h"
#include "core/metadata.h"
#include "core/preview_service.h"
#include "ui/image_view.h"
#include "ui/pipeline_result_model.h"
#include "ui/main_window.h"
#include "ui/appearance.h"
#include "ui/platform_support.h"
#include "ui/preview_strip.h"
#include "ui/slideshow_controller.h"
#include "ui/worker_wait.h"
#include <QElapsedTimer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QCollator>
#include <QDirListing>
#include <QImageReader>
#include <QBuffer>
#include <QLibrary>
#include <QStorageInfo>
#include <QJsonDocument>
#include <QtTest>
#include <QtWidgets>
#include <vector>
#include <barrier>
#include <future>
#include <chrono>
#include <thread>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif
namespace {
QJsonObject profileReaderInput(const QString &path) {
    QJsonArray samples;
    QByteArray expectedHash;
    QImage expected;
    for (int pair=0;pair<6;++pair) for (bool buffered : {pair%2==0,pair%2!=0}) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QImageReader reader(path); reader.setAutoTransform(true);
#ifdef Q_OS_WIN
        IO_COUNTERS before{},after{};
        GetProcessIoCounters(GetCurrentProcess(),&before);
#endif
        QElapsedTimer timer; timer.start();
        if (!reader.canRead()) return {{QStringLiteral("error"),reader.errorString()}};
        const auto format=reader.format();
        const auto count=reader.imageCount(); const auto size=reader.size(); const auto pixelFormat=reader.imageFormat();
        if (count!=1 || (format!="png" && format!="jpeg")) return {{QStringLiteral("error"),QStringLiteral("Expected static JPEG/PNG")}};
        if (buffered) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size()>128*ix::MiB) return {{QStringLiteral("error"),QStringLiteral("Input unavailable/too large")}};
            bytes=file.readAll(); buffer.open(QIODevice::ReadOnly);
            reader.setDevice(&buffer); reader.setFormat(format); reader.setAutoTransform(true);
        }
        auto image=reader.read();
        const auto elapsed=timer.nsecsElapsed()/1e6;
#ifdef Q_OS_WIN
        GetProcessIoCounters(GetCurrentProcess(),&after);
#endif
        if(image.isNull()) return {{QStringLiteral("error"),reader.errorString()}};
        const auto hash=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256);
        if(expectedHash.isEmpty()) { expectedHash=hash; expected=image; }
        else if(hash!=expectedHash || image.colorSpace()!=expected.colorSpace() || image.textKeys()!=expected.textKeys() ||
                image.dotsPerMeterX()!=expected.dotsPerMeterX() || image.dotsPerMeterY()!=expected.dotsPerMeterY())
            return {{QStringLiteral("error"),QStringLiteral("Device changed decoded pixels/properties")}};
        QJsonObject row{{QStringLiteral("buffered"),buffered},{QStringLiteral("ms"),elapsed},{QStringLiteral("input_bytes"),bytes.size()},
            {QStringLiteral("width"),size.width()},{QStringLiteral("height"),size.height()},{QStringLiteral("pixel_format"),int(pixelFormat)}};
#ifdef Q_OS_WIN
        row.insert(QStringLiteral("read_operations"),qint64(after.ReadOperationCount-before.ReadOperationCount));
        row.insert(QStringLiteral("read_bytes"),qint64(after.ReadTransferCount-before.ReadTransferCount));
#endif
        samples.append(row);
    }
    return {{QStringLiteral("samples"),samples},{QStringLiteral("pixel_sha256"),QString::fromLatin1(expectedHash.toHex())},
        {QStringLiteral("note"),QStringLiteral("Six alternating QFile/QBuffer pairs; includes source snapshot reading, header and decode. Pixel/DPI/profile validation outside timer; no document cache, metadata extraction or GUI. Windows counters are process IO, not physical disk traffic.")}};
}
QJsonObject profileSaveProgress() {
    ix::initializeImageMemoryLimit(); ix::configureImageStorage({false});
    QTemporaryDir directory; if (!directory.isValid()) return {{QStringLiteral("error"),QStringLiteral("Temporary directory failed")}};
    ix::ImageDocument document; document.pixels=QImage(128,96,QImage::Format_ARGB32);
    for (int y=0;y<96;++y) for (int x=0;x<128;++x)
        document.pixels.setPixel(x,y,qRgba((x*17+y*3)%256,(x*7+y*23)%256,(x*31+y*11)%256,(x*13+y*19)%256));
    ix::CodecRegistry registry;
    const auto warmup=registry.save(document,directory.filePath(QStringLiteral("warmup.png")),"png",95,true,QString());
    if (!warmup.isEmpty()) return {{QStringLiteral("error"),warmup}};
    QJsonArray samples;
    for (int sample=0;sample<20;++sample) for (bool legacy : {sample%2==0,sample%2!=0}) {
        const auto path=directory.filePath(QStringLiteral("save-%1-%2.png").arg(sample).arg(legacy));
        struct Progress final : QProgressDialog {
            int shows=0;
            void showEvent(QShowEvent *event) override { QProgressDialog::showEvent(event); ++shows; }
        } progress;
        progress.setCancelButton(nullptr); progress.setRange(0,0);
        progress.setWindowModality(Qt::ApplicationModal);
        QFutureWatcher<QString> watcher;
        QObject::connect(&watcher,&QFutureWatcherBase::finished,&progress,&QDialog::accept);
        bool timedOut=false; QTimer watchdog; watchdog.setSingleShot(true);
        QObject::connect(&watchdog,&QTimer::timeout,&progress,[&] { timedOut=true; progress.reject(); });
        watchdog.start(2000);
        QElapsedTimer elapsed; elapsed.start();
        watcher.setFuture(QtConcurrent::run([registry,document,path] {
            return registry.save(document,path,"png",95,true,QString());
        }));
        if (legacy) { progress.exec(); ix::waitForGuiWorker(watcher); }
        else ix::waitForGuiWorker(watcher,progress);
        const double milliseconds=elapsed.nsecsElapsed()/1e6; watchdog.stop();
        if (timedOut && !legacy) return {{QStringLiteral("error"),QStringLiteral("New progress wait exceeded the watchdog")}};
        const auto error=watcher.future().takeResult();
        if (!error.isEmpty() || QImage(path)!=document.pixels)
            return {{QStringLiteral("error"),error.isEmpty() ? QStringLiteral("Saved pixels differ") : error}};
        samples.append(QJsonObject{{QStringLiteral("sample"),sample},{QStringLiteral("legacy"),legacy},
            {QStringLiteral("milliseconds"),milliseconds},{QStringLiteral("progress_shows"),progress.shows},
            {QStringLiteral("watchdog_fired"),timedOut}});
    }
    return {{QStringLiteral("samples"),samples},{QStringLiteral("qt"),QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"),QGuiApplication::platformName()},
        {QStringLiteral("note"),QStringLiteral("20 alternating pairs; real 128x96 alpha PNG atomic saves; warm writer; GUI-thread waiting included; decoding validation outside timer. Timed-out legacy waits are failures, not speedup samples. No whole-app startup/navigation claim.")}};
}
QImage resizeProfileSource(QImage::Format format, QSize size = {1920,1024}) {
    QImage source(size,format);
    for (int y=0;y<source.height();++y) {
        auto *row=reinterpret_cast<QRgb *>(source.scanLine(y));
        for (int x=0;x<source.width();++x) {
            if (format==QImage::Format_RGBA64)
                reinterpret_cast<QRgba64 *>(row)[x]=QRgba64::fromRgba64(
                    (x*317+y*503)%65536,(x*107+y*223)%65536,(x*631+y*911)%65536,(x*913+y*419)%65536);
            else row[x]=qRgba((x*17+y*3)%256,(x*7+y*23)%256,(x*31+y*11)%256,
                format==QImage::Format_RGB32 ? 255 : (x*13+y*19)%256);
        }
    }
    source.setColorSpace(QColorSpace::SRgb);
    return source;
}
QJsonObject checkResize() {
    ix::initializeImageMemoryLimit(); ix::configureImageStorage({false});
    const auto source=resizeProfileSource(QImage::Format_ARGB32);
    ix::ResizeOptions options; options.size={10000,5333};
    const auto actual=ix::resample(source,options), expected=ix::test::referenceResample(source,options);
    qint64 differences=0; int error=0; QJsonArray examples;
    for (int y=0;y<actual.height();++y) {
        const auto *a=reinterpret_cast<const QRgb *>(actual.constScanLine(y));
        const auto *e=reinterpret_cast<const QRgb *>(expected.constScanLine(y));
        for(int x=0;x<actual.width();++x) if(a[x]!=e[x]) {
            ++differences;
            for(int shift : {0,8,16,24}) error=std::max(error,std::abs(int((a[x]>>shift)&255)-int((e[x]>>shift)&255)));
            if(examples.size()<12) examples.append(QJsonObject{{QStringLiteral("x"),x},{QStringLiteral("y"),y},
                {QStringLiteral("actual"),QString::number(a[x],16)},{QStringLiteral("expected"),QString::number(e[x],16)}});
        }
    }
    return {{QStringLiteral("different_pixels"),double(differences)},{QStringLiteral("max_channel_error"),error},{QStringLiteral("examples"),examples}};
}
QJsonObject profileResize(bool matrix = false, bool wide = false, int selectedCase = -1) {
    ix::initializeImageMemoryLimit();
    ix::configureImageStorage({false});
    QJsonArray cases;
    struct Scenario { QSize source, target; bool linear; QImage::Format format; };
    QVector<Scenario> scenarios;
    for (auto format : {QImage::Format_RGB32,QImage::Format_ARGB32}) {
        if (wide) {
            scenarios.append({{1920,1024},{50000,257},false,format});
            scenarios.append({{1920,1024},{100000,129},false,format});
            continue;
        }
        scenarios.append({{1920,1024},{10000,5333},false,format});
        if (matrix) {
            scenarios.append({{1920,1024},{640,341},false,format});
            scenarios.append({{640,341},{2048,1092},true,format});
        }
    }
    if (matrix) scenarios.append({{640,341},{2048,1092},true,QImage::Format_RGBA64});
    const auto filters = matrix ? QVector<ix::ResampleFilter>{ix::ResampleFilter::Hermite,
        ix::ResampleFilter::Triangle,ix::ResampleFilter::Bell,ix::ResampleFilter::BSpline,
        ix::ResampleFilter::Mitchell,ix::ResampleFilter::Lanczos,ix::ResampleFilter::Nearest}
        : QVector<ix::ResampleFilter>{ix::ResampleFilter::Lanczos,ix::ResampleFilter::Triangle,ix::ResampleFilter::Nearest};
    int caseIndex = -1;
    for (const auto &scenario : scenarios) {
        const auto source=resizeProfileSource(scenario.format,scenario.source);
        for (auto filter : filters) {
            if (++caseIndex != selectedCase && selectedCase >= 0) continue;
            QJsonArray resizeTimes,displayTimes,paintTimes;
            QByteArray hash;
            ix::ResizeOptions options; options.size=scenario.target; options.filter=filter;
            options.linearLight=scenario.linear;
            if (matrix && options.size.width()<source.width()) options.fastDownscale=false;
            for (int sample=0;sample<3;++sample) {
                QElapsedTimer timer; timer.start();
                auto resized=ix::resample(source,options);
                resizeTimes.append(timer.nsecsElapsed()/1e6);
                timer.restart(); const auto display=ix::displayImage(resized,true,false);
                displayTimes.append(timer.nsecsElapsed()/1e6);
                ix::ImageView view; view.resize(1200,800); view.setImage(display);
                QImage canvas(view.size(),QImage::Format_RGB32);
                timer.restart(); view.viewport()->render(&canvas);
                paintTimes.append(timer.nsecsElapsed()/1e6);
                if (selectedCase >= 0 && sample < 2) continue;
                const auto current=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(resized.constBits()),resized.sizeInBytes()),QCryptographicHash::Sha256).toHex();
                if (!hash.isEmpty() && hash!=current) return {{QStringLiteral("error"),QStringLiteral("Resize output changed between runs")}};
                hash=current;
            }
            cases.append(QJsonObject{{QStringLiteral("case_index"),caseIndex},{QStringLiteral("format"),int(scenario.format)}, {QStringLiteral("filter"),int(filter)},
                {QStringLiteral("source_width"),source.width()},{QStringLiteral("source_height"),source.height()},
                {QStringLiteral("target_width"),options.size.width()},{QStringLiteral("target_height"),options.size.height()},
                {QStringLiteral("linear_light"),options.linearLight},
                {QStringLiteral("fast_downscale"),options.fastDownscale},
                {QStringLiteral("resize_ms"),resizeTimes}, {QStringLiteral("display_ms"),displayTimes},
                {QStringLiteral("paint_ms"),paintTimes}, {QStringLiteral("sha256"),QString::fromLatin1(hash)}});
            qInfo().noquote() << "Resize profile:" << source.size() << "->" << options.size
                             << "format" << int(scenario.format) << "filter" << int(filter) << "linear" << options.linearLight;
        }
    }
    if (cases.isEmpty()) return {{QStringLiteral("error"),QStringLiteral("Resize case index is out of range")}};
    return {{QStringLiteral("cases"),cases},{QStringLiteral("source"),(matrix || wide) ? QStringLiteral("per case") : QStringLiteral("1920x1024")},
        {QStringLiteral("target"),(matrix || wide) ? QStringLiteral("per case") : QStringLiteral("10000x5333")},{QStringLiteral("qt"),QString::fromLatin1(qVersion())},
        {QStringLiteral("hash_samples"),selectedCase < 0 ? 3 : 1},
        {QStringLiteral("note"),QStringLiteral("Three calls per scenario/filter; focused cases hash only the last output, full matrices compare all three; no decoding/encoding/IO; alpha varies in ARGB32/RGBA64; sharpening off; hashes exclude timing; scenario fields specify dimensions and gamma. Same profiler for before/after.")}};
}
QJsonObject probeLargeResize(int axis, int filter) {
    if ((axis!=50000 && axis!=100000) || filter<0 || filter>6)
        return {{QStringLiteral("error"),QStringLiteral("Use axis 50000/100000 and filter 0..6")}};
    ix::initializeImageMemoryLimit(512);
    QTemporaryDir temporary(QDir::current().filePath(QStringLiteral("build/large-resize-probe-XXXXXX")));
    if (!temporary.isValid()) return {{QStringLiteral("error"),QStringLiteral("Temporary directory failed")}};
    ix::configureImageStorage({true,65536*ix::MiB,temporary.path(),1});
    const auto source=resizeProfileSource(QImage::Format_ARGB32);
    ix::ResizeOptions options; options.size={axis,int(std::lround(double(axis)*source.height()/source.width()))};
    options.filter=ix::ResampleFilter(filter);
    const auto pixelBytes=ix::imageBufferBytes(options.size,source.format());
    if (QStorageInfo(temporary.path()).bytesAvailable()<2*pixelBytes)
        return {{QStringLiteral("error"),QStringLiteral("Insufficient free disk space for bounded large probe")}};
    QVector<QPoint> points{{0,0},{axis-1,options.size.height()-1},{axis/2,options.size.height()/2}};
    for (int x : {8191,8192,16383,16384,32767,32768,49151,49152,axis-1})
        for (int y : {255,256,257,options.size.height()/4,options.size.height()/2,options.size.height()-1})
            points.append({x,y});
    QVector<QRgb> expected;
    if (filter!=6) expected=ix::test::referenceResamplePoints(source,options,points);
    else for (auto point:points) expected.append(source.pixel(
        int((qint64(2*point.x()+1)*source.width())/(2*axis)),
        int((qint64(2*point.y()+1)*source.height())/(2*options.size.height()))));
    QElapsedTimer timer; timer.start();
    auto image=ix::resample(source,options);
    const double elapsed=timer.nsecsElapsed()/1e6;
    for (qsizetype i=0;i<points.size();++i) if(image.pixel(points[i])!=expected[i])
        return {{QStringLiteral("error"),QStringLiteral("Large resize pixel mismatch")}};
    const auto backing=ix::imageBackingFile(image);
    const bool mapped=ix::imageIsFileBacked(image);
    const auto bytes=image.sizeInBytes(); image={};
    ix::flushImageStorageDeletes();
    if (!mapped || QFileInfo::exists(backing) || ix::mappedImageBytes()!=0)
        return {{QStringLiteral("error"),QStringLiteral("Mapped output was not released")}};
    return {{QStringLiteral("width"),options.size.width()},{QStringLiteral("height"),options.size.height()},
        {QStringLiteral("filter"),filter},{QStringLiteral("resize_ms"),elapsed},
        {QStringLiteral("pixel_bytes"),double(bytes)},{QStringLiteral("mapped"),mapped},
        {QStringLiteral("reference_points"),points.size()},{QStringLiteral("mapped_bytes_after_release"),0}};
}
QJsonObject profileAutoCrop() {
    QJsonArray formats;
    for (auto format : {QImage::Format_RGB32,QImage::Format_RGBA64}) {
        QImage source(6000,4000,format); source.fill(QColor(17,37,53));
        const QRect expected(37,51,6000-74,4000-102);
        { QPainter painter(&source); painter.fillRect(expected,QColor(91,151,213)); }
        QJsonArray oldTimes,newTimes;
        for (int sample=0;sample<3;++sample)
            for (bool reference : {sample%2==0,sample%2!=0}) {
                QElapsedTimer timer; timer.start();
                const auto rect=reference ? ix::test::autoCropReference(source,5) : ix::autoCropRect(source,5);
                const double elapsed=timer.nsecsElapsed()/1e6;
                if (rect!=expected) return {{QStringLiteral("error"),QStringLiteral("Crop bounds differ")}};
                (reference ? oldTimes : newTimes).append(elapsed);
            }
        formats.append(QJsonObject{{QStringLiteral("format"),int(format)},
            {QStringLiteral("reference_ms"),oldTimes},{QStringLiteral("scanline_ms"),newTimes}});
    }
    return {{QStringLiteral("formats"),formats},{QStringLiteral("qt"),QString::fromLatin1(qVersion())},
        {QStringLiteral("note"),QStringLiteral("24 MP in-memory image; three alternating calls per path/format; original scalar reference vs production scanlines, same crop bounds; excludes generation/decoding, no additional full image buffer.")}};
}
QJsonObject profileDirectoryFilters() {
    const auto base=QDir::current().filePath(QStringLiteral("build/audit-fixtures"));
    if (!QDir().mkpath(base)) return {{QStringLiteral("error"),QStringLiteral("Fixture directory failed")}};
    QTemporaryDir temporary(base+QStringLiteral("/directory-filters-XXXXXX"));
    if (!temporary.isValid()) return {{QStringLiteral("error"),QStringLiteral("Temporary directory failed")}};
    for (int i=0;i<10000;++i) {
        const auto suffix=i%4==0 ? QStringLiteral("txt") : i%4==1 ? QStringLiteral("PNG")
                         : i%4==2 ? QStringLiteral("jpg") : QStringLiteral("svgz");
        QFile file(temporary.filePath(QStringLiteral("image%1.").arg(i)+suffix));
        if (!file.open(QIODevice::WriteOnly) || file.write("fixture")!=7)
            return {{QStringLiteral("error"),QStringLiteral("Fixture write failed")}};
    }
    const auto formats=ix::CodecRegistry().extensions();
    QJsonArray scenarios;
    for (int scenario=0;scenario<3;++scenario) {
        const auto extensions=scenario==0 ? QStringList{QStringLiteral("png")} : formats;
        QJsonArray samples;
        QString digest;
        int count=0;
        for (int sample=0;sample<3;++sample) {
            QElapsedTimer timer; timer.start();
            const auto snapshot=ix::scanDirectory(temporary.path(),extensions,0,false,nullptr,scenario==2);
            samples.append(timer.nsecsElapsed()/1e6);
            count=snapshot.files.size();
            QCryptographicHash hash(QCryptographicHash::Sha256);
            for (const auto &path:snapshot.files) {
                hash.addData(QFileInfo(path).fileName().toUtf8()); hash.addData(QByteArrayView("\n"));
            }
            const auto current=QString::fromLatin1(hash.result().toHex());
            if (!digest.isEmpty() && current!=digest)
                return {{QStringLiteral("error"),QStringLiteral("Inconsistent scan results")}};
            digest=current;
        }
        scenarios.append(QJsonObject{{QStringLiteral("extensions"),extensions.size()},
            {QStringLiteral("find_latest"),scenario==2},{QStringLiteral("files"),count},
            {QStringLiteral("ordered_names_sha256"),digest},{QStringLiteral("ms"),samples}});
    }
    return {{QStringLiteral("scenarios"),scenarios},{QStringLiteral("qt"),QString::fromLatin1(qVersion())},
        {QStringLiteral("note"),QStringLiteral("10000 tiny mixed files, warm filesystem; complete natural-order scan including position map, excludes fixture generation and image decoding. Same profiler before/after.")}};
}
QJsonObject profileCacheAdmission() {
    const auto base=QDir::current().filePath(QStringLiteral("build/audit-fixtures"));
    if (!QDir().mkpath(base)) return {{QStringLiteral("error"),QStringLiteral("Fixture directory failed")}};
    QTemporaryDir temporary(base+QStringLiteral("/cache-admission-XXXXXX"));
    if (!temporary.isValid()) return {{QStringLiteral("error"),QStringLiteral("Temporary directory failed")}};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    qApp->setProperty("ixStorageData",temporary.filePath(QStringLiteral("profile")));
    QSettings().setValue(QStringLiteral("navigation/askToSave"),false);
    const auto path=temporary.filePath(QStringLiteral("source.bmp"));
    const QSize dimensions(5120,5120);
    QImage generated(dimensions,QImage::Format_RGB32); generated.fill(QColor(37,129,211));
    if (!generated.save(path)) return {{QStringLiteral("error"),QStringLiteral("BMP generation failed")}};
    generated={};
    const auto originalHash=ix::sha256File(path);
    const auto fileBytes=QFileInfo(path).size();
    ix::CodecRegistry registry;
    ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192,16,true);
    int decodes=0;
    qint64 pixelBytes=0;
    for (int i=0;i<2;++i) {
        auto result=ix::ImageLoadCache::read(registry,path,0,[&] { ++decodes; });
        if (!result.ok()) return {{QStringLiteral("error"),result.error}};
        pixelBytes=result.document.pixels.sizeInBytes();
        if (result.document.pixels.size()!=dimensions || pixelBytes!=100LL*1024*1024)
            return {{QStringLiteral("error"),QStringLiteral("Unexpected decoded size")}};
    }
    ix::MainWindow window;
    QString uiError;
    QTimer dismiss;
    QObject::connect(&dismiss,&QTimer::timeout,&window,[&] {
        if (auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            uiError=message->text(); message->reject();
        }
    });
    dismiss.start(10);
    auto wait=[&] {
        QElapsedTimer timer; timer.start();
        while (window.isBusy() && timer.elapsed()<30000) { QApplication::processEvents(); QThread::msleep(1); }
        return !window.isBusy() && uiError.isEmpty();
    };
    window.openPath(path);
    if (!wait() || window.document().pixels.size()!=dimensions)
        return {{QStringLiteral("error"),QStringLiteral("Main-window load failed: ")+uiError}};
    auto *view=window.findChild<ix::ImageView *>();
    view->setSelection({10,10,120,90}); window.dispatch(QStringLiteral("crop"));
    if (!wait() || window.document().pixels.size()!=QSize(120,90) || !window.document().modified)
        return {{QStringLiteral("error"),QStringLiteral("Crop failed: ")+uiError}};
    const bool undo=window.findChild<QAction *>(QStringLiteral("undo"))->isEnabled();
    window.dispatch(QStringLiteral("undo"));
    if (!wait() || window.document().pixels.size()!=dimensions || !undo)
        return {{QStringLiteral("error"),QStringLiteral("Pixel Undo failed: ")+uiError}};
    const bool unchanged=ix::sha256File(path)==originalHash;
    if (!unchanged || decodes!=2) return {{QStringLiteral("error"),QStringLiteral("Source/cache expectations failed")}};
    return {{QStringLiteral("width"),dimensions.width()},{QStringLiteral("height"),dimensions.height()},
        {QStringLiteral("decoded_pixel_bytes"),pixelBytes},{QStringLiteral("file_bytes"),fileBytes},
        {QStringLiteral("cache_admission_bytes"),96LL*1024*1024},{QStringLiteral("decode_calls_for_two_cache_reads"),decodes},
        {QStringLiteral("main_window_loaded"),true},{QStringLiteral("crop_succeeded"),true},
        {QStringLiteral("pixel_undo_restored_original_size"),true},{QStringLiteral("source_hash_unchanged"),unchanged},
        {QStringLiteral("qt"),QString::fromLatin1(qVersion())},
        {QStringLiteral("note"),QStringLiteral("Generated 100 MiB RGB32 BMP; proves cache admission differs from load/edit limits, not gigapixel or global-RAM support.")}};
}
QJsonObject profileBatchResults() {
    QList<ix::PipelineFile> entries;
    constexpr int count = 20000;
    entries.reserve(count);
    for (int i = 0; i < count; ++i) {
        ix::PipelineFile entry;
        entry.input = QStringLiteral("/photos/image-%1.png").arg(i);
        entry.output = QStringLiteral("/export/image-%1.webp").arg(i);
        entries.append(entry);
    }
    QJsonArray samples;
    for (int sample = 0; sample < 5; ++sample)
      for (bool legacy : {sample % 2 == 0, sample % 2 != 0}) {
        QElapsedTimer timer;
        timer.start();
        double elapsed = 0;
        if (legacy) {
            QTableWidget table(0, 4);
            table.setRowCount(count);
            for (int row = 0; row < count; ++row) {
                const auto &entry = entries.at(row);
                table.setItem(row, 0, new QTableWidgetItem(QFileInfo(entry.input).fileName()));
                table.item(row, 0)->setToolTip(entry.input);
                table.setItem(row, 1, new QTableWidgetItem(QFileInfo(entry.output).fileName()));
                table.item(row, 1)->setToolTip(entry.output);
                table.setItem(row, 2, new QTableWidgetItem(QStringLiteral("Create")));
                table.setItem(row, 3, new QTableWidgetItem(QStringLiteral("Ready")));
                table.item(row, 3)->setToolTip(QStringLiteral("Ready"));
            }
            elapsed = timer.nsecsElapsed() / 1e6;
        } else {
            ix::PipelineResultModel model;
            QTableView table;
            table.setModel(&model);
            model.setEntries(entries, false);
            // Include the first 25 rows, rather than timing an unread model only.
            for (int row = 0; row < 25; ++row)
                for (int col = 0; col < 4; ++col) model.data(model.index(row, col));
            elapsed = timer.nsecsElapsed() / 1e6;
        }
        samples.append(QJsonObject{{QStringLiteral("legacy"), legacy}, {QStringLiteral("ms"), elapsed}});
    }
    return {{QStringLiteral("rows"), count}, {QStringLiteral("samples"), samples},
        {QStringLiteral("note"), QStringLiteral("In-process setup only; fixture creation/destruction excluded; model includes 25 visible rows. No file IO, image conversion or viewer navigation measured.")},
        {QStringLiteral("qt"), QString::fromLatin1(qVersion())}};
}
QJsonObject profileBatchPlan() {
    QTemporaryDir temporary(QDir::current().filePath(QStringLiteral("build/batch-plan-XXXXXX")));
    if (!temporary.isValid()) return {{QStringLiteral("error"), QStringLiteral("Temporary directory failed")}};
    ix::PipelineJob job;
    const auto source = temporary.filePath(QStringLiteral("input.png"));
    QImage pixels(16, 12, QImage::Format_RGB32); pixels.fill(Qt::blue);
    if (!pixels.save(source)) return {{QStringLiteral("error"), QStringLiteral("Fixture image failed")}};
    job.outputDirectory = temporary.filePath(QStringLiteral("output"));
    QDir().mkpath(job.outputDirectory);
    job.profile.steps = {{QStringLiteral("export"), {{QStringLiteral("format"), QStringLiteral("png")}}}};
    for (int i = 1; i <= 16; ++i) {
        job.inputs.append(source);
        QFile target(QDir(job.outputDirectory).filePath(QStringLiteral("input_%1.png").arg(i, 4, 10, QLatin1Char('0'))));
        if (!target.open(QIODevice::WriteOnly) || !target.resize(8 * 1024 * 1024))
            return {{QStringLiteral("error"), QStringLiteral("Fixture destination failed")}};
    }
    ix::BatchPipeline::plan(job); // initialize writer discovery before timing
    QJsonArray samples;
    for (int sample = 0; sample < 5; ++sample)
        for (const bool overwrite : {sample % 2 == 0, sample % 2 != 0}) {
            job.overwrite = overwrite;
#ifdef Q_OS_WIN
            IO_COUNTERS before{}, after{};
            GetProcessIoCounters(GetCurrentProcess(), &before);
#endif
            QElapsedTimer timer; timer.start();
            const auto plan = ix::BatchPipeline::plan(job);
            const auto ms = timer.nsecsElapsed() / 1e6;
#ifdef Q_OS_WIN
            GetProcessIoCounters(GetCurrentProcess(), &after);
#endif
            int hashes = 0, errors = 0;
            for (const auto &entry : plan) {
                hashes += !entry.targetHash.isEmpty();
                errors += !entry.error.isEmpty();
            }
            if (plan.size() != 16 || errors != (overwrite ? 0 : 16) || (overwrite && hashes != 16))
                return {{QStringLiteral("error"), QStringLiteral("Plan behavior changed")}};
            QJsonObject row{{QStringLiteral("overwrite"), overwrite}, {QStringLiteral("ms"), ms},
                {QStringLiteral("hashes"), hashes}};
#ifdef Q_OS_WIN
            row.insert(QStringLiteral("process_read_bytes"), qint64(after.ReadTransferCount - before.ReadTransferCount));
#endif
            samples.append(row);
        }
    return {{QStringLiteral("samples"), samples}, {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("note"), QStringLiteral("Five alternating skip/overwrite plans, warm filesystem. 16 existing 8 MiB destinations. "
        "No conversion or writes timed. Windows counters are process-wide read transfers, not physical disk reads. "
        "Use the same harness before/after; compare skipped destinations with the unchanged overwrite control.")}};
}
QJsonObject profileInterfaceTheme(const QString &path) {
    ix::CodecRegistry registry;
    const auto reference = registry.read(path);
    if (!reference.ok()) return {{QStringLiteral("error"), reference.error}};
    ix::applyInterfaceTheme(QStringLiteral("light"));
    ix::ImageView view;
    view.resize(1200, 800);
    view.setImage(reference.document.pixels);
    view.setFit(ix::ImageView::Fit::Window);
    view.show();
    QCoreApplication::processEvents();
    const auto sourceKey = view.image().cacheKey();
    const auto zoom = view.zoom();
    QImage canvas(view.viewport()->size(), QImage::Format_ARGB32_Premultiplied), expected;
    QJsonArray samples;
    for (int sample = 0; sample < 6; ++sample) {
        for (const bool dark : {sample % 2 == 0, sample % 2 != 0}) {
            const auto mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
            QElapsedTimer timer; timer.start();
            ix::applyInterfaceTheme(mode);
            QCoreApplication::processEvents();
            const auto switchMs = timer.nsecsElapsed() / 1e6;
            timer.restart();
            const auto decoded = registry.read(path);
            const auto decodeMs = timer.nsecsElapsed() / 1e6;
            if (!decoded.ok() || decoded.document.pixels != reference.document.pixels)
                return {{QStringLiteral("error"), QStringLiteral("Decoded pixels changed")}};
            view.viewport()->render(&canvas); // prime the viewport raster cache
            timer.restart();
            for (int repaint = 0; repaint < 30; ++repaint) view.viewport()->render(&canvas);
            const auto repaintMs = timer.nsecsElapsed() / 1e6 / 30;
            if (expected.isNull()) expected = canvas.copy();
            if (canvas != expected || view.image().cacheKey() != sourceKey || view.zoom() != zoom)
                return {{QStringLiteral("error"), QStringLiteral("Theme changed image, viewport or zoom")}};
            samples.append(QJsonObject{{QStringLiteral("mode"), mode}, {QStringLiteral("switch_ms"), switchMs},
                {QStringLiteral("decode_ms"), decodeMs}, {QStringLiteral("cached_repaint_ms"), repaintMs}});
        }
    }
    return {{QStringLiteral("samples"), samples}, {QStringLiteral("pixels"),
        QJsonArray{reference.document.pixels.width(), reference.document.pixels.height()}},
        {QStringLiteral("qt"), QString::fromLatin1(qVersion())}, {QStringLiteral("note"), QStringLiteral(
        "Six alternating light/dark samples, warm filesystem, uncached registry decoding; 30 cached viewport repaints per sample. "
        "Switch timing includes palette propagation/event processing in this small harness, not the whole application. "
        "Exact viewport/decoded pixels, unchanged source key and zoom checked outside timing. No native compositor timing or before/after baseline.")}};
}
QJsonObject profileNativePixelEdits() {
    QJsonArray rows;
    for (const auto format : {QImage::Format_RGB32,QImage::Format_ARGB32}) {
        auto source=ix::test::filterPattern({6000,4000},format);
        source.setColorSpace(QColorSpace::DisplayP3); source.setDevicePixelRatio(1.5); source.setOffset({7,-3});
        source.setDotsPerMeterX(4500); source.setText(QStringLiteral("Comment"),QStringLiteral("pixel benchmark"));
        const auto key=source.cacheKey();
        for (int operation=0;operation<3;++operation) {
            const auto edit=[&](bool reference) {
                if (operation==0) return reference ? ix::test::referenceNegative(source) : ix::negative(source);
                if (operation==1) return reference ? ix::test::referenceChannel(source,3) : ix::channel(source,3);
                return reference ? ix::test::referenceSwap(source,0,2) : ix::swapChannels(source,0,2);
            };
            const auto expected=edit(true);
            QJsonArray referenceTimes,currentTimes;
            for (int sample=0;sample<3;++sample) for (const bool reference : {sample%2==0,sample%2!=0}) {
                QElapsedTimer timer; timer.start(); const auto result=edit(reference);
                const auto ms=timer.nsecsElapsed()/1e6;
                if (result!=expected || result.format()!=expected.format() || result.colorSpace()!=source.colorSpace() ||
                    result.devicePixelRatio()!=source.devicePixelRatio() || result.offset()!=source.offset() ||
                    result.dotsPerMeterX()!=source.dotsPerMeterX() || result.text()!=source.text() || source.cacheKey()!=key)
                    return {{QStringLiteral("error"),QStringLiteral("Pixel edits changed samples, properties or source")}};
                (reference ? referenceTimes : currentTimes).append(ms);
            }
            rows.append(QJsonObject{{QStringLiteral("format"),int(format)},{QStringLiteral("operation"),operation},
                {QStringLiteral("reference_ms"),referenceTimes},{QStringLiteral("current_ms"),currentTimes}});
        }
    }
    return {{QStringLiteral("samples"),rows},{QStringLiteral("qt"),QString::fromLatin1(qVersion())},
        {QStringLiteral("note"),QStringLiteral("24 MP RGB32/ARGB32, three alternating calls per arm. Negative/all, alpha-channel display, red/blue swap. Includes allocation and working/output conversion; no decode or file IO. Exact pixels, properties and unchanged source checked outside timing. Compare the unchanged reference and production before/after in fresh processes.")}};
}
QJsonObject profileGrayscale() {
    QJsonArray rows;
    for (const auto format : {QImage::Format_RGB32, QImage::Format_ARGB32}) {
        auto source = ix::test::filterPattern({6000,4000}, format);
        source.setColorSpace(QColorSpace::DisplayP3); source.setDotsPerMeterX(4500);
        source.setText(QStringLiteral("Comment"), QStringLiteral("grayscale measurement"));
        const auto key = source.cacheKey();
        const auto expected = ix::test::referenceGrayscale(source);
        QJsonArray legacyTimes, currentTimes;
        for (int sample = 0; sample < 3; ++sample)
            for (const bool legacy : {sample % 2 == 0, sample % 2 != 0}) {
                QElapsedTimer timer; timer.start();
                const auto result = legacy ? ix::test::referenceGrayscale(source) : ix::grayscale(source);
                const auto elapsed = timer.nsecsElapsed() / 1e6;
                if (result != expected || result.colorSpace() != source.colorSpace() ||
                    result.dotsPerMeterX() != source.dotsPerMeterX() || result.text() != source.text() || source.cacheKey() != key)
                    return {{QStringLiteral("error"), QStringLiteral("Grayscale pixels/properties/source changed")}};
                (legacy ? legacyTimes : currentTimes).append(elapsed);
            }
        rows.append(QJsonObject{{QStringLiteral("format"), int(format)},
            {QStringLiteral("legacy_ms"), legacyTimes}, {QStringLiteral("current_ms"), currentTimes}});
    }
    return {{QStringLiteral("samples"), rows}, {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("note"), QStringLiteral("24 MP RGB32/ARGB32, three alternating samples per arm. Includes allocation/conversion; excludes decode and GUI. Exact pixels and source properties checked outside timing.")}};
}
QJsonObject profileNativeSharpen(QImage::Format format,QSize size) {
    ix::initializeImageMemoryLimit(2048); ix::configureImageStorage({false});
    if((format!=QImage::Format_RGB32 && format!=QImage::Format_ARGB32 && format!=QImage::Format_RGBA64) ||
       size.width()<1 || size.height()<1 || qint64(size.width())*size.height()>24000000)
        return {{QStringLiteral("error"),QStringLiteral("Unsupported fixture")}};
    QImage source(size,format);
    if(source.isNull())return {{QStringLiteral("error"),QStringLiteral("Fixture allocation failed")}};
    for(int y=0;y<size.height();++y) for(int x=0;x<size.width();++x) {
        if(format==QImage::Format_RGBA64)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(quint16(x*317+y*7),quint16(y*503+x*13),quint16(x*631+y*11),quint16(x*419+y*29));
        else reinterpret_cast<QRgb *>(source.scanLine(y))[x]=qRgba((x*17+y*3)%256,(x*7+y*23)%256,(x*31+y*11)%256,
            format==QImage::Format_RGB32 ? 255 : (x*13+y*19)%256);
    }
    source.setColorSpace(QColorSpace::DisplayP3); source.setDotsPerMeterX(4500); source.setDotsPerMeterY(8300);
    source.setText(QStringLiteral("Comment"),QStringLiteral("Native sharpen measurement"));
    const auto sourceKey=source.cacheKey();
    const QVector<double> kernel{0,-1,0,-1,5,-1,0,-1,0};
    QJsonValue privateGrowth=QJsonValue::Null,cpuMs=QJsonValue::Null;
#ifdef Q_OS_WIN
    const auto memory=[] { PROCESS_MEMORY_COUNTERS_EX data{}; data.cb=sizeof(data);
        return GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&data),sizeof(data)) ? qint64(data.PrivateUsage) : qint64(-1); };
    const auto cpu=[] { FILETIME created{},exited{},kernelTime{},user{};
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernelTime,&user))return qint64(-1);
        return qint64((quint64(kernelTime.dwHighDateTime)<<32)|kernelTime.dwLowDateTime)+
            qint64((quint64(user.dwHighDateTime)<<32)|user.dwLowDateTime); };
    const auto initialPrivate=memory(),initialCpu=cpu(); std::atomic<qint64> peak=initialPrivate;
    std::atomic_bool stop=false;
    std::thread sampler([&] { while(!stop.load()) { peak.store(std::max(peak.load(),memory())); std::this_thread::sleep_for(std::chrono::milliseconds(2)); } });
    const auto stopSampler=qScopeGuard([&] { stop=true; if(sampler.joinable())sampler.join(); });
#endif
    QElapsedTimer timer; timer.start();
    const auto result=ix::convolve(source,kernel,3); const double elapsed=timer.nsecsElapsed()/1e6;
#ifdef Q_OS_WIN
    const auto finalCpu=cpu(); stop=true; sampler.join(); peak.store(std::max(peak.load(),memory()));
    if(initialPrivate>=0 && peak.load()>=initialPrivate)privateGrowth=peak.load()-initialPrivate;
    if(initialCpu>=0 && finalCpu>=0)cpuMs=(finalCpu-initialCpu)/10000.0;
#endif
    const auto expected=ix::test::referenceConvolve(source,kernel,3);
    if(result!=expected || result.colorSpace()!=expected.colorSpace() || result.text()!=expected.text() ||
       result.dotsPerMeterX()!=expected.dotsPerMeterX() || result.dotsPerMeterY()!=expected.dotsPerMeterY() || source.cacheKey()!=sourceKey)
        return {{QStringLiteral("error"),QStringLiteral("Pixels/properties/source changed")}};
    return {{QStringLiteral("elapsed_ms"),elapsed},{QStringLiteral("cpu_ms"),cpuMs},{QStringLiteral("private_peak_delta"),privateGrowth},
        {QStringLiteral("width"),size.width()},{QStringLiteral("height"),size.height()},{QStringLiteral("format"),int(format)},
        {QStringLiteral("pixel_sha256"),QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(
            reinterpret_cast<const char *>(result.constBits()),result.sizeInBytes()),QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("note"),QStringLiteral("Fresh-process sharpen, native RGB32/ARGB32 or RGBA64 control; direct fixture generation before timer, scalar RGBA64 reference/hash verification after timer and memory sampling. Includes allocation/filter/conversions. Windows process private growth sampled every 2ms; unsupported counters null. No decode, resize, GUI or general first-open claim.")}};
}
QJsonObject profileSharpen() {
    QJsonArray rows;
    const QVector<double> kernel{0, -1, 0, -1, 5, -1, 0, -1, 0};
    for (const auto size : {QSize(3000, 2000), QSize(6000, 4000)}) {
        for (const auto format : {QImage::Format_RGB32, QImage::Format_RGBA64}) {
            auto source = ix::test::filterPattern(size, format);
            source.setColorSpace(QColorSpace::DisplayP3);
            source.setDotsPerMeterX(4500);
            source.setText(QStringLiteral("Comment"), QStringLiteral("sharpen measurement"));
            const auto key = source.cacheKey();
            const auto reference = ix::test::referenceConvolve(source, kernel, 3);
            QJsonArray scalar, current;
            for (int sample = 0; sample < 3; ++sample) {
                for (const bool control : {sample % 2 == 0, sample % 2 != 0}) {
                    QElapsedTimer timer;
                    timer.start();
                    const auto result = control ? ix::test::referenceConvolve(source, kernel, 3)
                                                : ix::convolve(source, kernel, 3);
                    const double elapsed = timer.nsecsElapsed() / 1e6;
                    if (result != reference || result.colorSpace() != source.colorSpace() ||
                        result.dotsPerMeterX() != source.dotsPerMeterX() ||
                        result.text(QStringLiteral("Comment")) != source.text(QStringLiteral("Comment")) ||
                        source.cacheKey() != key)
                        return {{QStringLiteral("error"), QStringLiteral("Sharpen changed pixels, properties or original")}};
                    (control ? scalar : current).append(elapsed);
                }
            }
            rows.append(QJsonObject{{QStringLiteral("width"), size.width()}, {QStringLiteral("height"), size.height()},
                {QStringLiteral("source_format"), int(format)}, {QStringLiteral("scalar_ms"), scalar},
                {QStringLiteral("current_ms"), current}, {QStringLiteral("pixel_sha256"),
                 QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(
                     reinterpret_cast<const char *>(reference.constBits()), reference.sizeInBytes()), QCryptographicHash::Sha256).toHex())}});
        }
    }
    return {{QStringLiteral("samples"), rows}, {QStringLiteral("note"), QStringLiteral(
        "Three alternating scalar/current samples per case; 6/24 MP, RGB32/RGBA64. Includes working-format conversion, filtering and output conversion; excludes file I/O and UI. Exact pixels/profile/text/DPI and unchanged source checked outside timing. Compare identical profiler builds before/after in alternating fresh processes.")}};
}
QJsonObject profileMetadataOpen(const QString &path) {
    ix::CodecRegistry registry;
    // Prime plugin/metadata initialization, not the document cache: each
    // measured call below reads through the registry and decodes again.
    const auto reference = registry.read(path);
    if (!reference.ok())
        return {{QStringLiteral("error"), reference.error}};
    const auto &expected = reference.document;
    QJsonArray times;
#ifdef Q_OS_WIN
    IO_COUNTERS before{}, after{};
    const bool haveBefore = GetProcessIoCounters(GetCurrentProcess(), &before);
#endif
    for (int pass = 0; pass < 15; ++pass) {
        QElapsedTimer timer;
        timer.start();
        const auto result = registry.read(path);
        const auto elapsed = timer.nsecsElapsed() / 1e6;
        if (!result.ok())
            return {{QStringLiteral("error"), result.error}};
        const auto &actual = result.document;
        if (actual.pixels != expected.pixels || actual.pixels.colorSpace() != expected.pixels.colorSpace() ||
            actual.sourceHasExif != expected.sourceHasExif || actual.metadataWarning != expected.metadataWarning ||
            bool(actual.metadata) != bool(expected.metadata) ||
            (actual.metadata && (actual.metadata->tags != expected.metadata->tags ||
                                 actual.metadata->hasExif != expected.metadata->hasExif ||
                                 actual.metadata->hasIptc != expected.metadata->hasIptc ||
                                 actual.metadata->hasXmp != expected.metadata->hasXmp)))
            return {{QStringLiteral("error"), QStringLiteral("Repeated decode/metadata results differ")}};
        times.append(elapsed);
    }
#ifdef Q_OS_WIN
    const bool haveAfter = GetProcessIoCounters(GetCurrentProcess(), &after);
#endif
    const auto hash = QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char *>(expected.pixels.constBits()), expected.pixels.sizeInBytes()),
        QCryptographicHash::Sha256).toHex();
    QJsonObject report{{QStringLiteral("read_ms"), times},
                       {QStringLiteral("format"), QString::fromLatin1(expected.format)},
                       {QStringLiteral("width"), expected.pixels.width()},
                       {QStringLiteral("height"), expected.pixels.height()},
                       {QStringLiteral("file_bytes"), expected.fileBytes},
                       {QStringLiteral("pixel_sha256"), QString::fromLatin1(hash)},
                       {QStringLiteral("metadata_available"), bool(expected.metadata)},
                       {QStringLiteral("source_has_metadata"), expected.sourceHasExif},
                       {QStringLiteral("metadata_tags"), expected.metadata ? expected.metadata->tags.size() : 0},
                       {QStringLiteral("metadata_warning"), !expected.metadataWarning.isEmpty()},
                       {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
                       {QStringLiteral("note"), QStringLiteral("15 full registry reads after priming. Warm OS cache; no document cache or window presentation. Windows counters are process I/O, not physical disk traffic. Use identical profiler/fixtures and alternating before/after processes.")}};
#ifdef Q_OS_WIN
    if (haveBefore && haveAfter) {
        report.insert(QStringLiteral("read_operations"), qint64(after.ReadOperationCount - before.ReadOperationCount));
        report.insert(QStringLiteral("read_bytes"), qint64(after.ReadTransferCount - before.ReadTransferCount));
        report.insert(QStringLiteral("other_operations"), qint64(after.OtherOperationCount - before.OtherOperationCount));
    }
#endif
    return report;
}
QJsonObject profilePreviewReuse() {
    QTemporaryDir temporary(QDir::current().filePath(QStringLiteral("build/preview-reuse-XXXXXX")));
    if (!temporary.isValid())
        return {{QStringLiteral("error"), QStringLiteral("Cannot create preview fixtures")}};
    QImage pixels(4000, 3000, QImage::Format_RGB32);
    pixels.setColorSpace(QColorSpace::SRgb);
    for (int y = 0; y < pixels.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(pixels.scanLine(y));
        for (int x = 0; x < pixels.width(); ++x)
            row[x] = qRgb((x * 7 + y) % 256, (x + y * 3) % 256, (x * 2 + y * 11) % 256);
    }
    QJsonArray cases;
    ix::CodecRegistry registry;
    const auto restore = qScopeGuard([] {
        ix::ImageLoadCache::clear();
        ix::ImageLoadCache::configure(192, 16, true);
    });
    for (const auto &format : {QStringLiteral("png"), QStringLiteral("bmp")}) {
        const auto path = temporary.filePath(QStringLiteral("fixture.") + format);
        if (!pixels.save(path))
            return {{QStringLiteral("error"), QStringLiteral("Cannot save preview fixture")}};
        for (const QSize bounds : {QSize(68, 50), QSize(512, 512)}) {
            QJsonArray decoder, borrowed, decoderPrime, borrowedPrime;
            for (int sample = 0; sample < 5; ++sample) {
                for (int arm = 0; arm < 2; ++arm) {
                    const bool reuse = (sample + arm) % 2;
                    ix::ImageLoadCache::clear();
                    ix::ImageLoadCache::configure(reuse ? 192 : 0, 0, true);
                    QElapsedTimer timer;
                    timer.start();
                    const auto original = ix::ImageLoadCache::read(registry, path);
                    const auto prime = timer.nsecsElapsed() / 1e6;
                    if (!original.ok())
                        return {{QStringLiteral("error"), original.error}};
                    const auto key = original.document.pixels.cacheKey();
                    const auto expected = original.document.pixels.scaled(bounds, Qt::KeepAspectRatio,
                                                                         Qt::SmoothTransformation);
                    timer.restart();
                    const auto preview = ix::loadPreview(path, bounds);
                    const auto elapsed = timer.nsecsElapsed() / 1e6;
                    if (preview.isNull() || preview != expected || original.document.pixels.cacheKey() != key)
                        return {{QStringLiteral("error"), QStringLiteral("Preview pixels or original changed")}};
                    (reuse ? borrowed : decoder).append(elapsed);
                    (reuse ? borrowedPrime : decoderPrime).append(prime);
                }
            }
            cases.append(QJsonObject{{QStringLiteral("format"), format},
                                     {QStringLiteral("bounds"), QStringLiteral("%1x%2").arg(bounds.width()).arg(bounds.height())},
                                     {QStringLiteral("decoder_ms"), decoder},
                                     {QStringLiteral("borrowed_ms"), borrowed},
                                     {QStringLiteral("decoder_prime_ms"), decoderPrime},
                                     {QStringLiteral("borrowed_prime_ms"), borrowedPrime}});
        }
    }
    return {{QStringLiteral("cases"), cases}, {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
            {QStringLiteral("note"), QStringLiteral("Warm filesystem, 12 MP PNG/BMP, original decoded before every timed preview. Alternating order, preview cache disabled, exact pixel comparison. Priming cost reported separately; not first-open or end-to-end viewer latency.")}};
}
class FirstImagePaint final : public QObject {
  public:
    std::function<void()> completed;
    bool pending = false;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint && pending) {
            pending = false;
            QTimer::singleShot(0, this, [this] { if (completed) completed(); });
        }
        return false;
    }
};
QJsonObject profileSupersededOpen(const QString &stale, const QString &latest) {
    QTemporaryDir folder(QDir::current().filePath(QStringLiteral("build/supersede-profile-XXXXXX")));
    if(!folder.isValid() || !QFileInfo(stale).isFile() || !QFileInfo(latest).isFile())
        return {{QStringLiteral("error"),QStringLiteral("Missing profile/fixtures")}};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,folder.path());
    qApp->setProperty("ixStorageData",folder.path());
    QSettings().setValue(QStringLiteral("view/previewStrip"),false);
    QSettings().setValue(QStringLiteral("navigation/askToSave"),false);
    ix::initializeImageMemoryLimit(1024); ix::configureImageStorage({false});
    ix::MainWindow window; window.resize(1200,800);
    window.setAttribute(Qt::WA_ShowWithoutActivating); window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    window.show(); QTest::qWait(150); ix::ImageLoadCache::clear();
    auto *view=window.findChild<ix::ImageView *>(); FirstImagePaint observer;
    view->viewport()->installEventFilter(&observer);
    auto *pool=QThreadPool::globalInstance(); pool->waitForDone(); const int threads=pool->maxThreadCount(); pool->setMaxThreadCount(1);
    QSemaphore entered,resume; auto block=QtConcurrent::run([&] { entered.release(); resume.acquire(); });
    const auto cleanup=qScopeGuard([&] { resume.release(); block.waitForFinished(); pool->waitForDone(); pool->setMaxThreadCount(threads); });
    if(!entered.tryAcquire(1,3000)) return {{QStringLiteral("error"),QStringLiteral("Worker gate deadline")}};
    QEventLoop loop; QTimer deadline; deadline.setSingleShot(true); QElapsedTimer elapsed;
    double installed=-1,painted=-1; QStringList installedPaths;
    QObject::connect(view,&ix::ImageView::selectionChanged,&loop,[&] {
        if(!window.document().path.isEmpty()) installedPaths.append(window.document().path);
        if(window.document().path==latest && installed<0) {
            installed=elapsed.nsecsElapsed()/1e6; observer.pending=true;
        }
    });
    observer.completed=[&] { painted=elapsed.nsecsElapsed()/1e6; loop.quit(); };
    QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
    window.openPath(stale);
    if(!window.isBusy()) return {{QStringLiteral("error"),QStringLiteral("Initial load was not queued")}};
    elapsed.start(); deadline.start(30000); window.openPath(latest); resume.release(); loop.exec(); observer.completed={};
    if(painted<0 || window.document().path!=latest) return {{QStringLiteral("error"),QStringLiteral("Replacement paint deadline")}};
    const auto retained=ix::ImageLoadCache::retainedBytes();
    const auto hash=[](const QImage &image) { return QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex()); };
    const auto originalHash=hash(window.document().pixels),displayHash=hash(view->image());
    ix::CodecRegistry registry; int staleMisses=0;
    if(!ix::ImageLoadCache::read(registry,stale,0,[&] { ++staleMisses; }).ok())
        return {{QStringLiteral("error"),QStringLiteral("Verification read failed")}};
    return {{QStringLiteral("installed_ms"),installed},{QStringLiteral("paint_ms"),painted},
        {QStringLiteral("stale_installed"),installedPaths.contains(stale)},{QStringLiteral("stale_verification_misses"),staleMisses},
        {QStringLiteral("retained_before_verification"),retained},{QStringLiteral("original_sha256"),originalHash},
        {QStringLiteral("display_sha256"),displayHash},
        {QStringLiteral("note"),QStringLiteral("Shown 1200x800 Windows window, already running; global worker gate queues the first open, then a newer explicit open is submitted before release. Time is newer request to Qt paint. 192+16MiB caches; previews hidden; warm OS caches. Post-timer cache-miss probe detects whether stale pixels were decoded. Not ordinary single-open or keyboard-navigation latency.")}};
}
QJsonObject profilePreviewHandoff(const QString &path) {
    QTemporaryDir folder(QDir::current().filePath(QStringLiteral("build/handoff-profile-XXXXXX")));
    if (!folder.isValid() || !QFileInfo(path).isFile()) return {{QStringLiteral("error"),QStringLiteral("Missing fixture/profile")}};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,folder.path());
    qApp->setProperty("ixStorageData",folder.path());
    QSettings().setValue(QStringLiteral("view/previewStrip"),false);
    ix::initializeImageMemoryLimit(1024); ix::configureImageStorage({false});
    ix::MainWindow window; window.resize(1200,800);
    window.setAttribute(Qt::WA_ShowWithoutActivating); window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    window.show(); QTest::qWait(200);
    ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192,16,true);
    QElapsedTimer timer; timer.start();
    const auto preview=ix::loadPreview(path,{68,50});
    const double previewMs=timer.nsecsElapsed()/1e6;
    if(preview.isNull()) return {{QStringLiteral("error"),QStringLiteral("Preview failed")}};
    const auto retained=ix::ImageLoadCache::retainedBytes();
    auto *view=window.findChild<ix::ImageView *>(); FirstImagePaint observer;
    view->viewport()->installEventFilter(&observer);
    QEventLoop loop; QTimer deadline; deadline.setSingleShot(true);
    double installed=-1,painted=-1;
    QObject::connect(view,&ix::ImageView::selectionChanged,&loop,[&] {
        if(window.document().path==path && !window.document().empty() && installed<0) {
            installed=timer.nsecsElapsed()/1e6; observer.pending=true;
        }
    });
    observer.completed=[&] { painted=timer.nsecsElapsed()/1e6; loop.quit(); };
    QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
    timer.restart(); deadline.start(30000); window.openPath(path); loop.exec(); observer.completed={};
    if(painted<0) return {{QStringLiteral("error"),QStringLiteral("Foreground paint deadline")}};
    const auto hash=[](const QImage &image) { return QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex()); };
    const auto &document=window.document();
    return {{QStringLiteral("preview_ms"),previewMs},{QStringLiteral("installed_ms"),installed},{QStringLiteral("paint_ms"),painted},
        {QStringLiteral("combined_ms"),previewMs+painted},{QStringLiteral("retained_before_open"),retained},
        {QStringLiteral("original_sha256"),hash(document.pixels)},{QStringLiteral("display_sha256"),hash(view->image())},
        {QStringLiteral("preview_sha256"),hash(preview)},{QStringLiteral("dpi_x"),document.dpiX()},
        {QStringLiteral("dpi_y"),document.dpiY()},{QStringLiteral("format"),QString::fromLatin1(document.format)},
        {QStringLiteral("metadata_tags"),document.metadata ? QJsonObject::fromVariantMap([&] {
            QVariantMap tags; for(auto it=document.metadata->tags.cbegin();it!=document.metadata->tags.cend();++it) tags.insert(it.key(),it.value()); return tags;
        }()) : QJsonObject()},
        {QStringLiteral("note"),QStringLiteral("One real thumbnail followed by first foreground open in a shown, already-running 1200x800 window; warm OS caches. Preview and foreground times separately and combined. Qt paint events, not compositor latency. Hashing outside timers; 192+16MiB caches, 1GiB image budget.")}};
}
QJsonObject profileConcurrentPreview(const QString &path, bool uncontended) {
    ix::initializeImageMemoryLimit(1024); ix::configureImageStorage({false});
    ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192,16,true);
    ix::CodecRegistry registry; std::barrier start(uncontended ? 2 : 3);
    QElapsedTimer elapsed;
    double foregroundMs=0,previewMs=0;
    std::future<QImage> thumbnail;
    if (!uncontended) thumbnail=std::async(std::launch::async,[&] {
        start.arrive_and_wait(); const auto result=ix::loadPreview(path,{68,50});
        previewMs=elapsed.nsecsElapsed()/1e6; return result;
    });
    auto foreground=std::async(std::launch::async,[&] {
        start.arrive_and_wait(); auto result=ix::loadImage(registry,{path,0,true,false,false});
        foregroundMs=elapsed.nsecsElapsed()/1e6; return result;
    });
    qint64 privateBefore=0,privatePeak=0,cpuBefore=0,cpuAfter=0;
#ifdef Q_OS_WIN
    const auto memory=[] { PROCESS_MEMORY_COUNTERS_EX data{}; data.cb=sizeof(data);
        return GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&data),sizeof(data)) ? qint64(data.PrivateUsage) : qint64(-1); };
    const auto cpu=[] { FILETIME created{},exited{},kernel{},user{};
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return qint64(-1);
        return qint64((quint64(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+
            qint64((quint64(user.dwHighDateTime)<<32)|user.dwLowDateTime); };
    privatePeak=privateBefore=memory(); cpuBefore=cpu();
#endif
    elapsed.start(); start.arrive_and_wait();
    while(foreground.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready ||
          (thumbnail.valid() && thumbnail.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)) {
#ifdef Q_OS_WIN
        privatePeak=std::max(privatePeak,memory());
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto document=foreground.get(); const auto preview=thumbnail.valid() ? thumbnail.get() : QImage();
    const auto wallMs=elapsed.nsecsElapsed()/1e6;
#ifdef Q_OS_WIN
    cpuAfter=cpu(); privatePeak=std::max(privatePeak,memory());
#endif
    if(!document.error.isEmpty() || (!uncontended && preview.isNull()))
        return {{QStringLiteral("error"),document.error.isEmpty() ? QStringLiteral("Missing preview") : document.error}};
    const auto hash=[](const QImage &image) { return QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex()); };
    QJsonValue cpuMs=QJsonValue::Null,peakBytes=QJsonValue::Null;
#ifdef Q_OS_WIN
    if(cpuBefore>=0 && cpuAfter>=0) cpuMs=(cpuAfter-cpuBefore)/10000.0;
    if(privateBefore>=0 && privatePeak>=privateBefore) peakBytes=privatePeak-privateBefore;
#endif
    return {{QStringLiteral("foreground_ms"),foregroundMs},{QStringLiteral("preview_ms"),previewMs},
        {QStringLiteral("wall_ms"),wallMs},{QStringLiteral("cpu_ms"),cpuMs},
        {QStringLiteral("private_peak_delta"),peakBytes},{QStringLiteral("uncontended"),uncontended},
        {QStringLiteral("original_sha256"),hash(document.document.pixels)},{QStringLiteral("display_sha256"),hash(document.display)},
        {QStringLiteral("preview_sha256"),hash(preview)},
        {QStringLiteral("note"),QStringLiteral("Real thumbnail and foreground worker load released by a barrier; fresh process, warm OS caches; 2ms private-commit sampling, process CPU time. No GUI painting/startup or whole-process RAM-cap claim. Hashing outside timers. uncontended runs only the foreground load.")}};
}
QJsonObject profileCachedNavigation() {
    QTemporaryDir folder(QDir::current().filePath(QStringLiteral("build/navigation-profile-XXXXXX")));
    if (!folder.isValid()) return {{QStringLiteral("error"),QStringLiteral("Temporary profile failed")}};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,folder.path());
    qApp->setProperty("ixStorageData",folder.filePath(QStringLiteral("profile")));
    QSettings().setValue(QStringLiteral("navigation/askToSave"),false);
    QSettings().setValue(QStringLiteral("view/previewStrip"),false);
    ix::initializeImageMemoryLimit(1024); ix::configureImageStorage({false});
    QJsonArray scenarios;
    const auto hash=[](const QImage &image) {
        return QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex());
    };
    const bool srgbOnly=QCoreApplication::arguments().contains(QStringLiteral("--navigation-srgb-only"));
    for (int scenario=0;scenario<3;++scenario) {
        if (srgbOnly && scenario!=0) continue;
        ix::ImageLoadCache::clear();
        const bool wide=scenario==2;
        const auto root=folder.filePath(QStringLiteral("case-%1").arg(scenario)); QDir().mkpath(root);
        QStringList paths;
        for (int file=0;file<2;++file) {
            QImage image(wide ? QSize(3000,2000) : QSize(1920,1280),wide ? QImage::Format_RGBA64 : QImage::Format_RGB32);
            image.setColorSpace(scenario==0 ? QColorSpace::SRgb : QColorSpace::DisplayP3);
            for (int y=0;y<image.height();++y) {
                if (wide) {
                    auto *row=reinterpret_cast<QRgba64 *>(image.scanLine(y));
                    for (int x=0;x<image.width();++x) row[x]=QRgba64::fromRgba64(quint16(x*17+file*9000),quint16(y*29),quint16(x*3+y*11),quint16(30000+(x+y)%30000));
                } else {
                    auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));
                    for (int x=0;x<image.width();++x) row[x]=qRgb((x/8+file*40)%256,(y/7)%256,(x/13+y/11)%256);
                }
            }
            const auto path=root+QStringLiteral("/%1.").arg(file)+(wide ? QStringLiteral("png") : QStringLiteral("jpg"));
            if (!image.save(path)) return {{QStringLiteral("error"),QStringLiteral("Fixture write failed")}};
            paths.append(path);
        }
        ix::MainWindow window; window.resize(1200,800);
        window.setAttribute(Qt::WA_ShowWithoutActivating); window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        window.show(); QTest::qWait(150);
        auto *view=window.findChild<ix::ImageView *>();
        FirstImagePaint observer; view->viewport()->installEventFilter(&observer);
        QJsonArray first, warmed; QStringList originalHashes, displayHashes;
        for (int pass=0;pass<22;++pass) {
            const auto path=paths[pass%2];
            QEventLoop loop; QTimer heartbeat,deadline; deadline.setSingleShot(true);
            QElapsedTimer elapsed;
            double installed=-1,painted=-1,previous=0,maxGap=0;
            const auto ready=QObject::connect(view,&ix::ImageView::selectionChanged,&loop,[&] {
                if (window.document().path==path && !window.document().empty() && installed<0) {
                    installed=elapsed.nsecsElapsed()/1e6; observer.pending=true;
                }
            });
            observer.completed=[&] { painted=elapsed.nsecsElapsed()/1e6; loop.quit(); };
            QObject::connect(&heartbeat,&QTimer::timeout,&loop,[&] {
                const auto now=elapsed.nsecsElapsed()/1e6; maxGap=std::max(maxGap,now-previous); previous=now;
            });
            QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
            elapsed.start(); heartbeat.start(1); deadline.start(30000);
            window.openPath(path); const auto submitted=elapsed.nsecsElapsed()/1e6;
            loop.exec(); observer.completed={}; QObject::disconnect(ready);
            if (painted<0 || window.document().path!=path) return {{QStringLiteral("error"),QStringLiteral("Navigation paint deadline")}};
            const QJsonObject sample{{QStringLiteral("submit_ms"),submitted},{QStringLiteral("installed_ms"),installed},
                {QStringLiteral("paint_ms"),painted},{QStringLiteral("max_gui_gap_ms"),maxGap}};
            (pass<2 ? first : warmed).append(sample);
            const auto original=hash(window.document().pixels), display=hash(view->image());
            if (pass<2) { originalHashes.append(original); displayHashes.append(display); }
            else if (original!=originalHashes[pass%2] || display!=displayHashes[pass%2])
                return {{QStringLiteral("error"),QStringLiteral("Cached navigation changed original or display pixels")}};
        }
        scenarios.append(QJsonObject{{QStringLiteral("scenario"),scenario},{QStringLiteral("first_opens"),first},
            {QStringLiteral("cached_switches"),warmed},{QStringLiteral("original_hashes"),QJsonArray::fromStringList(originalHashes)},
            {QStringLiteral("display_hashes"),QJsonArray::fromStringList(displayHashes)}});
    }
    ix::ImageLoadCache::clear();
    return {{QStringLiteral("scenarios"),scenarios},{QStringLiteral("qt"),QString::fromLatin1(qVersion())},
        {QStringLiteral("srgb_only"),srgbOnly},
        {QStringLiteral("note"),QStringLiteral("Shown 1200x800 Windows window, already running; 1GiB image budget, 192MiB original/presentation cache, 16MiB thumbnails, previews hidden. Alternating two real files: sRGB 2.4MP JPEG control, P3 2.4MP JPEG, P3 6MP RGBA64 PNG; srgb_only selects just the control. First two opens reported separately; 20 warm switches per case. Paint observed through Qt events, not compositor. Fixture generation and SHA256 validation excluded; OS cache not flushed.")}};
}
QJsonObject profileFirstOpen(const QString &path) {
    qInfo("ix-first-open profile-start");
    QTemporaryDir profile(QDir::current().filePath(QStringLiteral("build/first-open-XXXXXX")));
    if (!profile.isValid() || !QFileInfo(path).isFile())
        return {{QStringLiteral("error"), QStringLiteral("Missing image or temporary profile")}};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    qApp->setProperty("ixStorageData", profile.path());
    qInfo("ix-first-open create-window");
    ix::MainWindow window;
    window.resize(1200, 800);
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    qInfo("ix-first-open show-window");
    window.show();
    QTest::qWait(200); // The window is already running before the measured open.
    qInfo("ix-first-open window-ready exposed=%d", window.windowHandle() && window.windowHandle()->isExposed());
    auto *view = window.findChild<ix::ImageView *>();
    FirstImagePaint observer;
    view->viewport()->installEventFilter(&observer);
    QJsonArray samples;
    for (int pass = 0; pass < 3; ++pass) {
        qInfo("ix-first-open pass %d start", pass);
        ix::ImageLoadCache::clear(); // Repeat decoding, not just cache retrieval.
        QEventLoop loop;
        QTimer heartbeat, deadline;
        QElapsedTimer elapsed;
        double installed = -1, painted = -1, previousBeat = 0, maxGap = 0;
        auto ready = QObject::connect(view, &ix::ImageView::selectionChanged, &loop, [&] {
            if (window.document().path == path && !window.document().empty() && installed < 0) {
                installed = elapsed.nsecsElapsed() / 1e6;
                observer.pending = true;
            }
        });
        observer.completed = [&] {
            painted = elapsed.nsecsElapsed() / 1e6;
            loop.quit();
        };
        QObject::connect(&heartbeat, &QTimer::timeout, &loop, [&] {
            const double now = elapsed.nsecsElapsed() / 1e6;
            maxGap = std::max(maxGap, now - previousBeat);
            previousBeat = now;
        });
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        elapsed.start();
        heartbeat.start(1);
        deadline.start(30000);
        window.openPath(path);
        const double submitted = elapsed.nsecsElapsed() / 1e6;
        loop.exec();
        observer.completed = {};
        QObject::disconnect(ready);
        if (painted < 0)
            return {{QStringLiteral("error"), QStringLiteral("Image was not painted within 30 seconds")}};
        qInfo("ix-first-open pass %d painted", pass);
        samples.append(QJsonObject{{QStringLiteral("open_return_ms"), submitted},
            {QStringLiteral("document_install_ms"), installed},
            {QStringLiteral("paint_completed_ms"), painted},
            {QStringLiteral("max_gui_heartbeat_gap_ms"), maxGap}});
        QTest::qWait(50);
    }
    qInfo("ix-first-open destroy-window");
    return {{QStringLiteral("samples"), samples},
        {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("note"), QStringLiteral("Already-running empty window, isolated default profile, three opens of the same image with document caches cleared. First sample includes lazy initialization. Paint completion is an event-loop observation, not a compositor/display measurement. OS file caches are not cleared.")}};
}
QJsonObject profileRawCache() {
    const auto folder = qEnvironmentVariable("IX_RAW_FIXTURES",
        QCoreApplication::applicationDirPath() + QStringLiteral("/quality-fixtures"));
    const auto path = QDir(folder).filePath(QStringLiteral("pentax-k10d.dng"));
    if (!QFileInfo::exists(path)) return {{QStringLiteral("error"), QStringLiteral("DNG fixture missing")}};
    ix::CodecRegistry registry;
    QJsonArray rows;
    const auto reset = qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192, 16, true); });
    for (int previewMiB : {16, 0}) {
        ix::ImageLoadCache::clear();
        ix::ImageLoadCache::configure(192, previewMiB, true);
        const ix::ImageLoadRequest request{path, 0, true, false, true};
        const auto prime = ix::loadImage(registry, request);
        if (!prime.error.isEmpty() || prime.document.empty()) return {{QStringLiteral("error"), prime.error}};
        if (ix::loadPreview(path, {1600, 1100}).isNull())
            return {{QStringLiteral("error"), QStringLiteral("Embedded RAW preview unavailable")}};
        QJsonArray before, after;
        int oldPreviews = 0, newPreviews = 0;
        for (int sample = 0; sample < 5; ++sample) {
            for (bool old : {sample % 2 == 0, sample % 2 != 0}) {
                QElapsedTimer timer;
                timer.start();
                auto chosen = request;
                if (old) {
                    if (!ix::loadPreview(path, {1600, 1100}).isNull()) ++oldPreviews;
                    chosen.rawPreview = false;
                }
                const auto result = ix::loadImage(registry, chosen, [&](QImage) { ++newPreviews; });
                (old ? before : after).append(timer.nsecsElapsed() / 1e6);
                if (!result.error.isEmpty() || result.document.pixels.cacheKey() != prime.document.pixels.cacheKey()
                    || result.display != prime.display)
                    return {{QStringLiteral("error"), QStringLiteral("Cached RAW pixels/display changed")}};
            }
        }
        rows.append(QJsonObject{{QStringLiteral("preview_cache_MiB"), previewMiB},
            {QStringLiteral("before_ms"), before}, {QStringLiteral("after_ms"), after},
            {QStringLiteral("before_preview_deliveries"), oldPreviews}, {QStringLiteral("after_preview_deliveries"), newPreviews}});
    }
    return {{QStringLiteral("samples"), rows}, {QStringLiteral("note"), QStringLiteral(
        "Five alternating samples per preview-cache budget; one cached Pentax DNG, warm filesystem, color management enabled. "
        "Reference always requests the preview before the cached original; current pipeline requests it only on a miss. "
        "Includes display conversion, excludes GUI painting. No claim for first RAW development, uncached originals or all cameras.")}};
}
QJsonObject profileSlideshow() {
    QStringList files;
    for (int i = 0; i < 10000; ++i) files.append(QString::number(i));
    QJsonArray before, after;
    for (int repeat = 0; repeat < 5; ++repeat) {
        for (bool old : {repeat % 2 == 0, repeat % 2 != 0}) {
            ix::SlideshowController controller;
            auto remaining = files;
            remaining.removeFirst();
            QStringList selected;
            QElapsedTimer timer;
            timer.start();
            for (int step = 0; step < 1000; ++step) {
                if (old) {
                    const QSet<QString> available(files.begin(), files.end());
                    remaining.removeIf([&](const QString &path) { return !available.contains(path); });
                    selected.append(remaining.takeAt(QRandomGenerator::global()->bounded(int(remaining.size()))));
                } else selected.append(controller.next(files, 0, true, false).path);
            }
            (old ? before : after).append(timer.nsecsElapsed() / 1e6);
            const QSet<QString> unique(selected.begin(), selected.end());
            if (unique.size() != 1000 || unique.contains(files.first()) || unique.contains(QString()))
                return {{QStringLiteral("error"), QStringLiteral("Invalid slideshow round")}};
        }
    }
    return {{QStringLiteral("before_ms"), before}, {QStringLiteral("after_ms"), after},
        {QStringLiteral("note"), QStringLiteral("Five alternating in-process samples; 1000 selections from 10000 unchanged paths. Previous QSet/filter/random-removal algorithm versus current controller. No file I/O, decoding or rendering; not a whole-viewer speedup.")}};
}
QJsonObject profileRawPreviews() {
    // Optional baseline DLL/so is a local measurement artifact, never shipped.
    QLibrary baseline(qEnvironmentVariable("IX_RAW_BASELINE_MODULE"));
    using Read = bool (*)(const QString &, QImage &);
    const auto oldRead = reinterpret_cast<Read>(baseline.resolve("ixReadRawPreview"));
    const auto folder = qEnvironmentVariable("IX_RAW_FIXTURES",
        QCoreApplication::applicationDirPath() + QStringLiteral("/quality-fixtures"));
    QJsonArray rows;
    for (const auto &file : QDir(folder).entryInfoList(QDir::Files)) {
        if (!ix::isRawImage(file.fileName())) continue;
        for (const QSize bounds : {QSize(68, 50), QSize(512, 512)}) {
            QJsonArray oldTimes, newTimes;
            QSize oldSize, newSize;
            for (int repeat = 0; repeat < 7; ++repeat) {
                for (bool old : {repeat % 2 == 0, repeat % 2 != 0}) {
                    if (old && !oldRead) continue;
                    ix::ImageLoadCache::clear();
                    QElapsedTimer timer; timer.start();
                    QImage image;
                    if (old) {
                        oldRead(file.absoluteFilePath(), image);
                        image = image.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    } else image = ix::loadPreview(file.absoluteFilePath(), bounds);
                    (old ? oldTimes : newTimes).append(timer.nsecsElapsed() / 1e6);
                    if (image.isNull()) return {{QStringLiteral("error"), file.fileName()}};
                    (old ? oldSize : newSize) = image.size();
                }
            }
            if (oldRead && oldSize != newSize)
                return {{QStringLiteral("error"), QStringLiteral("Preview dimensions changed: ") + file.fileName()}};
            rows.append(QJsonObject{{QStringLiteral("file"), file.fileName()},
                {QStringLiteral("bounds"), QStringLiteral("%1x%2").arg(bounds.width()).arg(bounds.height())},
                {QStringLiteral("before_ms"), oldTimes}, {QStringLiteral("after_ms"), newTimes}});
        }
    }
    return {{QStringLiteral("samples"), rows}, {QStringLiteral("note"),
        QStringLiteral("Seven alternating calls, warm filesystem, empty thumbnail cache. Measures embedded previews only, including RAW parsing; optional baseline uses the previously built module. Not full RAW development.")}};
}
QJsonObject profileAnimations(const QString &root, const ix::CodecRegistry &registry) {
    QJsonArray rows;
    for (int budget : {0, 16, 192}) {
        ix::ImageLoadCache::configure(budget, 16, true);
        for (int count : {20, 40, 80}) {
            const auto path = root + QStringLiteral("/animation-%1.png").arg(count);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return {{QStringLiteral("error"), file.errorString()}};
            file.write(ix::test::apngFixture(count));
            file.close();
            QJsonArray sequential, stateless, sequentialCompositions, statelessCompositions;
            QJsonArray sequentialParses, statelessParses;
            for (int repeat = 0; repeat < 3; ++repeat)
                for (bool reset : {repeat % 2 == 0, repeat % 2 != 0}) {
                    ix::ImageLoadCache::clear();
                    const auto before = ix::ApngCodec::cacheStats();
                    QElapsedTimer timer;
                    timer.start();
                    for (int page = 0; page < count; ++page) {
                        if (reset)
                            ix::ApngCodec::clearSession();
                        const auto frame = ix::ImageLoadCache::read(registry, path, page);
                        if (!frame.ok())
                            return {{QStringLiteral("error"), frame.error}};
                    }
                    const auto elapsed = timer.nsecsElapsed() / 1e6;
                    const auto after = ix::ApngCodec::cacheStats();
                    (reset ? stateless : sequential).append(elapsed);
                    (reset ? statelessCompositions : sequentialCompositions)
                        .append(double(after.framesComposited - before.framesComposited));
                    (reset ? statelessParses : sequentialParses)
                        .append(double(after.filesParsed - before.filesParsed));
                }
            rows.append(QJsonObject{{QStringLiteral("frames"), count},
                {QStringLiteral("frame_cache_mib"), budget},
                {QStringLiteral("session_ms"), sequential}, {QStringLiteral("stateless_ms"), stateless},
                {QStringLiteral("session_compositions"), sequentialCompositions},
                {QStringLiteral("stateless_compositions"), statelessCompositions},
                {QStringLiteral("session_parses"), sequentialParses},
                {QStringLiteral("stateless_parses"), statelessParses}});
        }
    }
    ix::ImageLoadCache::configure(192, 16, true);
    ix::ImageLoadCache::clear();
    return {{QStringLiteral("samples"), rows}, {QStringLiteral("note"),
        QStringLiteral("Windows Release/offscreen; 128x128 opaque APNG frames; warm filesystem; three alternating passes. The stateless control clears decoder state before every frame, recreating prior replay work. Each pass starts with an empty document cache; budgets are 0/16/192 MiB.")}};
}
QJsonObject profileInteraction(const QString &root) {
    QJsonObject report;
    const QImage source(root + QStringLiteral("/large.jpg"));
    for (const auto format : {QImage::Format_RGB32, QImage::Format_RGBA64}) {
        ix::ImageView view;
        view.resize(1200, 800);
        view.setImage(source.convertToFormat(format));
        view.setFit(ix::ImageView::Fit::Window);
        view.show();
        QTest::qWait(30);
        for (bool cached : {false, true}) {
            view.setDisplayCacheEnabled(cached);
            view.viewport()->repaint();
            view.viewport()->repaint();
            QJsonArray incremental, full;
            for (int i = 0; i < 40; ++i) {
            for (bool repaintAll : {i % 2 == 0, i % 2 != 0}) {
                view.setSelection(QRect(500, 500, 2500, 2000));
                QCoreApplication::processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
                QElapsedTimer timer;
                timer.start();
                view.setSelection(QRect(510 + i * 3, 510 + i * 2, 2500, 2000));
                if (repaintAll)
                    view.viewport()->update();
                QCoreApplication::processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
                (repaintAll ? full : incremental).append(timer.nsecsElapsed() / 1e6);
            }
            }
            const auto bits = format == QImage::Format_RGB32 ? QStringLiteral("8bit") : QStringLiteral("16bit");
            const auto prefix = bits + (cached ? QStringLiteral("_cached") : QStringLiteral("_uncached"));
            report.insert(prefix + QStringLiteral("_selection_ms"), incremental);
            report.insert(prefix + QStringLiteral("_selection_full_repaint_ms"), full);
            if (cached) {
                report.insert(bits + QStringLiteral("_selection_ms"), incremental);
                report.insert(bits + QStringLiteral("_selection_full_repaint_ms"), full);
            }
        }
    }
    for (const auto &extension : {QStringLiteral("jpg"), QStringLiteral("png")}) {
        QJsonArray times;
        for (int i = 0; i < 7; ++i) {
            QString warning;
            QElapsedTimer timer;
            timer.start();
            const auto metadata = ix::readMetadata(root + QStringLiteral("/large.") + extension, warning);
            if (!metadata)
                report.insert(extension + QStringLiteral("_metadata_warning"), warning);
            times.append(timer.nsecsElapsed() / 1e6);
        }
        report.insert(extension + QStringLiteral("_metadata_ms"), times);
    }
    report.insert(QStringLiteral("note"), QStringLiteral("24 MP, 1200x800 offscreen, warm filesystem. Alternating selection movement/full repaint in each cache mode; timings include GUI events. Cache modes receive two priming paints. Metadata is measured separately without image decoding."));
    return report;
}
QJsonObject profilePreviews(const QString &root) {
    QSettings().setValue(QStringLiteral("preview/previous"), 5);
    QSettings().setValue(QStringLiteral("preview/next"), 5);
    QStringList files;
    for (int i = 0; i < 13; ++i) {
        const auto path = root + QStringLiteral("/thumb-%1.png").arg(i);
        if (!QFileInfo::exists(path)) {
            QImage image(320, 240, QImage::Format_RGB32);
            image.fill(QColor::fromHsv(i * 25, 180, 190));
            if (!image.save(path))
                return {{QStringLiteral("error"), QStringLiteral("thumbnail fixture write failed")}};
        }
        files.append(path);
        ix::loadPreview(path, {68, 50});
    }
    ix::PreviewStrip strip;
    strip.resize(1200, 104);
    strip.show();
    strip.setFiles(files, 6);
    QTest::qWait(100);
    QJsonArray samples, rebuilt;
    for (int i = 0; i < 30; ++i) {
        for (bool reset : {i % 2 == 0, i % 2 != 0}) {
            strip.setFiles(files, 6);
            QTest::qWait(25);
            auto incoming = files;
            if (reset)
                incoming.detach();
            QElapsedTimer timer;
            timer.start();
            strip.setFiles(std::move(incoming), 7);
            QCoreApplication::processEvents();
            (reset ? rebuilt : samples).append(timer.nsecsElapsed() / 1e6);
            QTest::qWait(25);
        }
    }
    return {{QStringLiteral("preview_strip_step_ms"), samples},
            {QStringLiteral("preview_strip_force_rebuild_ms"), rebuilt},
            {QStringLiteral("note"), QStringLiteral("Ten neighbors, warm thumbnail cache, 1200x104 offscreen widget. Alternating order compares a shared-list step with a forced new directory snapshot in the same process; includes current UI events, excludes decode completion.")}};
}
QJsonObject profile(const QString &root, const ix::CodecRegistry &registry) {
    QJsonObject report;
    const auto folder = root + QStringLiteral("/folder-10000");
    auto measure = [&](const QString &name, auto fn, int repeats = 5) {
        QJsonArray samples;
        for (int i = 0; i < repeats; ++i) {
            QElapsedTimer timer;
            timer.start();
            fn();
            samples.append(timer.nsecsElapsed() / 1e6);
        }
        report.insert(name, samples);
    };
    const QStringList filters{QStringLiteral("*.png")};
    QFileInfoList entries;
    measure(QStringLiteral("directory_enumerate_no_permission_probe_ms"), [&] {
        entries = QDir(folder).entryInfoList(filters, QDir::Files, QDir::NoSort);
    }, 3);
    measure(QStringLiteral("directory_enumerate_readable_ms"), [&] {
        entries = QDir(folder).entryInfoList(filters, QDir::Files | QDir::Readable, QDir::NoSort);
    }, 3);
    QCollator collator(QLocale(QLocale::German, QLocale::Germany));
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::vector<QCollatorSortKey> keys;
    QStringList paths;
    measure(QStringLiteral("directory_paths_sortkeys_ms"), [&] {
        keys.clear(); paths.clear();
        for (const auto &file : entries) {
            paths.append(file.absoluteFilePath());
            keys.push_back(collator.sortKey(file.fileName()));
        }
    }, 3);
    measure(QStringLiteral("directory_sortkeys_only_ms"), [&] {
        keys.clear();
        for (const auto &file : entries)
            keys.push_back(collator.sortKey(file.fileName()));
    }, 3);
    measure(QStringLiteral("directory_paths_only_ms"), [&] {
        paths.clear();
        for (const auto &file : entries)
            paths.append(file.absoluteFilePath());
    }, 3);
    measure(QStringLiteral("directory_stream_readable_ms"), [&] {
        paths.clear();
        for (const auto &entry : QDirListing(folder, filters, QDirListing::IteratorFlag::FilesOnly))
            if (entry.isReadable())
                paths.append(entry.filePath());
    }, 3);
    measure(QStringLiteral("directory_stream_no_permission_probe_ms"), [&] {
        paths.clear();
        for (const auto &entry : QDirListing(folder, filters, QDirListing::IteratorFlag::FilesOnly))
            paths.append(entry.filePath());
    }, 3);
    report.insert(QStringLiteral("directory_entries"), entries.size());
    measure(QStringLiteral("directory_scan_single_extension_ms"), [&] {
        auto snapshot = ix::scanDirectory(folder, {QStringLiteral("png")}, 0, false, nullptr, false);
        if (snapshot.files.size() != entries.size())
            qFatal("Directory benchmark produced an incomplete index");
    }, 3);
    measure(QStringLiteral("directory_scan_all_extensions_ms"), [&] {
        auto snapshot = ix::scanDirectory(folder, registry.extensions(), 0, false, nullptr, false);
        if (snapshot.files.size() != entries.size())
            qFatal("Directory benchmark produced an incomplete index");
    }, 3);
    for (const auto &ext : {QStringLiteral("jpg"), QStringLiteral("png")}) {
        const auto path = root + QStringLiteral("/large.") + ext;
        measure(ext + QStringLiteral("_decode_only_ms"), [&] {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            auto image = reader.read();
            if (image.isNull())
                qFatal("Decode benchmark failed to read its fixture");
        });
        measure(ext + QStringLiteral("_metadata_only_ms"), [&] {
            QString warning;
            auto metadata = ix::readMetadata(path, warning);
            Q_UNUSED(metadata);
        });
    }
    measure(QStringLiteral("file_revision_1000_ms"), [&] {
        for (int i = 0; i < 1000; ++i)
            ix::imageFileKey(root + QStringLiteral("/large.jpg"));
    });
    QImage source(root + QStringLiteral("/large.jpg"));
    for (const auto &format : {QImage::Format_RGB32, QImage::Format_RGBA64}) {
        ix::ImageView view;
        view.resize(1200, 800);
        view.setImage(source.convertToFormat(format));
        view.setFit(ix::ImageView::Fit::Window);
        view.show();
        QCoreApplication::processEvents();
        QImage canvas(view.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        const auto prefix = format == QImage::Format_RGB32 ? QStringLiteral("display_8bit")
                                                         : QStringLiteral("display_16bit");
        for (bool cached : {false, true}) {
            view.setDisplayCacheEnabled(cached);
            QCoreApplication::processEvents();
            const auto suffix = cached ? QStringLiteral("_cached") : QStringLiteral("_uncached");
            measure(prefix + suffix + QStringLiteral("_repaint_ms"), [&] { view.viewport()->render(&canvas); }, 20);
            const auto pixels = view.image();
            measure(prefix + suffix + QStringLiteral("_first_paint_ms"), [&] {
                view.setImage(pixels);
                view.viewport()->render(&canvas);
            });
        }
    }
    report.insert(QStringLiteral("note"), QStringLiteral("Warm filesystem; timings are per operation, not end-to-end. Metadata and drawing retain the full image. Same fixtures and profiler must be used before and after."));
    report.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
    return report;
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("IxViewBench"));
    QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
    ix::configureDesktopAppearance();
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("windows")));
    if(app.arguments().contains(QStringLiteral("--profile-native-sharpen"))) {
        const auto report=profileNativeSharpen(QImage::Format(app.arguments().value(3).toInt()),
            {app.arguments().value(4).toInt(),app.arguments().value(5).toInt()});
        QFile output(app.arguments().value(1)); if(!output.open(QIODevice::WriteOnly))return 4;
        output.write(QJsonDocument(report).toJson()); return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if(app.arguments().contains(QStringLiteral("--profile-superseded-open"))) {
        const auto report=profileSupersededOpen(QFileInfo(app.arguments().value(3)).absoluteFilePath(),
            QFileInfo(app.arguments().value(4)).absoluteFilePath());
        QFile output(app.arguments().value(1)); if(!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(report).toJson()); return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-concurrent-preview"))) {
        const auto report=profileConcurrentPreview(QFileInfo(app.arguments().value(3)).absoluteFilePath(),
            app.arguments().contains(QStringLiteral("--uncontended")));
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(report).toJson()); return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-preview-handoff"))) {
        const auto report=profilePreviewHandoff(QFileInfo(app.arguments().value(3)).absoluteFilePath());
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(report).toJson()); return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-reader-input"))) {
        const auto report=profileReaderInput(QFileInfo(app.arguments().value(3)).absoluteFilePath());
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(report).toJson()); return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-cached-navigation"))) {
        const auto report=profileCachedNavigation(); QFile output(app.arguments().value(1));
        if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(report).toJson()); return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-save-progress"))) {
        const auto result=profileSaveProgress();
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(result).toJson()); return result.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--check-resize"))) {
        const auto result=checkResize(); QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(result).toJson()); return result.value(QStringLiteral("different_pixels")).toDouble() == 0 ? 0 : 5;
    }
    for (const auto &argument:app.arguments()) if (argument.startsWith(QStringLiteral("--probe-resize-axis="))) {
        int filter=5;
        for (const auto &value:app.arguments()) if(value.startsWith(QStringLiteral("--resize-filter="))) filter=value.sliced(16).toInt();
        QJsonObject result;
        try { result=probeLargeResize(argument.sliced(20).toInt(),filter); }
        catch (const std::exception &error) { result={{QStringLiteral("error"),QString::fromUtf8(error.what())}}; }
        QFile output(app.arguments().value(1)); if(!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(result).toJson()); return result.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-resize")) || app.arguments().contains(QStringLiteral("--profile-resize-matrix")) || app.arguments().contains(QStringLiteral("--profile-resize-wide"))) {
        int selectedCase = -1;
        for (const auto &value : app.arguments()) if (value.startsWith(QStringLiteral("--resize-case="))) {
            bool valid = false;
            selectedCase = value.sliced(14).toInt(&valid);
            if (!valid || selectedCase < 0) return 4;
        }
        const auto result=profileResize(app.arguments().contains(QStringLiteral("--profile-resize-matrix")),app.arguments().contains(QStringLiteral("--profile-resize-wide")),selectedCase);
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(result).toJson()); return result.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-grayscale"))) {
        const auto result = profileGrayscale();
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(result).toJson()); return result.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-auto-crop"))) {
        const auto result=profileAutoCrop();
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(result).toJson()); return result.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-native-pixel-edits"))) {
        const auto result=profileNativePixelEdits();
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(result).toJson()); return result.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-directory-filters"))) {
        const auto result=profileDirectoryFilters();
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(result).toJson()); return result.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-cache-admission"))) {
        const auto result=profileCacheAdmission();
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(result).toJson()); return result.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-open-filter"))) {
        auto modules = [] {
            QJsonArray names;
#ifdef Q_OS_WIN
            for (const auto &name : {QStringLiteral("kimg_raw.dll"), QStringLiteral("kimg_heif.dll"),
                 QStringLiteral("kimg_avif.dll"), QStringLiteral("kimg_exr.dll"), QStringLiteral("kimg_jxl.dll")})
                if (GetModuleHandleW(reinterpret_cast<const wchar_t *>(name.utf16()))) names.append(name);
#endif
            return names;
        };
        ix::CodecRegistry registry;
        const auto before = modules();
        QElapsedTimer timer; timer.start();
        const auto filter = registry.openFilter();
        const auto first = timer.nsecsElapsed()/1e6;
        const auto after = modules();
        QJsonArray warm;
        for (int i=0;i<5;++i) { timer.restart(); const auto next=registry.openFilter(); warm.append(timer.nsecsElapsed()/1e6); }
        const QJsonObject report{{QStringLiteral("first_filter_ms"),first}, {QStringLiteral("warm_filter_ms"),warm},
            {QStringLiteral("filter_characters"),filter.size()}, {QStringLiteral("filter_groups"),filter.count(QStringLiteral(";;"))+1},
            {QStringLiteral("heavy_plugins_before"),before}, {QStringLiteral("heavy_plugins_after"),after},
            {QStringLiteral("qt"),QString::fromLatin1(qVersion())},
            {QStringLiteral("note"),QStringLiteral("Fresh process, warm filesystem; filter preparation only, excludes file dialog and shell enumeration.")}};
        QFile output(app.arguments().value(1)); if (!output.open(QIODevice::WriteOnly)) return 4;
        output.write(QJsonDocument(report).toJson()); return 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-batch-results"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        out.write(QJsonDocument(profileBatchResults()).toJson());
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-batch-plan"))) {
        const auto report = profileBatchPlan();
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-interface-theme"))) {
        const auto report = profileInterfaceTheme(QFileInfo(app.arguments().value(3)).absoluteFilePath());
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-sharpen"))) {
        const auto report = profileSharpen();
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-metadata-open"))) {
        const auto report = profileMetadataOpen(QFileInfo(app.arguments().value(3)).absoluteFilePath());
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-preview-reuse"))) {
        const auto report = profilePreviewReuse();
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-first-open"))) {
        const auto input = QFileInfo(app.arguments().value(3)).absoluteFilePath();
        const auto report = profileFirstOpen(input);
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-raw-cache"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        const auto report = profileRawCache();
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-slideshow"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        const auto report = profileSlideshow();
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    QString root = QDir::current().filePath(QStringLiteral("build/perf-fixtures"));
    QDir().mkpath(root);
    ix::CodecRegistry registry;
    ix::ImageDocument d;
    QString jpeg = root + QStringLiteral("/large.jpg"), png = root + QStringLiteral("/large.png");
    if (!QFileInfo::exists(jpeg) || !QFileInfo::exists(png)) {
        d.pixels = QImage(6000, 4000, QImage::Format_RGB32);
        for (int y = 0; y < 4000; ++y) {
            auto *p = reinterpret_cast<QRgb *>(d.pixels.scanLine(y));
            for (int x = 0; x < 6000; ++x)
                p[x] = qRgb((x * 7 + y) % 256, (x + y * 3) % 256, (x * 2 + y * 11) % 256);
        }
        if (!registry.save(d, jpeg, "jpeg", 95, true).isEmpty() ||
            !registry.save(d, png, "png", 95, true).isEmpty())
            return 2;
        d.pixels = {};
    }
    if (app.arguments().contains(QStringLiteral("--profile-raw-previews"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        const auto report = profileRawPreviews();
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) || report.value(QStringLiteral("samples")).toArray().isEmpty() ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-animations"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly))
            return 4;
        const auto report = profileAnimations(root, registry);
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-interaction"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly))
            return 4;
        out.write(QJsonDocument(profileInteraction(root)).toJson());
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-previews"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly))
            return 4;
        out.write(QJsonDocument(profilePreviews(root)).toJson());
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile"))) {
        if (QDir(root + QStringLiteral("/folder-10000")).entryList({QStringLiteral("*.png")}, QDir::Files).size() != 10000) {
            qWarning("Run the standard benchmark once to prepare its 10000-file fixture first.");
            return 9;
        }
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly))
            return 4;
        out.write(QJsonDocument(profile(root, registry)).toJson());
        return 0;
    }
    QJsonObject report;
    QElapsedTimer timer;
    for (const auto &path : {jpeg, png}) {
        QJsonArray times;
        for (int i = 0; i < 5; ++i) {
            timer.start();
            auto r = registry.read(path);
            if (!r.ok())
                return 3;
            times.append(timer.nsecsElapsed() / 1e6);
        }
        report.insert(QFileInfo(path).suffix() + QStringLiteral("_24MP_ms"), times);
    }
    ix::ImageLoadCache::clear();
    QJsonArray largeCache;
    for (int i = 0; i < 5; ++i) {
        timer.start();
        auto result = ix::ImageLoadCache::read(registry, jpeg);
        if (!result.ok())
            return 8;
        largeCache.append(timer.nsecsElapsed() / 1e6);
    }
    report.insert(QStringLiteral("cached_24MP_jpeg_ms"), largeCache);
    QJsonArray startup;
    for (int i = 0; i < 5; ++i) {
        timer.start();
        {
            ix::MainWindow window;
            window.show();
            QCoreApplication::processEvents();
            startup.append(timer.nsecsElapsed() / 1e6);
        }
    }
    report.insert(QStringLiteral("window_offscreen_ms"), startup);

    QString folder = root + QStringLiteral("/folder-10000");
    QDir().mkpath(folder);
    for (int i = 0; i < 10000; ++i) {
        QFile file(folder + QStringLiteral("/image%1.png").arg(i));
        if (!file.exists()) {
            if (!file.open(QIODevice::WriteOnly))
                return 5;
            file.write("fixture");
        }
    }
    timer.start();
    auto snapshot = ix::scanDirectory(folder, {QStringLiteral("png")}, 0, false, nullptr, false);
    report.insert(QStringLiteral("folder_10000_scan_ms"), timer.nsecsElapsed() / 1e6);
    volatile qint64 sink = 0;
    timer.start();
    for (int i = 0; i < 10000; ++i)
        sink = sink + snapshot.files.indexOf(snapshot.files[i]);
    report.insert(QStringLiteral("folder_10000_linear_lookup_ms"), timer.nsecsElapsed() / 1e6);
    timer.start();
    for (int i = 0; i < 10000; ++i)
        sink = sink + snapshot.positions.value(snapshot.files[i]);
    report.insert(QStringLiteral("folder_10000_hash_lookup_ms"), timer.nsecsElapsed() / 1e6);
    QString small = root + QStringLiteral("/cache-2MP.jpg");
    if (!QFileInfo::exists(small)) {
        d.pixels = QImage(jpeg).scaled(1920, 1280);
        if (!registry.save(d, small, "jpeg", 95, true).isEmpty())
            return 6;
        d.pixels = {};
    }
    ix::ImageLoadCache::clear();
    QJsonArray cache;
    for (int i = 0; i < 5; ++i) {
        timer.start();
        auto read = ix::ImageLoadCache::read(registry, small);
        if (!read.ok())
            return 7;
        cache.append(timer.nsecsElapsed() / 1e6);
    }
    report.insert(QStringLiteral("cached_2MP_jpeg_ms"), cache);
    report.insert(QStringLiteral("note"),
                  QStringLiteral("Windows offscreen, warm filesystem; timings vary. Folder lookup compares "
                                 "identical data structures in this build. This benchmark does not measure RAW."));
    report.insert(QStringLiteral("platform"), QSysInfo::prettyProductName());
    report.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
    QFile out(app.arguments().value(1, QStringLiteral("tests/output/performance-before.json")));
    if (!out.open(QIODevice::WriteOnly))
        return 4;
    out.write(QJsonDocument(report).toJson());
    return 0;
}
