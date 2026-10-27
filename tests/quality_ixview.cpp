// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/codec_registry.h"
#include "core/file_journal.h"
#include "core/preview_service.h"
#include "convolution_reference.h"
#include "ui/main_window.h"
#include <QColorSpace>
#include <QJsonDocument>
#include <QSettings>
#include <QtTest>
#include <QtWidgets>
#ifdef IX_QUALITY_LCMS
#include <lcms2.h>
#endif
class QualityTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_{QStringLiteral(IX_QUALITY_OUTPUT "/run-quality-XXXXXX")};
  private slots:
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewQualityTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
        ix::ImageLoadCache::configure(0, 16, true);
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
