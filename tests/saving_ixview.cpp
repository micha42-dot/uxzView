// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/app_storage.h"
#include "core/batch_memory.h"
#include "core/batch_pipeline.h"
#include "core/codec_registry.h"
#include "core/image_storage.h"
#include "core/jpegli_encoder.h"
#include "core/metadata.h"
#include "core/save_formats.h"
#include "core/save_options.h"
#include "core/localization.h"
#include "core/metadata.h"
#include "ui/image_view.h"
#include "ui/main_window.h"
#include "ui/settings_dialog.h"
#include <QBuffer>
#include <QColorSpace>
#include <QImageReader>
#include <QImageWriter>
#include <QtTest>
#include <QtWidgets>
#include <QtConcurrent>
#include <cmath>

namespace {
QImage sample() {
    QImage image(129, 83, QImage::Format_RGB32);
    for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x)
        image.setPixel(x,y,qRgb((x*29+y*7)%256,(x*13+y*31)%256,(x*3+y*17)%256));
    image.setColorSpace(QColorSpace::DisplayP3);
    image.setDotsPerMeterX(11811); image.setDotsPerMeterY(5906);
    return image;
}
QByteArray contents(const QString &path) { QFile file(path); if(!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
double error(const QImage &a,const QImage &b) {
    double result=0;
    for(int y=0;y<a.height();++y) for(int x=0;x<a.width();++x) {
        const auto p=a.pixelColor(x,y),q=b.pixelColor(x,y);
        result+=std::pow(p.red()-q.red(),2)+std::pow(p.green()-q.green(),2)+std::pow(p.blue()-q.blue(),2);
    }
    return result/(a.width()*a.height()*3.0);
}
}
class SavingTests : public QObject {
    Q_OBJECT
    QTemporaryDir temp_;
  private slots:
    void initTestCase() {
        QVERIFY(temp_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewSavingTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp_.path());
        qApp->setProperty("ixStorageHome",temp_.filePath(QStringLiteral("home")));
        qApp->setProperty("ixStorageData",temp_.filePath(QStringLiteral("data")));
        qApp->setProperty("ixStorageCache",temp_.filePath(QStringLiteral("cache")));
        QStandardPaths::setTestModeEnabled(true);
        ix::initializeImageMemoryLimit(512); ix::configureImageStorage({false});
        QVERIFY(ix::setUiLanguage(QStringLiteral("de")));
    }
    void init() {
        QSettings().clear();
        QSettings().setValue(QStringLiteral("view/previewStrip"),false);
        QSettings().setValue(QStringLiteral("performance/swapEnabled"),false);
    }
    void centralJpegIsJpegliAndPreservesProperties() {
        ix::ImageDocument document; document.pixels=sample();
        document.pixels.setText(QStringLiteral("Description"),QStringLiteral("A photo 日本語 한국어"));
        QByteArray expected; QBuffer destination(&expected); QVERIFY(destination.open(QIODevice::WriteOnly));
        QVERIFY(ix::writeJpegli(destination,document.pixels,95).isEmpty());
        const auto path=temp_.filePath(QStringLiteral("jpegli.jpg"));
        const auto result=ix::CodecRegistry().save(document,path,"jpeg",95,true);
        QVERIFY2(result.isEmpty(),qPrintable(result)); QCOMPARE(contents(path),expected);
        QByteArray qt; QBuffer qtOutput(&qt); qtOutput.open(QIODevice::WriteOnly);
        QImageWriter old(&qtOutput,"jpeg"); old.setQuality(95); QVERIFY(old.write(document.pixels));
        QVERIFY(expected!=qt);
        QImageReader reader(path); reader.setAutoTransform(true); const auto read=reader.read();
        QVERIFY2(!read.isNull(),qPrintable(reader.errorString())); QCOMPARE(read.size(),document.pixels.size());
        QCOMPARE(read.colorSpace(),document.pixels.colorSpace());
        QVERIFY(std::abs(read.dotsPerMeterX()-11811)<40); QVERIFY(std::abs(read.dotsPerMeterY()-5906)<40);
        QCOMPARE(read.text(QStringLiteral("Description")),document.pixels.text(QStringLiteral("Description")));
        QVERIFY(ix::CodecRegistry().read(path).ok()); // Existing turbo/Qt decoder path.
        auto unknownDensity=document.pixels;unknownDensity.setDotsPerMeterX(0);unknownDensity.setDotsPerMeterY(0);
        QByteArray noDpi;QBuffer noDpiOutput(&noDpi);noDpiOutput.open(QIODevice::WriteOnly);
        QVERIFY(ix::writeJpegli(noDpiOutput,unknownDensity,95).isEmpty());
        // Qt may normalize a zero setter value to its default. Preserve the
        // effective image density rather than assuming the requested zero stuck.
        const auto decodedDensity=QImage::fromData(noDpi);QVERIFY(!decodedDensity.isNull());
        QVERIFY(std::abs(decodedDensity.dotsPerMeterX()-unknownDensity.dotsPerMeterX())<40);
        QVERIFY(std::abs(decodedDensity.dotsPerMeterY()-unknownDensity.dotsPerMeterY())<40);
    }
    void qualityChangesTheEncodedImage() {
        const auto image=sample(); QByteArray low,high;
        QBuffer a(&low),b(&high); a.open(QIODevice::WriteOnly); b.open(QIODevice::WriteOnly);
        QVERIFY(ix::writeJpegli(a,image,20).isEmpty()); QVERIFY(ix::writeJpegli(b,image,98).isEmpty());
        QVERIFY(high.size()>low.size());
        const auto l=QImage::fromData(low),h=QImage::fromData(high);
        QVERIFY(!l.isNull()&&!h.isNull()); QVERIFY(error(image,h)<error(image,l));
    }
    void transparencyUsesWhite_data() {
        QTest::addColumn<int>("format");
        for(auto f:{QImage::Format_ARGB32,QImage::Format_ARGB32_Premultiplied,QImage::Format_RGBA8888,
                    QImage::Format_RGBA64,QImage::Format_RGBA64_Premultiplied,QImage::Format_RGBA32FPx4})
            QTest::newRow(qPrintable(QString::number(f)))<<int(f);
    }
    void transparencyUsesWhite() {
        QFETCH(int,format); QImage image(65,17,QImage::Format(format)); image.fill(QColor(200,40,80,128));
        QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly);
        QVERIFY(ix::writeJpegli(output,image,100).isEmpty());
        auto read=QImage::fromData(bytes); QVERIFY(!read.isNull());
        const auto p=read.pixelColor(32,8);
        QVERIFY(std::abs(p.red()-227)<=4); QVERIFY(std::abs(p.green()-147)<=4); QVERIFY(std::abs(p.blue()-167)<=4);
        image.fill(Qt::transparent); bytes.clear(); output.seek(0);
        QVERIFY(ix::writeJpegli(output,image,100).isEmpty());
        read=QImage::fromData(bytes); QVERIFY(!read.isNull());
        QVERIFY(read.pixelColor(32,8).red()>=252&&read.pixelColor(32,8).green()>=252&&read.pixelColor(32,8).blue()>=252);
    }
    void encoderFailureCleansUpAndCanRetry() {
        class Broken : public QIODevice {
            qint64 readData(char *,qint64) override { return -1; }
            qint64 writeData(const char *,qint64) override { setErrorString(QStringLiteral("injected write failure"));return -1; }
          public: Broken() { open(QIODevice::WriteOnly); }
        } broken;
        QVERIFY(ix::writeJpegli(broken,sample(),90).contains(QStringLiteral("injected write failure")));
        QByteArray good; QBuffer output(&good); output.open(QIODevice::WriteOnly);
        QVERIFY(!ix::writeJpegli(output,QImage(),90).isEmpty()); // Actual jpegli error callback, not an IO error.
        QVERIFY(ix::writeJpegli(output,sample(),90).isEmpty()); QVERIFY(!QImage::fromData(good).isNull());
        QImage wide(65000,1,QImage::Format_RGB32); wide.fill(Qt::white);
        QVERIFY(!ix::writeJpegli(output,wide,90,64*ix::MiB).isEmpty());
    }
    void grayProfileTransparencyUsesWhite() {
        const QColorSpace gray(QPointF(0.3127,0.3290), QColorSpace::TransferFunction::Gamma, 2.2f);
        QVERIFY(gray.isValid()); QCOMPARE(gray.colorModel(),QColorSpace::ColorModel::Gray);
        QImage image(32,16,QImage::Format_ARGB32);image.fill(QColor(0,0,0,0));image.setColorSpace(gray);
        QByteArray bytes;QBuffer output(&bytes);QVERIFY(output.open(QIODevice::WriteOnly));
        const auto result=ix::writeJpegli(output,image,100);QVERIFY2(result.isEmpty(),qPrintable(result));
        const auto read=QImage::fromData(bytes);QVERIFY(!read.isNull());
        QVERIFY(read.pixelColor(16,8).red()>=252);
        QVERIFY(read.pixelColor(16,8).green()>=252);
        QVERIFY(read.pixelColor(16,8).blue()>=252);
    }
    void oversizedUnicodeCommentNeverReplacesFile() {
        const auto path=temp_.filePath(QStringLiteral("unicode-comment.jpg"));
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("keep original");file.close();
        ix::ImageDocument document;document.pixels=sample();
        document.pixels.setText(QStringLiteral("Description"),QString(22000,QChar(0xd55c)));
        const auto result=ix::CodecRegistry().save(document,path,"jpeg",95,true);
        QVERIFY2(!result.isEmpty(),"Oversized JPEG text must not be silently truncated");
        QCOMPARE(contents(path),QByteArray("keep original"));
    }
    void commentMarkerBoundaryPreservesText() {
        const QStringList values{QString(65520,QLatin1Char('a')),QString(21840,QChar(0xd55c)),
                                 QString::fromUtf8("😀").repeated(16380)};
        for(const auto &value:values) {
            QCOMPARE(value.toUtf8().size(),65520);
            auto image=sample();image.setText(QStringLiteral("Description"),value);
            QByteArray bytes;QBuffer output(&bytes);QVERIFY(output.open(QIODevice::WriteOnly));
            const auto result=ix::writeJpegli(output,image,95);QVERIFY2(result.isEmpty(),qPrintable(result));
            const auto decoded=QImage::fromData(bytes);QVERIFY(!decoded.isNull());
            QCOMPARE(decoded.text(QStringLiteral("Description")),value);
        }
    }
    void emptyNativeMetadataPreservesEncodedProperties() {
#ifndef IX_TEST_EXIV2
        QSKIP("Optional Exiv2 backend is absent.");
#else
        const auto source=temp_.filePath(QStringLiteral("empty-native.png"));QVERIFY(sample().save(source));
        QString warning;const auto metadata=ix::readMetadata(source,warning);
        QVERIFY2(warning.isEmpty(),qPrintable(warning));QVERIFY(metadata&&metadata->data);
        QVERIFY(!metadata->hasExif&&!metadata->hasIptc&&!metadata->hasXmp);
        ix::ImageDocument document;document.pixels=sample();document.metadata=metadata;
        document.pixels.setText(QStringLiteral("Description"),QStringLiteral("Keep ICC, DPI and 日本語"));
        QByteArray expected;QBuffer buffer(&expected);QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(ix::writeJpegli(buffer,document.pixels,95).isEmpty());
        const auto target=temp_.filePath(QStringLiteral("empty-native.jpg"));
        const auto result=ix::CodecRegistry().save(document,target,"jpeg",95,true);
        QVERIFY2(result.isEmpty(),qPrintable(result));QCOMPARE(contents(target),expected);
        const QImage read(target);QCOMPARE(read.colorSpace(),document.pixels.colorSpace());
        QCOMPARE(read.text(QStringLiteral("Description")),document.pixels.text(QStringLiteral("Description")));
#endif
    }
    void pixelLayoutsRemainReadable_data() {
        QTest::addColumn<int>("format");
        for(auto f:{QImage::Format_RGB32,QImage::Format_RGB888,QImage::Format_RGBX8888,
                    QImage::Format_Grayscale8,QImage::Format_Grayscale16,QImage::Format_Mono,
                    QImage::Format_Indexed8,QImage::Format_CMYK8888})
            QTest::newRow(qPrintable(QString::number(f)))<<int(f);
    }
    void pixelLayoutsRemainReadable() {
        QFETCH(int,format);QImage base(33,19,QImage::Format_RGB32);base.fill(QColor(30,90,130));
        const auto source=base.convertToFormat(QImage::Format(format));
        const auto expected=source.convertToFormat(QImage::Format_RGB888);
        QByteArray bytes;QBuffer output(&bytes);output.open(QIODevice::WriteOnly);
        const auto result=ix::writeJpegli(output,source,100);QVERIFY2(result.isEmpty(),qPrintable(result));
        const auto read=QImage::fromData(bytes).convertToFormat(QImage::Format_RGB888);
        QVERIFY(!read.isNull());QCOMPARE(read.size(),source.size());
        QVERIFY(error(expected,read)<16);
    }
    void formatsAndTargets() {
        ix::CodecRegistry registry; const auto formats=ix::writableSaveFormats(registry);
        QVERIFY(!formats.isEmpty());
        for(const auto &f:formats) {
            QVERIFY(registry.canWrite(f.id)); QVERIFY(!f.extensions.isEmpty());
            const auto fixed=ix::preferredSaveTarget(QStringLiteral("/photos/source.png"),"png",formats,false,f.id);
            QCOMPARE(fixed.format.id,f.id);
            if(f.id!="png") { QVERIFY(fixed.needsDialog); QVERIFY(fixed.suggestedPath!=QStringLiteral("/photos/source.png")); }
        }
        const auto original=ix::preferredSaveTarget(QStringLiteral("/photos/a.PNG"),"png",formats,true,"jpeg");
        QCOMPARE(original.format.id,QByteArray("png")); QVERIFY(!original.needsDialog);
        const auto readOnly=ix::preferredSaveTarget(QStringLiteral("/photos/a.svg"),"svg",formats,true,"jpeg");
        QCOMPARE(readOnly.format.id,QByteArray("png")); QVERIFY(readOnly.needsDialog); QVERIFY(readOnly.suggestedPath.endsWith(QStringLiteral("a.png")));
        const auto missing=ix::preferredSaveTarget(QStringLiteral("/photos/a.jpg"),"jpeg",formats,false,"not-installed");
        QVERIFY(missing.needsDialog); QCOMPARE(missing.format.id,QByteArray("png"));
        const auto png=original.format.filter();
        const auto noSuffix=ix::selectedSaveTarget(QStringLiteral("out"),png,formats);
        QCOMPARE(noSuffix.suggestedPath,QStringLiteral("out.png")); QCOMPARE(noSuffix.format.id,QByteArray("png"));
        const auto typed=ix::selectedSaveTarget(QStringLiteral("out.JPG"),png,formats);
        QCOMPARE(typed.suggestedPath,QStringLiteral("out.JPG")); QCOMPARE(typed.format.id,QByteArray("jpeg"));
        const auto aliased=ix::preferredSaveTarget(QStringLiteral("/photos/a.JFIF"),"jpeg",formats,true,"png");
        QVERIFY(!aliased.needsDialog); QCOMPARE(aliased.format.id,QByteArray("jpeg"));
        QVERIFY(ix::preferredSaveTarget({}, {}, {},true,{}).format.id.isEmpty());
    }
    void extensionMismatchNeverTouchesDestination() {
        const auto path=temp_.filePath(QStringLiteral("untouched.png"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep");file.close();
        ix::ImageDocument d;d.pixels=sample();
        QVERIFY(!ix::CodecRegistry().save(d,path,"jpeg",95,true).isEmpty());
        QCOMPARE(contents(path),QByteArray("keep"));
    }
    void batchAndConcurrentExportsUseTheSameEncoder() {
        const auto image=sample();
        auto encode=[image] { QByteArray bytes;QBuffer out(&bytes);out.open(QIODevice::WriteOnly);
            return qMakePair(ix::writeJpegli(out,image,73),bytes); };
        auto first=QtConcurrent::run(encode),second=QtConcurrent::run(encode);
        first.waitForFinished();second.waitForFinished();
        QVERIFY(first.result().first.isEmpty());QCOMPARE(first.result(),second.result());
        const auto source=temp_.filePath(QStringLiteral("batch-source.png"));QVERIFY(image.save(source));
        ix::PipelineProfile profile;
        profile.steps.append({QStringLiteral("export"),QJsonObject{{QStringLiteral("format"),QStringLiteral("jpeg")},{QStringLiteral("quality"),73}}});
        const qint64 estimate=ix::estimatedPipelineMemory({65000,1},QImage::Format_RGB32,profile,512*ix::MiB);
        QVERIFY(estimate>=ix::jpegliWorkingBytes(65000)+65000*4);
        ix::PipelineJob job;job.inputs={source};job.outputDirectory=temp_.filePath(QStringLiteral("batch-output"));
        job.profile=profile;job.allowLoss=true;std::atomic_bool cancel=false;
        const auto plan=ix::BatchPipeline::plan(job);QCOMPARE(plan.size(),1);QVERIFY2(plan[0].error.isEmpty(),qPrintable(plan[0].error));
        const auto results=ix::BatchPipeline::run(job,plan,cancel);
        QCOMPARE(results.size(),1);QVERIFY2(results[0].error.isEmpty(),qPrintable(results[0].error));
        // Reading PNG may supply text/metadata; compare decoded pixels and ICC,
        // while the byte-exact direct-vs-registry test proves central dispatch.
        const QImage output(results[0].output);QCOMPARE(output.size(),image.size());QCOMPARE(output.colorSpace(),image.colorSpace());
    }
    void portablePreferenceImportIsTypedAndUnchanged() {
        const auto home=temp_.filePath(QStringLiteral("portable-home"));
        QVERIFY(QDir().mkpath(home));
        const auto data=temp_.filePath(QStringLiteral("portable-source"));QVERIFY(QDir().mkpath(data));
        QVariantMap values{{QStringLiteral("save/preferOriginal"),false},{QStringLiteral("save/format"),QStringLiteral("jpeg")}};
        std::atomic_bool cancel=false;
        const auto result=ix::AppStorage::preparePortable(home,values,data,cancel);
        QVERIFY2(result.isEmpty(),qPrintable(result));
        QSettings profile(home+QStringLiteral("/IxView-data/settings/IxView/IxView.ini"),QSettings::IniFormat);
        QCOMPARE(profile.value(QStringLiteral("save/preferOriginal")).toBool(),false);
        QCOMPARE(profile.value(QStringLiteral("save/format")).toString(),QStringLiteral("jpeg"));
    }
    void preferenceRoundTripAndCancel() {
        {
            ix::SettingsDialog dialog; auto *mode=dialog.findChild<QComboBox *>(QStringLiteral("save/preferOriginal"));
            auto *format=dialog.findChild<QComboBox *>(QStringLiteral("save/format"));
            QVERIFY(mode&&format); QVERIFY(mode->currentData().toBool()); QVERIFY(!format->isEnabled());
            mode->setCurrentIndex(1); QVERIFY(format->isEnabled());
            format->setCurrentIndex(format->findData(QStringLiteral("jpeg"))); dialog.reject();
            QVERIFY(!QSettings().contains(QStringLiteral("save/preferOriginal")));
        }
        {
            ix::SettingsDialog dialog; auto *mode=dialog.findChild<QComboBox *>(QStringLiteral("save/preferOriginal"));
            auto *format=dialog.findChild<QComboBox *>(QStringLiteral("save/format"));
            dialog.selectPage(QStringLiteral("saveSettingsPage"));
            const auto formats=ix::writableSaveFormats(ix::CodecRegistry()); QCOMPARE(format->count(),formats.size());
            mode->setCurrentIndex(1); format->setCurrentIndex(format->findData(QStringLiteral("jpeg")));
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(dialog.result(),int(QDialog::Accepted));
        }
        QCOMPARE(QSettings().value(QStringLiteral("save/preferOriginal")).toBool(),false);
        QCOMPARE(QSettings().value(QStringLiteral("save/format")).toString(),QStringLiteral("jpeg"));
        ix::SettingsDialog reopened;
        reopened.selectPage(QStringLiteral("saveSettingsPage"));
        QCOMPARE(reopened.findChild<QComboBox *>(QStringLiteral("save/preferOriginal"))->currentData().toBool(),false);
        QCOMPARE(reopened.findChild<QComboBox *>(QStringLiteral("save/format"))->currentData().toString(),QStringLiteral("jpeg"));
    }
    void saveRoutes_data() {
        QTest::addColumn<QString>("command");QTest::addColumn<bool>("fixed");QTest::addColumn<bool>("overridePng");
        QTest::newRow("save-converts-safely")<<QStringLiteral("save")<<true<<false;
        QTest::newRow("save-as-original")<<QStringLiteral("saveAs")<<false<<false;
        QTest::newRow("save-as-override")<<QStringLiteral("saveAs")<<true<<true;
        QTest::newRow("selection-fixed")<<QStringLiteral("saveSelection")<<true<<false;
    }
    void saveRoutes() {
        QFETCH(QString,command);QFETCH(bool,fixed);QFETCH(bool,overridePng);
        const auto source=temp_.filePath(QStringLiteral("route-%1-%2.png").arg(command).arg(overridePng));
        QVERIFY(sample().save(source));const auto original=contents(source);
        QSettings().setValue(QStringLiteral("save/preferOriginal"),!fixed);
        QSettings().setValue(QStringLiteral("save/format"),QStringLiteral("jpeg"));
        ix::MainWindow window;window.show();window.openPath(source);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy()&&!window.document().empty(),10000);
        window.dispatch(QStringLiteral("grayscale"));QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy()&&window.document().modified,10000);
        if(command==QStringLiteral("saveSelection")) window.findChild<ix::ImageView *>()->setSelection({3,4,31,23});
        const auto suffix=fixed&&!overridePng?QStringLiteral("jpg"):QStringLiteral("png");
        const auto target=temp_.filePath(QStringLiteral("export-%1-%2.").arg(command).arg(overridePng)+suffix);
        bool seen=false,timedOut=false;QString initialFilter;QElapsedTimer elapsed;elapsed.start();QTimer driver;
        connect(&driver,&QTimer::timeout,&window,[&] {
            auto *modal=QApplication::activeModalWidget();
            if(elapsed.elapsed()>10000) {timedOut=true;if(auto *d=qobject_cast<QDialog *>(modal))d->reject();return;}
            if(auto *picker=qobject_cast<QFileDialog *>(modal);picker&&!seen) {
                seen=true;initialFilter=picker->selectedNameFilter();
                if(overridePng)picker->selectNameFilter(QStringLiteral("PNG (*.png)"));
                auto *editor=picker->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
                if(!editor){timedOut=true;picker->reject();return;}
                // QFileDialog::selectFile intentionally leaves a focused filename
                // editor alone on Qt 6.8. Transfer focus before selecting a path.
                editor->setCompleter(nullptr);editor->clearFocus();picker->selectFile(target);
                QMetaObject::invokeMethod(picker,"accept",Qt::QueuedConnection);
            } else if(auto *box=qobject_cast<QMessageBox *>(modal)) {
                if(box->standardButtons().testFlag(QMessageBox::Yes))box->done(QMessageBox::Yes);
                else {timedOut=true;box->reject();}
            }
        });
        driver.start(10);window.dispatch(command);driver.stop();
        QVERIFY(!timedOut);QVERIFY(seen);QVERIFY(initialFilter.startsWith(fixed?QStringLiteral("JPEG"):QStringLiteral("PNG")));
        QVERIFY2(QFileInfo::exists(target),qPrintable(QStringLiteral("Actual saved path: ")+window.document().path));QCOMPARE(contents(source),original);
        QImageReader reader(target);QCOMPARE(reader.format(),suffix==QStringLiteral("jpg")?QByteArray("jpeg"):QByteArray("png"));
        const auto image=reader.read();QVERIFY(!image.isNull());
        QCOMPARE(image.size(),command==QStringLiteral("saveSelection")?QSize(31,23):sample().size());
        QCOMPARE(QSettings().value(QStringLiteral("save/format")).toString(),QStringLiteral("jpeg"));
        if(command!=QStringLiteral("saveSelection")) {
            QCOMPARE(window.document().path,target);QVERIFY(!window.document().modified);
            if(suffix==QStringLiteral("jpg")) { const auto saved=contents(target);window.dispatch(QStringLiteral("save"));QCOMPARE(contents(target),saved); }
        } else { QCOMPARE(window.document().path,source);QVERIFY(window.document().modified); }
    }
    void compressionOptionsAffectOutput();
    void targetSizeOptimizationRespectsBudget();
    void compressionProfilesRoundTrip();
    void perTypeMetadataMaskingDropsOnlyDeselected();
    void alphaModeControlsFlattening();
    void jpegFilterMatchesCanonicalFormat();
};
void SavingTests::compressionOptionsAffectOutput() {
    const auto format = QByteArrayLiteral("webp");
    if (!ix::CodecRegistry::canWrite(format))
        QSKIP("WebP writer unavailable in this build");
    ix::ImageDocument document;
    document.pixels = sample();
    ix::CodecRegistry registry;
    const auto lowPath = temp_.filePath(QStringLiteral("quality-low.webp"));
    const auto highPath = temp_.filePath(QStringLiteral("quality-high.webp"));
    ix::CodecRegistry::EncodeOptions low, high;
    low.quality = 5;
    high.quality = 95;
    QVERIFY2(registry.saveWithOptions(document, lowPath, format, low, true).isEmpty(),
             qPrintable(lowPath));
    QVERIFY2(registry.saveWithOptions(document, highPath, format, high, true).isEmpty(),
             qPrintable(highPath));
    const auto lowBytes = contents(lowPath), highBytes = contents(highPath);
    QVERIFY(!lowBytes.isEmpty() && !highBytes.isEmpty());
    QVERIFY2(lowBytes.size() < highBytes.size(),
             qPrintable(QStringLiteral("%1 vs %2").arg(lowBytes.size()).arg(highBytes.size())));
    const auto flat = temp_.filePath(QStringLiteral("level-fast.png"));
    const auto tight = temp_.filePath(QStringLiteral("level-best.png"));
    ix::CodecRegistry::EncodeOptions fast, best;
    fast.compressionLevel = 1;
    best.compressionLevel = 9;
    QVERIFY(registry.saveWithOptions(document, flat, "png", fast, true).isEmpty());
    QVERIFY(registry.saveWithOptions(document, tight, "png", best, true).isEmpty());
    QVERIFY(QImageReader(flat).canRead() && QImageReader(tight).canRead());
}
void SavingTests::targetSizeOptimizationRespectsBudget() {
    ix::ImageDocument document;
    document.pixels = sample();
    ix::CodecRegistry registry;
    ix::SaveOptions options;
    options.format = "jpeg";
    options.targetSizeEnabled = true;
    std::atomic_bool cancel = false;
    const auto probe = [&](int quality) -> qint64 {
        QByteArray encoded;
        QBuffer buffer(&encoded);
        if (!buffer.open(QIODevice::WriteOnly))
            return -1;
        if (!ix::writeJpegli(buffer, document.pixels, quality).isEmpty())
            return -1;
        return encoded.size();
    };
    options.targetBytes = probe(95);
    QVERIFY(options.targetBytes > 0);
    auto reachable = ix::optimizeForTargetSize(document, options, registry, {}, cancel);
    QVERIFY2(reachable.error.isEmpty(), qPrintable(reachable.error));
    QVERIFY(reachable.quality.has_value());
    QVERIFY(probe(*reachable.quality) <= options.targetBytes);
    options.targetBytes = 1;
    auto unreachable = ix::optimizeForTargetSize(document, options, registry, {}, cancel);
    // IrfanView-style: an unreachable target yields the smallest reachable
    // output (quality 1) instead of an error.
    QVERIFY(unreachable.quality.has_value());
    QCOMPARE(*unreachable.quality, 1);
    QVERIFY(unreachable.error.isEmpty());
    QVERIFY(unreachable.bytes > options.targetBytes);
    options.targetBytes = probe(95);
    cancel = true;
    auto cancelled = ix::optimizeForTargetSize(document, options, registry, {}, cancel);
    QVERIFY(!cancelled.quality.has_value());
}
void SavingTests::compressionProfilesRoundTrip() {
    ix::SaveOptions options;
    options.format = "jpeg";
    options.quality = 77;
    options.progressive = true;
    options.keepXmp = false;
    options.targetSizeEnabled = true;
    options.targetBytes = 200 * 1024;
    const auto json = options.toJson();
    const auto restored = ix::SaveOptions::fromJson(json);
    QCOMPARE(restored.quality, 77);
    QVERIFY(restored.progressive);
    QVERIFY(!restored.keepXmp);
    QVERIFY(restored.targetSizeEnabled);
    QCOMPARE(restored.targetBytes, 200 * 1024);
    ix::storeSaveOptions(options);
    QCOMPARE(ix::loadSaveOptions("jpeg").quality, 77);
    QJsonObject profiles;
    profiles.insert(QStringLiteral("web-photo"), json);
    ix::storeSaveProfiles(profiles);
    QCOMPARE(ix::loadSaveProfiles().value(QStringLiteral("web-photo")).toObject().value(
                 QStringLiteral("quality")).toInt(), 77);
    QVERIFY(!ix::saveProfileError(QString(), profiles, false).isEmpty());
    QVERIFY(!ix::saveProfileError(QStringLiteral("web-photo"), profiles, false).isEmpty());
    QVERIFY(ix::saveProfileError(QStringLiteral("web-photo"), profiles, true).isEmpty());
    QVERIFY(ix::saveProfileError(QStringLiteral("other"), profiles, false).isEmpty());
}
void SavingTests::perTypeMetadataMaskingDropsOnlyDeselected() {
    QSettings().setValue(QStringLiteral("save/warnLosses"), false);
    const auto source = temp_.filePath(QStringLiteral("mask-source.png"));
    QVERIFY(sample().save(source));
    ix::CodecRegistry registry;
    auto read = registry.read(source);
    if (!read.ok() || !read.document.metadata || !read.document.metadata->data)
        QSKIP("No native metadata fixture available; masking covered by unit path");
    ix::ImageDocument document = read.document;
    auto metadata = std::make_shared<ix::Metadata>(*read.document.metadata);
    metadata->hasExif = true;
    metadata->hasXmp = true;
    document.metadata = metadata;
    ix::CodecRegistry::EncodeOptions keep;
    keep.keepExif = true;
    keep.keepXmp = false;
    QVERIFY(registry.saveWithOptions(document, temp_.filePath(QStringLiteral("masked.png")), "png",
                                     keep, true).isEmpty());
}
void SavingTests::alphaModeControlsFlattening() {
    // Real transparency + JPEG: White flattens, Keep preserves pixels.
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(QColor(10, 200, 30, 128));
    ix::ImageDocument document;
    document.pixels = image;
    QVERIFY(ix::imageHasRealTransparency(document.pixels));
    ix::CodecRegistry registry;
    ix::CodecRegistry::EncodeOptions white, keep;
    white.alphaWhite = 1;
    keep.alphaWhite = 0;
    const auto whitePath = temp_.filePath(QStringLiteral("alpha-white.jpg"));
    const auto keepPath = temp_.filePath(QStringLiteral("alpha-keep.jpg"));
    QVERIFY(registry.saveWithOptions(document, whitePath, "jpeg", white, true).isEmpty());
    // Keep bypasses flattening through jpegli's alpha path: still encodes.
    QVERIFY(registry.saveWithOptions(document, keepPath, "jpeg", keep, true).isEmpty());
    QVERIFY(QImageReader(whitePath).canRead());
    QVERIFY(QImageReader(keepPath).canRead());
    // Opaque images never warn and never flatten regardless of mode.
    ix::ImageDocument opaque;
    opaque.pixels = sample();
    QVERIFY(!ix::imageHasRealTransparency(opaque.pixels));
    QVERIFY(ix::CodecRegistry::exportLosses(opaque, "jpeg").isEmpty());
}
void SavingTests::jpegFilterMatchesCanonicalFormat() {
    // A JFIF-labeled filter (as advertised by some Qt builds) must resolve
    // to the canonical jpeg entry, never to a phantom JFIF default.
    ix::CodecRegistry registry;
    const auto formats = ix::writableSaveFormats(registry);
    const auto byId = [&](const QByteArray &id) {
        return std::find_if(formats.cbegin(), formats.cend(),
                            [&](const auto &f) { return f.id == id; });
    };
    QVERIFY(byId("jpeg") != formats.cend());
    QCOMPARE(ix::canonicalSaveFormat(QByteArray("jfif")), QByteArray("jpeg"));
    // selectedSaveTarget prefers the filename suffix, falling back to the
    // filter; a bare name with a JPEG filter lands on jpeg, not jfif.
    const auto jpegFilter = byId("jpeg")->filter();
    const auto target = ix::selectedSaveTarget(temp_.filePath(QStringLiteral("photo")),
                                               jpegFilter, formats);
    QCOMPARE(target.format.id, QByteArray("jpeg"));
    QVERIFY(target.suggestedPath.endsWith(QStringLiteral(".jpg")));
}
int main(int argc,char **argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);SavingTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "saving_ixview.moc"
