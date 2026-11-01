#include "core/image_storage.h"
#include "core/image_document.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/codec_registry.h"
#include "core/batch_pipeline.h"
#include "core/file_operations.h"
#include "core/preview_service.h"
#include "convolution_reference.h"
#include "auto_crop_reference.h"
#include "pixel_processing_reference.h"
#include "resample_reference.h"
#include "pdf_fixture.h"
#include <QtConcurrent>
#include "ui/main_window.h"
#include <QColorSpace>
#include <QJsonDocument>
#include <QImageReader>
#include <QSettings>
#include <QtTest>
#include <QtWidgets>
#include <limits>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#ifdef IX_QUALITY_LCMS
#include <lcms2.h>
#endif
class QualityTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_{QStringLiteral(IX_QUALITY_OUTPUT "/run-quality-XXXXXX")};
  private slots:
    void memoryPolicyCalculatesAutoAndOverrides() {
        QCOMPARE(ix::calculateImageMemoryLimit(16ULL*1024*ix::MiB), 8192*ix::MiB);
        QCOMPARE(ix::calculateImageMemoryLimit(4ULL*1024*ix::MiB), 2048*ix::MiB);
        QCOMPARE(ix::calculateImageMemoryLimit(16942501888ULL) % ix::MiB, 0);
        QCOMPARE(ix::calculateImageMemoryLimit(128*ix::MiB), 512*ix::MiB);
        QCOMPARE(ix::calculateImageMemoryLimit(std::numeric_limits<quint64>::max()), 8192*ix::MiB);
        QCOMPARE(ix::calculateImageMemoryLimit(16ULL*1024*ix::MiB,1024), 1024*ix::MiB);
        QCOMPARE(ix::calculateImageMemoryLimit(16ULL*1024*ix::MiB,0,32), 1024*ix::MiB);
        QCOMPARE(QImageReader::allocationLimit(), int(ix::imageMemoryLimit()/ix::MiB));
        QVERIFY(ix::physicalMemoryBytes() > 0);
        QVERIFY(ix::safeSize({100001,1},4)); QVERIFY(!ix::safeSize({1,1},0));
        QVERIFY(!ix::safeSize({ix::ImageAxisLimit+1,1},4));
    }
    void rowResampleMatchesLegacy_data() {
        QTest::addColumn<int>("format");
        for (auto format : {QImage::Format_RGB32,QImage::Format_ARGB32,QImage::Format_ARGB32_Premultiplied,
                            QImage::Format_RGBA8888,QImage::Format_RGB888,QImage::Format_Indexed8,QImage::Format_Grayscale8})
            QTest::newRow(qPrintable(QString::number(int(format)))) << int(format);
    }
    void rowResampleMatchesLegacy() {
        QFETCH(int,format);
        auto source=ix::test::filterPattern({41,29},QImage::Format(format));
        source.setColorSpace(QColorSpace::DisplayP3); source.setText(QStringLiteral("Comment"),QStringLiteral("resize rows"));
        const auto key=source.cacheKey();
        for (auto size : {QSize(73,67),QSize(17,11),QSize(1,31),QSize(53,1)})
            for (int filter=0;filter<6;++filter) for (bool linear : {false,true}) for (bool fast : {false,true}) {
                ix::ResizeOptions options; options.size=size; options.filter=ix::ResampleFilter(filter);
                options.linearLight=linear; options.fastDownscale=fast;
                const auto actual=ix::resample(source,options), expected=ix::test::referenceResample(source,options);
                QCOMPARE(actual,expected); QCOMPARE(actual.format(),expected.format());
                QCOMPARE(actual.colorSpace(),expected.colorSpace()); QCOMPARE(actual.text(),expected.text());
                QCOMPARE(actual.dotsPerMeterX(),expected.dotsPerMeterX());
            }
        QCOMPARE(source.cacheKey(),key);
    }
    void resizeEstimateDefinesTheAcceptedBoundary() {
        ix::ResizeOptions options; options.size={100000,100000};
        const QSize source(6000,4000);
        const auto maximum=ix::maximumResizeSize(source,QImage::Format_RGB32,options);
        QVERIFY(maximum.width()>0); QVERIFY(maximum.width()<100000);
        options.size=maximum;
        QVERIFY(ix::estimatedResizeBytes(source,QImage::Format_RGB32,options)<=ix::imageMemoryLimit());
        options.size+=QSize(1,1);
        QVERIFY(ix::estimatedResizeBytes(source,QImage::Format_RGB32,options)>ix::imageMemoryLimit());
        QVERIFY(ix::resizeSizeLimitError(source,QImage::Format_RGB32,options).contains(QString::number(maximum.width())));
        std::atomic_bool cancel=true; QImage small(5,5,QImage::Format_RGB32); small.fill(Qt::red);
        options.size={8,8}; QVERIFY(ix::resample(small,options,&cancel).isNull());
    }
    void grayscaleMatchesEveryRgbColor() {
        QImage source(4096,4096,QImage::Format_RGB32); QVERIFY(!source.isNull());
        for (int y = 0; y < source.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(source.scanLine(y));
            for (int x = 0; x < source.width(); ++x) row[x] = 0xff000000u | quint32(y * 4096 + x);
        }
        const auto key = source.cacheKey();
        QCOMPARE(ix::grayscale(source),ix::test::referenceGrayscale(source));
        QCOMPARE(source.cacheKey(),key);
    }
    void pdfRemainsLazyDuringImageBrowsing() {
        ix::CodecRegistry registry;
        if (!registry.extensions().contains(QStringLiteral("pdf"))) return;
#ifdef Q_OS_WIN
        QVERIFY(GetModuleHandleW(L"Qt6Pdf.dll") == nullptr);
        QVERIFY(GetModuleHandleW(L"ixpdf.dll") == nullptr);
#endif
        QVERIFY(registry.openFilter().contains(QStringLiteral("*.pdf")));
        const auto imagePath = temporary_.filePath(QStringLiteral("before-pdf.png"));
        QImage image(12,8,QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(imagePath));
        QVERIFY(registry.read(imagePath).ok());
#ifdef Q_OS_WIN
        QVERIFY(GetModuleHandleW(L"Qt6Pdf.dll") == nullptr);
#endif
        const auto pdfPath = temporary_.filePath(QStringLiteral("preview.pdf"));
        QVERIFY(ix::test::writePdfFixture(pdfPath));
        const auto preview = ix::loadPreview(pdfPath, {32,24});
        QCOMPARE(preview.size(),QSize(32,24)); QCOMPARE(preview.pixelColor(16,12),QColor(Qt::red));
#ifdef Q_OS_WIN
        QVERIFY(GetModuleHandleW(L"Qt6Pdf.dll") != nullptr);
#endif
    }
    void pdfRendersPagesAndPreservesSource() {
        ix::CodecRegistry registry;
        if (!registry.extensions().contains(QStringLiteral("pdf"))) return; // Deliberately reduced builds.
        const auto path=temporary_.filePath(QStringLiteral("pages.pdf"));
        QVERIFY(ix::test::writePdfFixture(path)); const auto digest=ix::sha256File(path);
        const auto first=registry.read(path,0), second=registry.read(path,1);
        QVERIFY2(first.ok(),qPrintable(first.error)); QVERIFY2(second.ok(),qPrintable(second.error));
        QCOMPARE(first.document.format,QByteArray("pdf")); QCOMPARE(first.document.pageCount,2);
        QCOMPARE(first.document.pixels.size(),QSize(160,120)); QCOMPARE(second.document.pixels.size(),QSize(120,200));
        QCOMPARE(first.document.pixels.pixelColor(80,60),QColor(Qt::red));
        QCOMPARE(second.document.pixels.pixelColor(60,100),QColor(Qt::blue));
        QCOMPARE(second.document.pixels.pixelColor(1,1),QColor(Qt::white));
        QVERIFY(std::abs(first.document.dpiX()-144)<.1); QVERIFY(!first.document.animated);
        QCOMPARE(first.document.text.value(QStringLiteral("Title")),QStringLiteral("uxzView PDF fixture"));
        QVERIFY(first.document.metadataWarning.isEmpty()); QVERIFY(!ix::CodecRegistry::canWrite("pdf"));
        QVERIFY(registry.openFilter().contains(QStringLiteral("*.pdf")));
        QVERIFY(!registry.read(path,-1).ok()); QVERIFY(!registry.read(path,2).ok());
        QVERIFY(!registry.save(first.document,path,"pdf",95,true).isEmpty());
        const auto exported=temporary_.filePath(QStringLiteral("pdf-page.png"));
        QVERIFY(registry.save(second.document,exported,"png",95,true).isEmpty());
        const auto restored=registry.read(exported); QVERIFY(restored.ok());
        QCOMPARE(restored.document.pixels.size(),second.document.pixels.size());
        QCOMPARE(restored.document.pixels.pixelColor(60,100),QColor(Qt::blue));
        QCOMPARE(ix::sha256File(path),digest);
    }
    void pdfRejectsMalformedAndOversizedPages() {
        ix::CodecRegistry registry; if (!registry.extensions().contains(QStringLiteral("pdf"))) return;
        const auto huge=temporary_.filePath(QStringLiteral("huge.pdf"));
        QVERIFY(ix::test::writePdfFixture(huge,100000,100000));
        const auto result=registry.read(huge);
        QVERIFY(!result.ok()); QVERIFY(result.error.contains(QStringLiteral("Speicherlimit")));
        const auto malformed=temporary_.filePath(QStringLiteral("broken.pdf"));
        QFile file(malformed); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("%PDF-1.7\ninvalid\n"); file.close();
        QVERIFY(!registry.read(malformed).ok());
    }
    void pdfReadersOwnIndependentWorkerDocuments() {
        ix::CodecRegistry registry; if (!registry.extensions().contains(QStringLiteral("pdf"))) return;
        const auto path=temporary_.filePath(QStringLiteral("parallel.pdf")); QVERIFY(ix::test::writePdfFixture(path));
        auto first=QtConcurrent::run([registry,path] { return registry.read(path,0); });
        auto second=QtConcurrent::run([registry,path] { return registry.read(path,1); });
        QVERIFY(first.result().ok()); QVERIFY(second.result().ok());
        QCOMPARE(first.result().document.pixels.size(),QSize(160,120));
        QCOMPARE(second.result().document.pixels.size(),QSize(120,200));
    }
    void init() {
        ix::configureImageStorage({false});
        QSettings().setValue(QStringLiteral("performance/swapEnabled"), false);
    }
    void initTestCase() {
        ix::initializeImageMemoryLimit(512);
        ix::configureImageStorage({false});
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewQualityTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
        QSettings().setValue(QStringLiteral("performance/swapEnabled"), false);
        ix::ImageLoadCache::configure(0, 16, true);
    }
    void swapChannelsRejectsInvalidIndices() {
        QImage source(1, 1, QImage::Format_RGBA64);
        reinterpret_cast<QRgba64 *>(source.scanLine(0))[0] = QRgba64::fromRgba64(0x1234, 0x5678, 0x9abc, 0xdef0);
        const auto key = source.cacheKey();
        for (int invalid : {std::numeric_limits<int>::min(), -1, 3, std::numeric_limits<int>::max()}) {
            QVERIFY_EXCEPTION_THROWN(ix::swapChannels(source, invalid, 0), std::out_of_range);
            QVERIFY_EXCEPTION_THROWN(ix::swapChannels(source, 0, invalid), std::out_of_range);
        }
        const auto result = ix::swapChannels(source, 0, 2).pixelColor(0, 0).rgba64();
        QCOMPARE(result.red(), quint16(0x9abc)); QCOMPARE(result.green(), quint16(0x5678));
        QCOMPARE(result.blue(), quint16(0x1234)); QCOMPARE(result.alpha(), quint16(0xdef0));
        QCOMPARE(ix::swapChannels(source, 1, 1), source);
        QCOMPARE(source.cacheKey(), key);
    }
    void nativePixelEditsMatchLegacy_data() {
        QTest::addColumn<int>("format");
        for (auto format : {QImage::Format_RGB32,QImage::Format_ARGB32,QImage::Format_ARGB32_Premultiplied,
             QImage::Format_RGBA8888,QImage::Format_RGB888,QImage::Format_Indexed8,QImage::Format_Grayscale8,
             QImage::Format_Grayscale16,QImage::Format_RGBA64,QImage::Format_RGBA32FPx4})
            QTest::newRow(QByteArray::number(int(format)).constData()) << int(format);
    }
    void nativePixelEditsMatchLegacy() {
        QFETCH(int,format);
        auto source=ix::test::filterPattern({257,113},QImage::Format(format));
        QVERIFY(!source.isNull());
        source.setColorSpace(QColorSpace::DisplayP3);
        source.setDevicePixelRatio(1.75); source.setOffset({-7,13});
        source.setDotsPerMeterX(4500); source.setDotsPerMeterY(7300);
        source.setText(QStringLiteral("Comment"),QStringLiteral("pixel edit preservation"));
        const auto before=source.copy(); const auto key=source.cacheKey();
        const auto verify=[&](const QImage &actual,const QImage &expected) {
            QCOMPARE(actual,expected); QCOMPARE(actual.format(),expected.format());
            QCOMPARE(actual.colorSpace(),source.colorSpace());
            QCOMPARE(actual.devicePixelRatio(),source.devicePixelRatio()); QCOMPARE(actual.offset(),source.offset());
            QCOMPARE(actual.dotsPerMeterX(),source.dotsPerMeterX()); QCOMPARE(actual.dotsPerMeterY(),source.dotsPerMeterY());
            QCOMPARE(actual.text(QStringLiteral("Comment")),source.text(QStringLiteral("Comment")));
        };
        for (int channel : {std::numeric_limits<int>::min(),-1,0,1,2,3,std::numeric_limits<int>::max()}) {
            verify(ix::negative(source,channel),ix::test::referenceNegative(source,channel));
            verify(ix::channel(source,channel),ix::test::referenceChannel(source,channel));
        }
        verify(ix::grayscale(source),ix::test::referenceGrayscale(source));
        for (int first=0;first<3;++first) for (int second=0;second<3;++second)
            verify(ix::swapChannels(source,first,second),ix::test::referenceSwap(source,first,second));
        QCOMPARE(source,before); QCOMPARE(source.cacheKey(),key);
    }
    void nativePixelEditsRespectPaddingAndOpaqueAlpha() {
        constexpr int width=17,height=11,stride=80;
        QByteArray buffer(height*stride,char(0x5a));
        QImage source(reinterpret_cast<uchar *>(buffer.data()),width,height,stride,QImage::Format_RGB32);
        // RGB32 ignores the stored alpha byte, including malformed external buffers.
        for (int y=0;y<height;++y) {
            auto *row=reinterpret_cast<QRgb *>(source.scanLine(y));
            for (int x=0;x<width;++x) row[x]=qRgba(x*13,y*19,(x+y)*7,(x+y)%3 ? 0 : 127);
        }
        const auto before=QByteArray(buffer.constData(),buffer.size());
        QCOMPARE(ix::negative(source),ix::test::referenceNegative(source));
        QCOMPARE(ix::channel(source,3),ix::test::referenceChannel(source,3));
        QCOMPARE(ix::swapChannels(source,0,2),ix::test::referenceSwap(source,0,2));
        QCOMPARE(ix::grayscale(source),ix::test::referenceGrayscale(source));
        QCOMPARE(buffer,before);
    }
    void nativePixelEditsFitWithinBudget_data() {
        QTest::addColumn<int>("operation");
        QTest::newRow("negative") << 0; QTest::newRow("channel") << 1; QTest::newRow("swap") << 2;
        QTest::newRow("grayscale") << 3;
    }
    void nativePixelEditsFitWithinBudget() {
        QFETCH(int,operation);
        QImage source(8193,4096,QImage::Format_RGB32); QVERIFY(!source.isNull()); source.fill(Qt::blue);
        const auto key=source.cacheKey(); QImage result; QString error;
        try {
            result=operation==0 ? ix::negative(source) : operation==1 ? ix::channel(source,0)
                : operation==2 ? ix::swapChannels(source,0,2) : ix::grayscale(source);
        } catch (const std::exception &failure) { error=QString::fromUtf8(failure.what()); }
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(result.size(),source.size());
        const QColor expected=operation==0 ? Qt::yellow : operation==1 ? Qt::black
            : operation==2 ? Qt::red : QColor(18,18,18);
        QCOMPARE(result.pixelColor(0,0),expected); QCOMPARE(result.pixelColor(8192,4095),expected);
        QCOMPARE(source.pixelColor(8192,4095),QColor(Qt::blue)); QCOMPARE(source.cacheKey(),key);
    }
    void nativePixelEditsRejectExcessiveBuffers() {
        QImage source(8193,8192,QImage::Format_RGB32); QVERIFY(!source.isNull()); source.fill(Qt::blue);
        QVERIFY_EXCEPTION_THROWN(ix::negative(source),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ix::channel(source,0),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ix::swapChannels(source,0,2),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ix::grayscale(source),std::runtime_error);
        QCOMPARE(source.pixelColor(8192,8191),QColor(Qt::blue));
    }
    void autoCropMatchesOriginalPixels_data() {
        QTest::addColumn<int>("format");
        for (auto format : {QImage::Format_RGB32,QImage::Format_ARGB32,QImage::Format_RGBA64,
            QImage::Format_RGBX64,QImage::Format_ARGB32_Premultiplied,QImage::Format_RGBA64_Premultiplied,
            QImage::Format_RGBA32FPx4,QImage::Format_Grayscale16,QImage::Format_Indexed8})
            QTest::newRow(QByteArray::number(int(format)).constData()) << int(format);
    }
    void autoCropMatchesOriginalPixels() {
        QFETCH(int,format);
        QImage high(11,9,QImage::Format_RGBA64);
        high.fill(QColor::fromRgba64(0x2345,0x6789,0xabc1,0x7fff));
        for (int y=2;y<7;++y) for (int x=3;x<9;++x)
            high.setPixelColor(x,y,QColor::fromRgba64(0x3456,0x789a,0x4567,0x6789));
        high.setPixelColor(1,1,QColor::fromRgba64(0x2345+257,0x6789,0xabc1,0x7fff));
        const auto source=high.convertToFormat(QImage::Format(format));
        QVERIFY(!source.isNull()); const auto key=source.cacheKey();
        for (int tolerance : {-1,0,1,16,127,255,std::numeric_limits<int>::max()})
            QCOMPARE(ix::autoCropRect(source,tolerance),ix::test::autoCropReference(source,tolerance));
        QCOMPARE(source.cacheKey(),key);
        QCOMPARE(ix::autoCropRect(QImage()),QRect());
    }
    void pixelWorkingSetsRejectExcessiveBuffers_data() {
        QTest::addColumn<int>("format");
        QTest::newRow("8-bit") << int(QImage::Format_RGB32);
        QTest::newRow("16-bit") << int(QImage::Format_RGBA64);
    }
    void pixelWorkingSetsRejectExcessiveBuffers() {
        QFETCH(int, format);
        QImage source(8193, 4096, QImage::Format(format));
        QVERIFY(!source.isNull()); source.fill(Qt::blue);
        const auto key = source.cacheKey();
        if (format == QImage::Format_RGB32)
            QCOMPARE(ix::grayscale(source).pixelColor(0,0), QColor(18,18,18));
        else
            QVERIFY_EXCEPTION_THROWN(ix::grayscale(source), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ix::convolve(source, {1}, 1), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ix::autoCorrect(source), std::runtime_error);
        QCOMPARE(source.cacheKey(), key);
    }
    void convolutionRejectsInvalidGeometryBeforeAllocation() {
        QImage source(1, 1, QImage::Format_RGB32); source.fill(Qt::red);
        for (int side : {std::numeric_limits<int>::min(), -1, 0, std::numeric_limits<int>::max()})
            QVERIFY_EXCEPTION_THROWN(ix::convolve(source, {}, side), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ix::convolve(source, {1}, 1, 0), std::runtime_error);
        QCOMPARE(source.pixelColor(0, 0), QColor(Qt::red));
    }
    void batchProgressQueuesGuiDelivery() {
        const auto root=temporary_.filePath(QStringLiteral("thread-progress"));
        QVERIFY(QDir().mkpath(root+QStringLiteral("/in")));
        QVERIFY(QDir().mkpath(root+QStringLiteral("/out")));
        ix::PipelineJob job; job.outputDirectory=root+QStringLiteral("/out");
        job.namePattern=QStringLiteral("{name}-out"); job.parallelism=4; job.allowLoss=true;
        job.profile.steps={{QStringLiteral("export"),{{QStringLiteral("format"),QStringLiteral("png")}}}};
        QImage image(4,3,QImage::Format_RGB32); image.fill(Qt::green);
        for (int i=0;i<4;++i) {
            const auto path=root+QStringLiteral("/in/%1.png").arg(i);
            QVERIFY(image.save(path)); job.inputs.append(path);
        }
        const auto plan=ix::BatchPipeline::plan(job); QCOMPARE(plan.size(),4);
        std::atomic_bool cancel=false, callbackOnGui=false, valid=true;
        std::atomic_int callbacks=0;
        QObject receiver; int delivered=0;
        const auto *guiThread=QThread::currentThread();
        const auto results=ix::BatchPipeline::run(job,plan,cancel,[&](int done,int total) {
            ++callbacks;
            if (QThread::currentThread()==guiThread) callbackOnGui=true;
            if (done<1 || done>4 || total!=4) valid=false;
            QMetaObject::invokeMethod(&receiver,[&] { ++delivered; },Qt::QueuedConnection);
        });
        QCOMPARE(callbacks.load(),4); QVERIFY(!callbackOnGui.load()); QVERIFY(valid.load());
        QCOMPARE(delivered,0);
        for (const auto &result:results) QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QTRY_COMPARE(delivered,4);
    }
    void convolutionRetainsReferencePixels_data() {
        QTest::addColumn<int>("format");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<int>("variant");
        for (const auto format : {QImage::Format_RGB32, QImage::Format_ARGB32, QImage::Format_RGBA64,
                                 QImage::Format_RGBA64_Premultiplied, QImage::Format_Grayscale16,
                                 QImage::Format_RGBA32FPx4})
            for (const auto size : {QSize(1, 1), QSize(1, 7), QSize(9, 1), QSize(13, 11)}) {
                const auto name = QByteArray::number(int(format)) + '-' + QByteArray::number(size.width()) +
                                  'x' + QByteArray::number(size.height());
                QTest::newRow(name.constData()) << int(format) << size << 0;
            }
        QTest::newRow("full-16-bit-range") << int(QImage::Format_RGBA64) << QSize(65536, 1) << 0;
        for (int variant = 1; variant <= 5; ++variant) {
            const auto name = QByteArray("general-filter-") + QByteArray::number(variant);
            QTest::newRow(name.constData()) << int(QImage::Format_RGBA64) << QSize(13, 11) << variant;
        }
    }
    void convolutionRetainsReferencePixels() {
        QFETCH(int, format); QFETCH(QSize, size); QFETCH(int, variant);
        auto source = ix::test::filterPattern(size, QImage::Format(format));
        if (size.width() == 65536) {
            auto *row = reinterpret_cast<QRgba64 *>(source.scanLine(0));
            for (int x = 0; x < size.width(); ++x)
                row[x] = QRgba64::fromRgba64(quint16(x), quint16(x * 37), quint16(65535 - x), quint16(x * 127));
        }
        source.setColorSpace(QColorSpace::DisplayP3);
        source.setDotsPerMeterX(4500); source.setDotsPerMeterY(8300);
        source.setText(QStringLiteral("Comment"), QStringLiteral("Precision and alpha"));
        const auto key = source.cacheKey();
        QVector<double> kernel{0, -1, 0, -1, 5, -1, 0, -1, 0};
        int side = 3;
        double divisor = 1, offset = 0;
        if (variant == 1) kernel[4] = 4.75;
        if (variant == 2) divisor = 2;
        if (variant == 3) offset = .125;
        if (variant == 4) kernel.fill(0);
        if (variant == 5) { side = 2; kernel = {.5, -1.5, 2, 1}; }
        const auto expected = ix::test::referenceConvolve(source, kernel, side, divisor, offset);
        const auto actual = ix::convolve(source, kernel, side, divisor, offset);
        QCOMPARE(actual, expected);
        QCOMPARE(actual.format(), expected.format());
        QCOMPARE(actual.colorSpace(), source.colorSpace());
        QCOMPARE(actual.dotsPerMeterX(), source.dotsPerMeterX());
        QCOMPARE(actual.dotsPerMeterY(), source.dotsPerMeterY());
        QCOMPARE(actual.text(QStringLiteral("Comment")), source.text(QStringLiteral("Comment")));
        QCOMPARE(source.cacheKey(), key);
    }
    void colorConversionAgainstLittleCms_data() {
        QTest::addColumn<int>("space");
        QTest::newRow("linear-sRGB") << int(QColorSpace::SRgbLinear);
        QTest::newRow("Adobe-RGB") << int(QColorSpace::AdobeRgb);
        QTest::newRow("Display-P3") << int(QColorSpace::DisplayP3);
        QTest::newRow("ProPhoto-RGB") << int(QColorSpace::ProPhotoRgb);
    }
    void colorConversionAgainstLittleCms() {
#ifndef IX_QUALITY_LCMS
        QSKIP("LittleCMS development library is not installed; independent color reference unavailable.");
#else
        QFETCH(int, space);
        QImage image(8, 1, QImage::Format_RGBA64);
        image.setColorSpace(QColorSpace(QColorSpace::NamedColorSpace(space)));
        auto *source = reinterpret_cast<QRgba64 *>(image.bits());
        for (int i = 0; i < 8; ++i)
            source[i] =
                QRgba64::fromRgba64(3000 + i * 6500, 12000 + i * 3200, 38000 - i * 3700, 5000 + i * 8000);
        const auto profileBytes = image.colorSpace().iccProfile();
        auto input = cmsOpenProfileFromMem(profileBytes.constData(), profileBytes.size());
        auto output = cmsCreate_sRGBProfile();
        QVERIFY(input);
        QVERIFY(output);
        auto transform =
            cmsCreateTransform(input, TYPE_RGBA_16, output, TYPE_RGBA_16, INTENT_RELATIVE_COLORIMETRIC,
                               cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOOPTIMIZE);
        QVERIFY(transform);
        QImage reference(8, 1, QImage::Format_RGBA64);
        cmsDoTransform(transform, image.constBits(), reference.bits(), 8);
        cmsDeleteTransform(transform);
        cmsCloseProfile(input);
        cmsCloseProfile(output);
        auto converted = image.convertedToColorSpace(QColorSpace::SRgb);
        QVERIFY(!converted.isNull());
        const auto *expected = reinterpret_cast<const QRgba64 *>(reference.constBits());
        auto compare = [&](const QImage &actual) {
            const auto pixels = actual.convertToFormat(QImage::Format_RGBA64);
            const auto *got = reinterpret_cast<const QRgba64 *>(pixels.constBits());
            int error = 0;
            for (int i = 0; i < 8; ++i) {
                error = std::max({error, std::abs(int(got[i].red()) - expected[i].red()),
                                  std::abs(int(got[i].green()) - expected[i].green()),
                                  std::abs(int(got[i].blue()) - expected[i].blue())});
                if (got[i].alpha() != source[i].alpha())
                    return false;
            }
            qInfo() << "maximum RGB error (16-bit code values):" << error;
            return error <= 96;
        };
        QVERIFY(compare(converted));
        const auto path = temporary_.filePath(QStringLiteral("profile-%1.png").arg(space));
        ix::ImageDocument document;
        document.pixels = image;
        QVERIFY(ix::CodecRegistry().save(document, path, QByteArray("png")).isEmpty());
        QSettings().setValue(QStringLiteral("color/manage"), true);
        ix::MainWindow window;
        window.openPath(path);
        QTRY_COMPARE(window.document().path, path);
        auto *view = window.findChild<ix::ImageView *>();
        QCOMPARE(view->image().colorSpace(), QColorSpace(QColorSpace::SRgb));
        QVERIFY(compare(view->image()));
        QCOMPARE(window.document().pixels.colorSpace(), image.colorSpace());
        QSettings().setValue(QStringLiteral("color/manage"), false);
        ix::MainWindow unmanaged;
        unmanaged.openPath(path);
        QTRY_COMPARE(unmanaged.document().path, path);
        QCOMPARE(unmanaged.findChild<ix::ImageView *>()->image(), image);
#endif
    }
    void hdrExportPreservesExtendedValues() {
        if (!ix::CodecRegistry::canWrite(QByteArray("exr")))
            QSKIP("Optional OpenEXR codec is absent.");
        QImage image(4, 2, QImage::Format_RGBA32FPx4);
        image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
        auto *values = reinterpret_cast<float *>(image.bits());
        for (int i = 0; i < 8; ++i) {
            values[i * 4] = -.125f;
            values[i * 4 + 1] = 1.5f;
            values[i * 4 + 2] = 4.f;
            values[i * 4 + 3] = .5f;
        }
        ix::ImageDocument document;
        document.pixels = image;
        QVERIFY(!ix::CodecRegistry::exportLosses(document, QByteArray("png")).isEmpty());
        const auto path = temporary_.filePath(QStringLiteral("hdr.exr"));
        const auto error = ix::CodecRegistry().save(document, path, QByteArray("exr"), 95, true);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto loaded = ix::CodecRegistry().read(path);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QVERIFY(ix::CodecRegistry::exportLosses(loaded.document, QByteArray("png"))
                    .join(QChar(10))
                    .contains(QStringLiteral("HDR")));
        auto pixels = loaded.document.pixels.convertToFormat(QImage::Format_RGBA32FPx4);
        const auto *got = reinterpret_cast<const float *>(pixels.constBits());
        for (int i = 0; i < 32; ++i)
            QVERIFY2(std::abs(got[i] - values[i]) < .005f, "OpenEXR changed an extended HDR/alpha sample");
    }
    void heicColorAndAlphaRoundTrip() {
        if (!ix::CodecRegistry::canWrite(QByteArray("heic")))
            QSKIP("Optional HEIC encoder is absent.");
        QImage image(64, 48, QImage::Format_RGBA8888);
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x)
                image.setPixelColor(x, y, QColor(x * 3, y * 4, (x + y) * 2, 80 + x * 2));
        ix::ImageDocument document;
        document.pixels = image;
        const auto path = temporary_.filePath(QStringLiteral("colors.heic"));
        const auto error = ix::CodecRegistry().save(document, path, QByteArray("heic"), 95, true);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto loaded = ix::CodecRegistry().read(path);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QCOMPARE(loaded.document.pixels.size(), image.size());
        qint64 rgbError = 0, alphaError = 0;
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x) {
                const auto before = image.pixelColor(x, y), after = loaded.document.pixels.pixelColor(x, y);
                rgbError += std::abs(before.red() - after.red()) + std::abs(before.green() - after.green()) +
                            std::abs(before.blue() - after.blue());
                alphaError += std::abs(before.alpha() - after.alpha());
            }
        qInfo() << "HEIC mean RGB/alpha error:" << double(rgbError) / (64 * 48 * 3)
                << double(alphaError) / (64 * 48);
        QVERIFY(double(rgbError) / (64 * 48 * 3) < 8);
        QVERIFY(double(alphaError) / (64 * 48) < 5);
    }
    void cameraRaw_data() {
        QTest::addColumn<QJsonObject>("fixture");
        QFile file(QStringLiteral(IX_RAW_MANIFEST));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto all =
            QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("fixtures")).toArray();
        for (auto value : all) {
            const auto object = value.toObject();
            QTest::newRow(object.value(QStringLiteral("file")).toString().toUtf8().constData()) << object;
        }
    }
    void cameraRaw() {
        QFETCH(QJsonObject, fixture);
        const auto folder = qEnvironmentVariable("IX_RAW_FIXTURES", QStringLiteral(IX_RAW_DIRECTORY));
        const auto path = QDir(folder).absoluteFilePath(fixture.value(QStringLiteral("file")).toString());
        if (!QFileInfo::exists(path)) {
            if (qEnvironmentVariableIntValue("IX_REQUIRE_RAW"))
                QFAIL("Required camera RAW fixture is missing");
            QSKIP("Run python scripts/fetch_quality_fixtures.py to enable real camera RAW checks.");
        }
        const auto hash = fixture.value(QStringLiteral("sha256")).toString();
        QCOMPARE(ix::sha256File(path), hash);
        QElapsedTimer elapsed;
        elapsed.start();
        auto loaded = ix::CodecRegistry().read(path);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        const auto &image = loaded.document.pixels;
        QVERIFY(image.depth() >= 64);
        QVERIFY(image.colorSpace().isValid());
        const auto pixels = qint64(image.width()) * image.height();
        const auto expectedPixels = fixture.value(QStringLiteral("megapixels")).toDouble() * 1e6;
        QVERIFY(pixels > expectedPixels * .85 && pixels < expectedPixels * 1.1);
        if (fixture.contains(QStringLiteral("width"))) {
            QCOMPARE(image.width(), fixture.value(QStringLiteral("width")).toInt());
            QCOMPARE(image.height(), fixture.value(QStringLiteral("height")).toInt());
        }
        auto reduced = image.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        int low = 255, high = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const auto gray = qGray(reduced.pixel(x, y));
                low = std::min(low, gray);
                high = std::max(high, gray);
            }
        QVERIFY(high - low > 25);
        const auto preview = ix::loadPreview(path, {512, 512});
        QVERIFY2(!preview.isNull(), "Embedded camera preview was not decoded");
        QVERIFY(preview.width() <= 512 && preview.height() <= 512);
        QVERIFY(std::abs(double(image.width()) / image.height() -
                         double(preview.width()) / preview.height()) < .06);
        const auto smallPreview = ix::loadPreview(path, {68, 50});
        QVERIFY(!smallPreview.isNull());
        QVERIFY(smallPreview.width() <= 68 && smallPreview.height() <= 50);
        const auto largePreview = preview.scaled(smallPreview.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        double difference = 0;
        for (int y = 0; y < smallPreview.height(); ++y)
            for (int x = 0; x < smallPreview.width(); ++x) {
                const auto a = smallPreview.pixelColor(x, y), b = largePreview.pixelColor(x, y);
                difference += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
            }
        // JPEG scaled-IDCT may differ slightly, but must retain orientation and content.
        QVERIFY(difference / (smallPreview.width() * smallPreview.height() * 3) < 12);
        const auto exportPath = temporary_.filePath(
            QStringLiteral("raw-%1.png").arg(fixture.value(QStringLiteral("file")).toString()));
        ix::ImageDocument small = loaded.document;
        small.pixels = image.scaled(256, 256, Qt::KeepAspectRatio);
        small.modified = true;
        const auto error = ix::CodecRegistry().save(small, exportPath, QByteArray("png"), 95, true);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto roundTrip = ix::CodecRegistry().read(exportPath);
        QVERIFY(roundTrip.ok());
        QCOMPARE(roundTrip.document.pixels.size(), small.pixels.size());
        QCOMPARE(roundTrip.document.pixels.convertToFormat(QImage::Format_RGBA64),
                 small.pixels.convertToFormat(QImage::Format_RGBA64));
        QCOMPARE(roundTrip.document.pixels.colorSpace(), small.pixels.colorSpace());
        QCOMPARE(ix::sha256File(path), hash);
        qInfo() << fixture.value(QStringLiteral("model")).toString() << image.size() << image.format()
                << "ICC:" << image.colorSpace().isValid() << "elapsed ms:" << elapsed.elapsed();
    }
};
QTEST_MAIN(QualityTests)
#include "quality_ixview.moc"
