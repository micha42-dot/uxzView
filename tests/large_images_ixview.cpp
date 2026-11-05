// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/image_storage.h"
#include "core/codec_registry.h"
#include "core/turbojpeg_codec.h"
#include "core/image_loading.h"
#include "core/document_history.h"
#include "resample_reference.h"
#include "ui/image_view.h"
#include <QApplication>
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QStorageInfo>
#include <QtConcurrent>
#include <QtTest>
#include <cstdlib>
#include <cstring>
#include <cstdio>
namespace {
QImage fixture(QImage::Format format = QImage::Format_ARGB32) {
    QImage image(31,23,format);
    for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x)
        image.setPixelColor(x,y,QColor((x*31+y*7)%256,(x*11+y*23)%256,(x+y*37)%256,format==QImage::Format_RGB32 ? 255 : (x*17+y*29)%256));
    if (format==QImage::Format_RGBA64) {
        for (int y=0;y<image.height();++y) {
            auto *row=reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for (int x=0;x<image.width();++x)
                row[x]=QRgba64::fromRgba64((x*1117+y*37)%65536,(x*493+y*2411)%65536,
                    (x*4397+y*103)%65536,(x*331+y*1907)%65536);
        }
    }
    return image;
}
// Minimal single-entry EXIF APP1 segment (Orientation tag) for fallback tests.
QByteArray jpegWithExifOrientation(const QByteArray &jpeg, quint16 orientation) {
    QByteArray tiff;
    tiff.append("II").append(char(0x2A)).append(char(0)).append(QByteArray::fromHex("08000000"));
    tiff.append(QByteArray::fromHex("0100"));                 // one IFD entry
    tiff.append(QByteArray::fromHex("1201"));                 // Orientation tag
    tiff.append(QByteArray::fromHex("0300"));                 // SHORT
    tiff.append(QByteArray::fromHex("01000000"));             // count 1
    tiff.append(char(orientation & 0xFF)).append(char((orientation >> 8) & 0xFF));
    tiff.append(QByteArray::fromHex("0000"));                 // value padding
    tiff.append(QByteArray::fromHex("00000000"));             // no next IFD
    QByteArray app1 = QByteArray::fromHex("FFE1");
    const int length = 2 + 6 + tiff.size();
    app1.append(char((length >> 8) & 0xFF)).append(char(length & 0xFF));
    app1.append("Exif\0\0", 6);
    app1.append(tiff);
    return jpeg.left(2) + app1 + jpeg.mid(2);
}
// APP2 ICC_PROFILE segment inserted right after SOI.
QByteArray jpegWithIccProfile(const QByteArray &jpeg, const QByteArray &icc) {
    QByteArray app2 = QByteArray::fromHex("FFE2");
    const int payload = 12 + 2 + icc.size();
    const int length = payload + 2;
    app2.append(char((length >> 8) & 0xFF)).append(char(length & 0xFF));
    app2.append("ICC_PROFILE\0", 12);
    app2.append(char(1)).append(char(1)); // sequence 1 of 1
    app2.append(icc);
    return jpeg.left(2) + app2 + jpeg.mid(2);
}
}
class LargeImageTests : public QObject {
    Q_OBJECT
    QTemporaryDir temp_;
private slots:
    void initTestCase() {
        ix::initializeImageMemoryLimit(512); QVERIFY(temp_.isValid());
        const QStorageInfo disk(temp_.path());
        qInfo() << "Temporary storage:" << temp_.path() << "Qt volume:" << disk.rootPath()
            << "valid:" << disk.isValid() << "ready:" << disk.isReady()
            << "read-only:" << disk.isReadOnly() << "available bytes:" << disk.bytesAvailable();
    }
    void init() { QCOMPARE(ix::mappedImageBytes(),0); ix::configureImageStorage({true,32*ix::MiB,temp_.path(),1}); }
    void cleanup() { QCOMPARE(ix::mappedImageBytes(),0); }
    void lifetimeAndHeader() {
        QString path;
        {
            auto image=ix::allocateImage({91,43},QImage::Format_ARGB32);
            QVERIFY(ix::imageIsFileBacked(image)); path=ix::imageBackingFile(image);
            QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY(file.read(32).startsWith("uxzView pixels v1")); file.close();
#ifndef Q_OS_WIN
            QVERIFY(!(QFile::permissions(path)&(QFileDevice::ReadGroup|QFileDevice::ReadOther|QFileDevice::WriteGroup|QFileDevice::WriteOther)));
#endif
            image.fill(QColor(21,42,63,84)); auto shared=image; image={};
            QVERIFY(QFileInfo::exists(path)); QCOMPARE(shared.pixelColor(90,42),QColor(21,42,63,84));
            QCOMPARE(ix::cleanupStaleImageStorage(temp_.path()),0);
            auto worker=QtConcurrent::run([pixels=std::move(shared)]() mutable { pixels={}; }); worker.waitForFinished();
        }
        QVERIFY(!QFileInfo::exists(path));
    }
    void budgetAndGeometry() {
        ix::configureImageStorage({true,70000,temp_.path(),1});
        auto first=ix::allocateImage({10,10},QImage::Format_RGB32);
        QVERIFY_EXCEPTION_THROWN(ix::allocateImage({10,10},QImage::Format_RGB32),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ix::allocateImage({ix::ImageAxisLimit+1,1},QImage::Format_RGB32),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ix::allocateImage({-1,1},QImage::Format_RGB32),std::runtime_error);
        QVERIFY(!ix::canAllocateImage({100000,100000},QImage::Format_RGB32));
        ix::configureImageStorage({false,0,temp_.path()});
        QVERIFY(ix::imageIsFileBacked(first)); // Existing mapping survives disable.
        QVERIFY_EXCEPTION_THROWN(ix::allocateImage({10,10},QImage::Format_RGB32,0,true),std::runtime_error);
    }
    void sixteenGiBExampleIsAccountedWithoutAllocatingIt() {
        if (sizeof(void *)!=8) return;
        ix::configureImageStorage({true,65536*ix::MiB,temp_.path()});
        QCOMPARE(ix::imageBufferBytes({100000,100000},QImage::Format_RGB32),40000000000LL);
        QVERIFY(ix::canAllocateImage({100000,100000},QImage::Format_RGB32,64*ix::MiB));
        QVERIFY(!ix::canAllocateImage({100000,100000},QImage::Format_RGBA64));
        QVERIFY(ix::imageBufferBytes({1,1},QImage::Format(-1))<0);
        ix::ResizeOptions options; options.size={100000,100000};
        QVERIFY(ix::resizeFitsBudget({100,100},QImage::Format_RGB32,options));
        options.sharpen=true; QVERIFY(!ix::resizeFitsBudget({100,100},QImage::Format_RGB32,options));
        QCOMPARE(ix::mappedImageBytes(),0);
    }
    void mappedMetadataLossRequiresConsent() {
        ix::ImageDocument document; document.pixels=ix::copyImageRegion(fixture(),fixture().rect());
        auto metadata=std::make_shared<ix::Metadata>(); metadata->hasExif=true;
        document.metadata=metadata; document.sourceHasExif=true;
        const auto path=temp_.filePath(QStringLiteral("metadata.png"));
        QVERIFY(!ix::CodecRegistry::exportLosses(document,"png").isEmpty());
        QVERIFY(!ix::CodecRegistry().save(document,path,"png",95,false).isEmpty()); QVERIFY(!QFileInfo::exists(path));
        QVERIFY(ix::CodecRegistry().save(document,path,"png",95,true).isEmpty());
    }
    void mappedDisplayColorConversionMatchesQt() {
        auto image=ix::copyImageRegion(fixture(),fixture().rect()); image.setColorSpace(QColorSpace::DisplayP3);
        const auto display=ix::displayImage(image,true,false);
        QVERIFY(ix::imageIsFileBacked(display)); QCOMPARE(display,image.convertedToColorSpace(QColorSpace::SRgb));
        QCOMPARE(image.colorSpace(),QColorSpace(QColorSpace::DisplayP3));
    }
    void resizeMatchesReference_data() {
        QTest::addColumn<int>("format"); QTest::addColumn<int>("filter");
        for (int format : {int(QImage::Format_RGB32),int(QImage::Format_ARGB32),int(QImage::Format_RGBA64)})
            for (int f=0;f<=6;++f) QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(format).arg(f)))<<format<<f;
    }
    void resizeMatchesReference() {
        QFETCH(int,format); QFETCH(int,filter);
        const auto source=fixture(QImage::Format(format));
        ix::ResizeOptions o; o.size={73,41}; o.filter=ix::ResampleFilter(filter); o.linearLight=true;
        const auto out=ix::resample(source,o); QVERIFY(ix::imageIsFileBacked(out));
        QCOMPARE(out,ix::test::referenceResample(source,o));
        std::atomic_bool cancel=true;
        try { QVERIFY(ix::resample(source,o,&cancel).isNull()); } catch (const std::runtime_error &) { QVERIFY(filter==6); }
        QCOMPARE(ix::mappedImageBytes(),out.sizeInBytes()+65536);
    }
    void parallelMappedResizeMatchesLegacyAndReleases() {
        const auto source=fixture(QImage::Format_RGBA64);
        ix::ResizeOptions options; options.size={2048,1536};
        const auto expected=ix::test::referenceResample(source,options);
        const auto image=ix::resample(source,options);
        QVERIFY(ix::imageIsFileBacked(image)); QCOMPARE(image,expected);
        QCOMPARE(ix::mappedImageBytes(),image.sizeInBytes()+65536);
    }
    void wideMappedResizeMatchesLegacy_data() {
        QTest::addColumn<int>("filter");
        for (int filter : {1,5,6}) QTest::newRow(qPrintable(QString::number(filter))) << filter;
    }
    void wideMappedResizeMatchesLegacy() {
        QFETCH(int,filter);
        ix::configureImageStorage({true,64*ix::MiB,temp_.path(),1});
        const auto source=fixture(QImage::Format_RGBA64);
        ix::ResizeOptions options; options.size={100000,65}; options.filter=ix::ResampleFilter(filter);
        const auto expected=ix::test::referenceResample(source,options);
        QString backing;
        {
            const auto image=ix::resample(source,options);
            QVERIFY(ix::imageIsFileBacked(image)); QCOMPARE(image,expected);
            backing=ix::imageBackingFile(image);
            QCOMPARE(ix::mappedImageBytes(),image.sizeInBytes()+65536);
        }
        QCOMPARE(ix::mappedImageBytes(),0);
        ix::flushImageStorageDeletes();
        QVERIFY(!QFileInfo::exists(backing));
    }
    void mappedDecodeAndAtomicExport_data() {
        QTest::addColumn<QByteArray>("format");
        QTest::newRow("png")<<QByteArray("png"); QTest::newRow("jpeg")<<QByteArray("jpeg");
    }
    void mappedDecodeAndAtomicExport() {
        QFETCH(QByteArray,format); const auto source=fixture(QImage::Format_RGB32);
        const auto path=temp_.filePath(QStringLiteral("input.")+QString::fromLatin1(format)); QVERIFY(source.save(path,format.constData()));
        ix::CodecRegistry registry; const auto read=registry.read(path); QVERIFY2(read.ok(),qPrintable(read.error));
        QVERIFY(ix::imageIsFileBacked(read.document.pixels)); QCOMPARE(read.document.pixels,QImage(path));
        const auto target=temp_.filePath(QStringLiteral("output.")+QString::fromLatin1(format));
        QVERIFY(registry.save(read.document,target,format,100,true).isEmpty());
        QCOMPARE(QImage(target).size(),source.size());
        if (format=="png") QCOMPARE(QImage(target),source);
        QFile saved(target); QVERIFY(saved.open(QIODevice::ReadOnly)); const auto before=saved.readAll(); saved.close();
        QVERIFY(!registry.save(read.document,target,format,95,true,std::nullopt,[]{return QStringLiteral("cancelled");}).isEmpty());
        QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(),before);
    }
    void progressiveJpegHasHonestError() {
        const auto path=temp_.filePath(QStringLiteral("progressive.jpg"));
        QImageWriter writer(path,"jpeg"); writer.setProgressiveScanWrite(true); QVERIFY(writer.write(fixture(QImage::Format_RGB32)));
        const auto result=ix::CodecRegistry().read(path); QVERIFY(!result.ok()); QVERIFY(result.error.contains(QStringLiteral("progressive")));
    }
    void turboJpegKeepsDocumentIdentity() {
        ix::configureImageStorage({false, 0, temp_.path()}); // no spill → turbo path
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::red);
        image.setDotsPerMeterX(2835); // 72 dpi
        image.setDotsPerMeterY(2835);
        const auto path = temp_.filePath(QStringLiteral("identity.jpg"));
        QVERIFY(image.save(path, "jpeg", 95));
        ix::CodecRegistry registry;
        const auto read = registry.read(path);
        QVERIFY2(read.ok(), qPrintable(read.error));
        QCOMPARE(read.document.path, QFileInfo(path).absoluteFilePath());
        QCOMPARE(read.document.format, QByteArray("jpeg"));
        QCOMPARE(read.document.page, 0);
        QCOMPARE(read.document.pageCount, 1);
        QVERIFY(!read.document.animated);
        QCOMPARE(read.document.pixels.size(), QSize(40, 30));
        QVERIFY2(read.document.pixels.dotsPerMeterX() > 0, "JFIF density lost");
        QVERIFY2(read.document.pixels.dotsPerMeterY() > 0, "JFIF density lost");
    }
    void turboJpegFallsBackForGrayscaleSources() {
        ix::configureImageStorage({false, 0, temp_.path()});
        QImage image(24, 18, QImage::Format_Grayscale8);
        for (int y = 0; y < image.height(); ++y)
            std::memset(image.scanLine(y), 64 + y * 4, size_t(image.bytesPerLine()));
        const auto path = temp_.filePath(QStringLiteral("gray.jpg"));
        QVERIFY(image.save(path, "jpeg", 95));
        const auto read = ix::CodecRegistry().read(path);
        QVERIFY2(read.ok(), qPrintable(read.error));
        // Turbo expands everything to RGB; the fallback keeps Qt's format.
        QCOMPARE(read.document.pixels.format(), QImage(path).format());
        // The document size is bookkeeping, not decoder output: both paths owe it.
        QCOMPARE(read.document.fileBytes, QFileInfo(path).size());
    }
    void turboJpegFallsBackForExifOrientation() {
        ix::configureImageStorage({false, 0, temp_.path()});
        QImage image(40, 24, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QByteArray encoded;
        QBuffer buffer(&encoded);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QImageWriter writer(&buffer, "jpeg");
        writer.setQuality(95);
        QVERIFY(writer.write(image));
        buffer.close();
        const auto path = temp_.filePath(QStringLiteral("oriented.jpg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(jpegWithExifOrientation(encoded, 6)); // rotate 90° CW
        file.close();
        const auto read = ix::CodecRegistry().read(path);
        QVERIFY2(read.ok(), qPrintable(read.error));
        // The turbo path ignores orientation; the fallback must transpose.
        QCOMPARE(read.document.pixels.size(), QSize(24, 40));
        QImageReader reference(path);
        reference.setAutoTransform(true);
        QCOMPARE(read.document.pixels, reference.read());
    }
    void turboJpegFallsBackForIccProfile() {
        ix::configureImageStorage({false, 0, temp_.path()});
        QImage image(32, 20, QImage::Format_RGB32);
        image.fill(Qt::green);
        QByteArray encoded;
        QBuffer buffer(&encoded);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QImageWriter writer(&buffer, "jpeg");
        writer.setQuality(95);
        QVERIFY(writer.write(image));
        buffer.close();
        const auto icc = QColorSpace(QColorSpace::SRgb).iccProfile();
        QVERIFY(!icc.isEmpty());
        const auto path = temp_.filePath(QStringLiteral("tagged.jpg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(jpegWithIccProfile(encoded, icc));
        file.close();
        const auto read = ix::CodecRegistry().read(path);
        QVERIFY2(read.ok(), qPrintable(read.error));
        // Turbo would drop the profile; the fallback keeps it on the image.
        QVERIFY2(read.document.pixels.colorSpace().isValid(), "ICC profile lost");
        QImageReader reference(path);
        QCOMPARE(read.document.pixels.colorSpace(), reference.read().colorSpace());
    }
    // Every width alignment must decode identically: QImage's 24-bit stride is
    // 32-bit aligned, so a fast path that assumes a tight pitch shears rows.
    void turboJpegMatchesQtForUnalignedWidths() {
        ix::configureImageStorage({false, 0, temp_.path()});
        ix::TurboJpegCodec codec;
        for (const int width : {64, 65, 66, 67}) {
            QImage source(width, 48, QImage::Format_RGB32);
            for (int y = 0; y < source.height(); ++y)
                for (int x = 0; x < width; ++x)
                    source.setPixel(x, y, qRgb(x * 255 / (width - 1), y * 255 / 47, 96));
            const auto path = temp_.filePath(QStringLiteral("unaligned-%1.jpg").arg(width));
            QVERIFY(source.save(path, "jpeg", 95));
            QVERIFY(codec.accepts(path));
            const auto read = codec.read(path, 0);
            QVERIFY2(read.ok(), qPrintable(read.error));
            QCOMPARE(read.document.pixels.size(), source.size());
            QCOMPARE(read.document.fileBytes, QFileInfo(path).size());
            QImageReader reference(path);
            reference.setAutoTransform(false);
            const auto expected = reference.read().convertToFormat(QImage::Format_RGB32);
            QVERIFY2(!expected.isNull(), qPrintable(reference.errorString()));
            QCOMPARE(expected.size(), read.document.pixels.size());
            double deviation = 0;
            for (int y = 0; y < expected.height(); ++y)
                for (int x = 0; x < expected.width(); ++x) {
                    const auto a = expected.pixel(x, y), b = read.document.pixels.pixel(x, y);
                    deviation += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
                                 std::abs(qBlue(a) - qBlue(b));
                }
            deviation /= double(expected.width()) * expected.height() * 3.0;
            QVERIFY2(deviation < 4.0,
                     qPrintable(QStringLiteral("width %1 deviates by %2").arg(width).arg(deviation)));
        }
    }
    // A truncated stream must either decode or fail - but never disagree with Qt
    // about readability, because the codec that claimed the file owns it.
    void turboJpegMatchesQtForCorruptStreams() {
        ix::configureImageStorage({false, 0, temp_.path()});
        QImage source(240, 180, QImage::Format_RGB32);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                source.setPixel(x, y, qRgb((x * 7) % 256, (y * 11) % 256, (x + y) % 256));
        const auto path = temp_.filePath(QStringLiteral("truncated.jpg"));
        QVERIFY(source.save(path, "jpeg", 92));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(file.size() * 3 / 5)); // cut the entropy-coded tail
        file.close();
        QImageReader reference(path);
        reference.setAutoTransform(false);
        const auto expected = reference.read();
        ix::TurboJpegCodec codec;
        if (!codec.accepts(path))
            QSKIP("the truncated header no longer passes accepts()");
        const auto read = codec.read(path, 0);
        if (expected.isNull()) {
            QVERIFY2(!read.ok(), "turbo claims an image that Qt cannot read");
        } else {
            QVERIFY2(read.ok(), qPrintable(read.error));
            QCOMPARE(read.document.pixels.size(), expected.size());
        }
    }
    void streamingAlphaFlattenMatchesQtAndKeepsDpi() {
        const auto source=fixture(); ix::ImageDocument document;
        document.pixels=ix::copyImageRegion(source,source.rect()); document.pixels.setDevicePixelRatio(2);
        const auto path=temp_.filePath(QStringLiteral("alpha.jpg"));
        QVERIFY(ix::CodecRegistry().save(document,path,"jpeg",100,true).isEmpty());
        QImage expected(source.size(),QImage::Format_RGB32); expected.fill(Qt::white);
        { QPainter painter(&expected); painter.drawImage(0,0,source); }
        QByteArray encoded; QBuffer buffer(&encoded); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(expected.save(&buffer,"jpeg",100));
        QCOMPARE(QImage(path),QImage::fromData(encoded,"jpeg"));
    }
    void orientationsAndCrop() {
        const auto source=fixture();
        for (int orientation=0;orientation<8;++orientation) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            auto expected=source.flipped(Qt::Orientations((orientation&1 ? Qt::Horizontal : 0) | (orientation&2 ? Qt::Vertical : 0)));
#else
            auto expected=source.mirrored(orientation&1,orientation&2);
#endif
            if (orientation&4) expected=expected.transformed(QTransform().rotate(90));
            QCOMPARE(ix::transformImageOrientation(source,orientation),expected);
        }
        QCOMPARE(ix::copyImageRegion(source,{3,4,15,11}),source.copy({3,4,15,11}));
    }
    void mappedJpegExifOrientationsMatchReader() {
        const auto path=temp_.filePath(QStringLiteral("oriented.jpg"));
        for (int orientation=0;orientation<8;++orientation) {
            QImageWriter writer(path,"jpeg"); writer.setTransformation(QImageIOHandler::Transformations(orientation));
            QVERIFY(writer.write(fixture(QImage::Format_RGB32)));
            QImageReader reader(path); reader.setAutoTransform(true); const auto expected=reader.read(); QVERIFY(!expected.isNull());
            const auto result=ix::CodecRegistry().read(path); QVERIFY2(result.ok(),qPrintable(result.error));
            QVERIFY(ix::imageIsFileBacked(result.document.pixels)); QCOMPARE(result.document.pixels,expected);
        }
    }
    void displayAndUndoRetainOriginal() {
        ix::configureImageStorage({true,512*ix::MiB,temp_.path(),1});
        auto source=ix::allocateImage({100001,512},QImage::Format_RGB32);
        source.fill(Qt::red);
        for (int y=0;y<source.height();++y) {
            auto *row=reinterpret_cast<QRgb *>(source.scanLine(y));
            std::fill(row+source.width()/2,row+source.width(),qRgb(0,255,0));
        }
        ix::ImageDocument doc; doc.pixels=source;
        const auto path=temp_.filePath(QStringLiteral("wide-original.png"));
        ix::CodecRegistry registry; QVERIFY(registry.save(doc,path,"png",0,true).isEmpty());
        {
            const auto restored=registry.read(path); QVERIFY2(restored.ok(),qPrintable(restored.error));
            QVERIFY(ix::imageIsFileBacked(restored.document.pixels)); QCOMPARE(restored.document.pixels,source);
        }
        const auto jpeg=temp_.filePath(QStringLiteral("too-wide.jpg"));
        QVERIFY(registry.save(doc,jpeg,"jpeg",95,true).contains(QStringLiteral("65.500"))); QVERIFY(!QFileInfo::exists(jpeg));
        ix::DocumentHistory history; history.recordEdit(doc,{}); source={}; doc.pixels={};
        QVERIFY(history.target(ix::DocumentHistory::Direction::Undo));
        const auto &pixels=history.target(ix::DocumentHistory::Direction::Undo)->pixels;
        QCOMPARE(pixels.size(),QSize(100001,512));
        ix::ImageView view; view.resize(480,320); view.setImage(pixels); view.show();
        QImage screenshot(view.size(),QImage::Format_RGB32); view.render(&screenshot);
        QCOMPARE(pixels.pixelColor(100000,1),QColor(Qt::green));
        QCOMPARE(screenshot.pixelColor(screenshot.width()/4,screenshot.height()/2),QColor(Qt::red));
        QCOMPARE(screenshot.pixelColor(screenshot.width()*3/4,screenshot.height()/2),QColor(Qt::green));
        view.setSelection({90000,200,1000,100}); QCOMPARE(view.selection(),QRect(90000,200,1000,100));
    }
    void crashCleanupPreservesUnknownFiles() {
        const auto unknown=temp_.filePath(QStringLiteral("pixels-00000000-0000-0000-0000-000000000001.bin"));
        QFile file(unknown); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("personal data"); file.close();
        QProcess child; child.start(QCoreApplication::applicationFilePath(),{QStringLiteral("-platform"),QStringLiteral("offscreen"),QStringLiteral("--leave-swap"),temp_.path()});
        QVERIFY(child.waitForFinished(30000)); QCOMPARE(child.exitCode(),0);
        const auto path=QString::fromUtf8(child.readAllStandardOutput()).trimmed(); QVERIFY(QFileInfo::exists(path));
        QCOMPARE(ix::cleanupStaleImageStorage(temp_.path()),1);
        QVERIFY(!QFileInfo::exists(path)); QVERIFY(QFileInfo::exists(unknown));
    }
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    try {
        if (app.arguments().contains(QStringLiteral("--leave-swap"))) {
            ix::configureImageStorage({true,32*ix::MiB,app.arguments().last(),1});
            auto image=ix::allocateImage({20,20},QImage::Format_RGB32); image.fill(Qt::green);
            const auto path=ix::imageBackingFile(image).toUtf8(); std::fwrite(path.constData(),1,size_t(path.size()),stdout); std::fflush(stdout);
            std::_Exit(0);
        }
        LargeImageTests tests; return QTest::qExec(&tests,argc,argv);
    } catch (const std::exception &error) {
        qCritical().noquote() << "Large-image test exception:" << error.what();
        return 1;
    }
}
#include "large_images_ixview.moc"
