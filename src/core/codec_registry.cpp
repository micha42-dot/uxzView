// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "codec_registry.h"
#include "image_storage.h"
#include "image_export_policy.h"
#include "apng_codec.h"
#include "file_revision.h"
#include "localization.h"
#include "plugin_store.h"
#include "preview_service.h"
#include "turbojpeg_codec.h"
#include <QBuffer>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QMap>
#include <QLibrary>
#include <mutex>
namespace ix {
bool isRawFormat(const QByteArray &format) {
    static const auto formats = QByteArray("3fr arw bay bmq cap cine cr2 cr3 crw cs1 dc2 dcr dng drf eip erf fff "
        "ia iiq k25 kc2 kdc mdc mef mos mrw nef nrw obm orf pef ptx pxn qtk raf raw rdc rw2 rwl rwz "
        "sr2 srf srw sti x3f").split(' ');
    return formats.contains(format.toLower());
}
namespace {
#ifdef IX_PDF_MODULE
ImageResult readPdf(const QString &path, int page) {
    static QLibrary module(optionalModulePath(QStringLiteral(IX_PDF_MODULE)));
    using Read = ImageResult (*)(const QString &, int, qint64);
    static const auto read = reinterpret_cast<Read>(module.resolve("ixReadPdf"));
    auto result = read ? read(path, page, imageMemoryLimit()) : ImageResult{{}, QStringLiteral("PDF-Modul ist nicht verfügbar.")};
    result.error = localized(result.error);
    return result;
}
class PdfCodec final : public ImageCodec {
  public:
    bool accepts(const QString &path) const override { return QFileInfo(path).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0; }
    ImageResult read(const QString &path, int page) const override { return readPdf(path, page); }
    QList<FormatCapability> capabilities() const override {
        return {{QStringLiteral("PDF"), {QStringLiteral("pdf")}, PluginStore::formatEnabled("pdf"), false, QStringLiteral("Qt PDF / PDFium")}};
    }
};
#endif
QString backend(const QByteArray &f) {
    if (f == "cbz" || f == "zip")
        return QStringLiteral("üxzView / KArchive");
    if (f == "jpeg" || f == "jfif")
        return QStringLiteral("Qt / libjpeg-turbo");
    if (f == "png")
        return QStringLiteral("Qt / libpng");
    if (f == "tiff")
        return QStringLiteral("Qt / libtiff");
    if (f == "webp")
        return QStringLiteral("Qt / libwebp");
    if (f == "avif")
        return QStringLiteral("KImageFormats / libavif");
    if (f == "heif" || f == "heic" || f == "avci" || f == "hej2")
        return QStringLiteral("KImageFormats / libheif");
    if (f == "jxl")
        return QStringLiteral("KImageFormats / libjxl");
    if (f == "exr")
        return QStringLiteral("KImageFormats / OpenEXR");
    if (f == "jp2" || f == "j2k")
        return QStringLiteral("Qt/KImageFormats / JasPer/OpenJPEG");
    if (f == "svg" || f == "svgz")
        return QStringLiteral("Qt SVG");
    if (isRawFormat(f))
        return QStringLiteral("KImageFormats / LibRaw");
    return QStringLiteral("Qt / KImageFormats");
}
bool alphaWritable(const QByteArray &f) {
    return QList<QByteArray>{"png",  "tiff", "webp", "ico", "cur", "avif", "heif",
                             "heic", "jxl",  "exr",  "dds", "tga", "qoi"}
        .contains(f);
}
QByteArray normalized(QByteArray f) {
    f = f.toLower();
    if (f == "jpg" || f == "jpe")
        return "jpeg";
    if (f == "tif")
        return "tiff";
    return f;
}
class QtCodec final : public ImageCodec {
  public:
    bool accepts(const QString &) const override {
        return true;
    }
    ImageResult read(const QString &path, int page) const override {
        const bool profile=qEnvironmentVariableIntValue("IX_PROFILE_IMAGE_LOAD")!=0;
        QElapsedTimer timer; if(profile) timer.start();
        qint64 previous=0;
        const auto stage=[&](const char *name) {
            if (!profile) return;
            const auto now=timer.nsecsElapsed();
            qInfo("ix-codec %s %.3f ms",name,(now-previous)/1e6); previous=now;
        };
        ImageResult out;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (!reader.canRead()) {
            out.error = reader.errorString();
            return out;
        }
        const auto detectedFormat = reader.format();
        if (!PluginStore::formatEnabled(normalized(detectedFormat))) {
            out.error = IX_TEXT("Dieses Format wurde in der Plugin-Verwaltung deaktiviert.");
            return out;
        }
#ifdef IX_PDF_MODULE
        if (detectedFormat == "pdf") return readPdf(path, page);
#else
        if (detectedFormat == "pdf") return {{}, IX_TEXT("PDF-Modul ist nicht verfügbar.")};
#endif
        // Query the directory count before size(): the TIFF plugin may open a
        // second libtiff handle on the same device when counting pages. Reading
        // size first leaves that device past its header in some Qt versions.
        const int count = reader.imageCount();
        auto dimensions = reader.size();
        const auto pixelFormat = reader.imageFormat();
        const auto pixelBytes = imageBufferBytes(dimensions,pixelFormat);
        const bool large = dimensions.isValid() && (!safeSize(dimensions) ||
            (imageStorageOptions().spillThresholdBytes > 0 && pixelBytes > imageStorageOptions().spillThresholdBytes));
        const qint64 decodeRows=qint64(dimensions.width())*64 + qint64(dimensions.height())*sizeof(void *);
        if (large && ((detectedFormat != "png" && detectedFormat != "jpeg" && detectedFormat != "jpg") ||
            page != 0 || pixelBytes <= 0 || !canAllocateImage(dimensions,pixelFormat,decodeRows))) {
            out.error = imageSizeLimitError(dimensions);
            return out;
        }
        if (large && (detectedFormat == "jpeg" || detectedFormat == "jpg")) {
            // Progressive JPEG keeps coefficient arrays for the entire image;
            // backing only its final pixels would not bound decoder memory.
            QFile header(path);
            bool sequential = false;
            if (header.open(QIODevice::ReadOnly) && header.read(2) == QByteArray::fromHex("ffd8")) {
                for (int segments=0; segments<4096 && header.pos()<16*MiB; ++segments) {
                    char marker=0, code=0;
                    if (!header.getChar(&marker) || uchar(marker)!=0xff) break;
                    do { if (!header.getChar(&code)) break; } while (uchar(code)==0xff && header.pos()<16*MiB);
                    if (uchar(code)==0xc0 || uchar(code)==0xc1) { sequential=true; break; }
                    if (uchar(code)==0xc2 || uchar(code)==0xca || uchar(code)==0xda || uchar(code)==0xd9) break;
                    const auto length=header.read(2);
                    if (length.size()!=2) break;
                    const int bytes=(uchar(length[0])<<8)|uchar(length[1]);
                    if (bytes<2 || !header.seek(header.pos()+bytes-2)) break;
                }
            }
            if (!sequential) { out.error=IX_TEXT("Dieses große JPEG benötigt einen sequenziellen Decoder; progressive JPEGs überschreiten das RAM-Budget."); return out; }
        }
        if (page < 0 || (count > 0 && page >= count)) {
            out.error = IX_TEXT("Seite nicht vorhanden.");
            return out;
        }
        if (page > 0 && !reader.jumpToImage(page)) {
            // Animated formats often only support sequential access.
            reader.setFileName(path);
            reader.setAutoTransform(true);
            for (int i = 0; i < page; ++i)
                if (reader.read().isNull()) {
                    out.error = reader.errorString();
                    return out;
                }
        }
        auto &d = out.document;
        stage("header");
        std::optional<ImageLoadCache::PreviewPixels> preview;
        if (!large && page == 0 && count == 1 && (detectedFormat == "png" || detectedFormat == "bmp") &&
            reader.transformation() == QImageIOHandler::TransformationNone)
            preview = ImageLoadCache::takePreviewPixels(path, dimensions, pixelFormat, detectedFormat);
        if (preview && profile) qInfo("ix-codec reused preview pixels");
        if (!preview && !large && page == 0 && count == 1 && pixelBytes >= MiB && pixelBytes <= 96 * MiB &&
            (detectedFormat == "png" || detectedFormat == "bmp") &&
            reader.transformation() == QImageIOHandler::TransformationNone &&
            !reader.supportsOption(QImageIOHandler::ScaledSize) &&
            (detectedFormat != "png" || !ApngCodec().accepts(path))) {
            preview = ImageLoadCache::decodePreviewPixels(path, dimensions, pixelFormat, detectedFormat, [&] {
                auto pixels = reader.read();
                return ImageLoadCache::PreviewPixels{pixels, std::max(20, reader.nextImageDelay()), reader.loopCount()};
            });
        }
        if (preview) {
            d.pixels = std::move(preview->pixels);
        } else if (large) {
            try {
                const auto transformation = reader.transformation();
                reader.setAutoTransform(false);
                d.pixels = allocateImage(dimensions,pixelFormat,decodeRows,true);
                const auto *mapped = d.pixels.constBits();
                if (!reader.read(&d.pixels) || d.pixels.constBits()!=mapped) {
                    d.pixels={}; out.error=IX_TEXT("Der Decoder kann dieses Bild nicht direkt in den ausgelagerten Puffer lesen."); return out;
                }
                d.pixels=transformImageOrientation(d.pixels,int(transformation));
            } catch (const std::exception &error) { d.pixels={}; out.error=localized(QString::fromUtf8(error.what())); return out; }
        } else d.pixels = reader.read();
        if (d.empty()) {
            out.error = reader.errorString();
            return out;
        }
        if (!imageIsFileBacked(d.pixels) && !safeSize(d.pixels.size(), std::max(8, d.pixels.depth() / 8))) {
            out.error = IX_TEXT("Bild zu groß.");
            d.pixels = {};
            return out;
        }
        stage("pixels");
        d.path = QFileInfo(path).absoluteFilePath();
        // Some handlers clear their format after reading the final image.
        d.format = detectedFormat;
        d.page = page;
        d.pageCount = std::max(1, count);
        d.animated = reader.supportsAnimation() && (count != 1);
        d.delayMs = preview ? preview->delayMs : std::max(20, reader.nextImageDelay());
        d.loopCount = preview ? preview->loopCount : reader.loopCount();
        for (const auto &key : d.pixels.textKeys())
            d.text.insert(key, d.pixels.text(key));
        QFile file(path);
        d.fileBytes = file.size();
        stage("properties");
        d.metadata = readMetadata(path, d.metadataWarning);
        if (d.metadata) {
            d.sourceHasExif = d.metadata->hasExif || d.metadata->hasIptc || d.metadata->hasXmp;
        } else if (file.open(QIODevice::ReadOnly)) {
            // Only probe when the metadata reader cannot supply an answer.
            // Preserve conservative export warnings in unsupported/metadata-free
            // builds without rereading a header after successful Exiv2 parsing.
            auto head = file.read(256 * 1024);
            d.sourceHasExif = head.contains("Exif\0\0") || head.contains("http://ns.adobe.com/xap/") ||
                              head.contains("Photoshop 3.0");
        }
        stage("metadata");
        return out;
    }
    QStringList extensions() const override {
        // Read Qt plugin metadata without constructing decoders or mapping their dependencies.
        static const QStringList keys = [] {
            QStringList out{QStringLiteral("bmp"),  QStringLiteral("dib"), QStringLiteral("jpg"),
                            QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("pbm"),
                            QStringLiteral("pgm"),  QStringLiteral("ppm"), QStringLiteral("xbm"),
                            QStringLiteral("xpm")};
            for (auto plugin : PluginStore::builtins())
                for (auto format : plugin.toObject().value(QStringLiteral("formats")).toArray())
                    out.append(format.toString());
            out.removeDuplicates();
            return out;
        }();
        QStringList result;
        for (const auto &key : keys)
            if (key != QStringLiteral("pdf") && PluginStore::formatEnabled(key.toLatin1()))
                result.append(key);
        return result;
    }
    QList<FormatCapability> capabilities() const override {
        static const QList<FormatCapability> cached = [] {
            QMap<QByteArray, FormatCapability> formats;
            for (const auto &ext : QImageReader::supportedImageFormats()) {
                auto key = normalized(ext);
                if (key == "pdf") continue; // The bounded PDF codec owns this capability.
                auto &f = formats[key];
                f.name = QString::fromLatin1(key).toUpper();
                f.extensions.append(QString::fromLatin1(ext));
                f.read = true;
                f.library = backend(key);
            }
            for (const auto &ext : QImageWriter::supportedImageFormats()) {
                auto key = normalized(ext);
                if (key == "pdf") continue;
                auto &f = formats[key];
                f.name = QString::fromLatin1(key).toUpper();
                if (!f.extensions.contains(QString::fromLatin1(ext)))
                    f.extensions.append(QString::fromLatin1(ext));
                f.write = key != "icns";
                f.library = backend(key);
            }
            return formats.values();
        }();
        auto result = cached;
        for (auto &cap : result)
            if (!PluginStore::formatEnabled(normalized(cap.extensions.first().toLatin1())))
                cap.read = cap.write = false;
        return result;
    }
};
} // namespace
CodecRegistry::CodecRegistry() {
    initializeImageMemoryLimit();
    add(std::make_shared<QtCodec>());
    add(std::make_shared<ApngCodec>());
    add(std::make_shared<TurboJpegCodec>());
#ifdef IX_PDF_MODULE
    add(std::make_shared<PdfCodec>());
#endif
}
void CodecRegistry::add(std::shared_ptr<ImageCodec> c) {
    codecs_.prepend(std::move(c));
}
ImageResult CodecRegistry::read(const QString &p, int page) const {
    if (!PluginStore::formatEnabled(QFileInfo(p).suffix().toLower().toLatin1()))
        return {{}, IX_TEXT("Dieses Format wurde in der Plugin-Verwaltung deaktiviert.")};
    if (!QFileInfo(p).isFile())
        return {{}, IX_TEXT("Datei nicht gefunden: %1").arg(p)};
    for (const auto &c : codecs_)
        if (c->accepts(p)) {
            auto result = c->read(p, page);
            if (result.ok() && !PluginStore::formatEnabled(normalized(result.document.format)))
                return {{}, IX_TEXT("Dieses Format wurde in der Plugin-Verwaltung deaktiviert.")};
            if (result.ok())
                result.document.sourceModified = QFileInfo(p).lastModified();
            return result;
        }
    return {{}, IX_TEXT("Kein passender Decoder gefunden.")};
}
bool CodecRegistry::canWrite(const QByteArray &f) {
    if (!PluginStore::formatEnabled(normalized(f)))
        return false;
    if (normalized(f) == "icns" || normalized(f) == "pdf")
        return false;
    static const auto writers = QImageWriter::supportedImageFormats();
    for (const auto &x : writers)
        if (normalized(x) == normalized(f))
            return true;
    return false;
}
QStringList CodecRegistry::exportLosses(const ImageDocument &d, const QByteArray &f) {
    QStringList losses;
    auto format = normalized(f);
    if (d.pageCount > 1)
        losses.append(IX_TEXT("Es wird nur die aktuelle Seite / der aktuelle Frame gespeichert."));
    if (d.sourceHasExif && (!d.metadata || !metadataWritable(format) || exportNeedsStreaming(d.pixels)))
        losses.append(
            IX_TEXT("EXIF-, IPTC- und XMP-Daten werden vom Qt-Encoder nicht vollständig übernommen. "
                    "Die EXIF-Ausrichtung ist bereits auf die Pixel angewendet."));
    if (d.pixels.hasAlphaChannel() && !alphaWritable(format))
        losses.append(IX_TEXT("Dieses Ausgabeformat erhält den Alphakanal nicht zuverlässig. "
                              "Transparenz wird auf Weiß reduziert."));
    if (d.pixels.pixelFormat().typeInterpretation() == QPixelFormat::FloatingPoint && format != "exr")
        losses.append(IX_TEXT("HDR-Gleitkommawerte können auf den Bereich 0–1 begrenzt werden."));
    if (imageChannelBits(d.pixels) > 8 && format != "png" && format != "tiff" && format != "exr")
        losses.append(IX_TEXT("Die Farbtiefe wird auf 8 Bit pro Kanal reduziert."));
    if (d.pixels.colorSpace().isValid() && format != "png" && format != "jpeg" && format != "tiff" &&
        format != "webp")
        losses.append(IX_TEXT("Das ICC-Farbprofil wird in diesem Format nicht zuverlässig gespeichert."));
    return losses;
}
QString CodecRegistry::save(const ImageDocument &d, const QString &path, const QByteArray &requested,
                            int quality, bool allowLoss, const std::optional<QString> &expectedTarget,
                            const std::function<QString()> &beforeCommit) const {
    QByteArray format = normalized(requested.isEmpty() ? QFileInfo(path).suffix().toLatin1() : requested);
    if (d.empty())
        return IX_TEXT("Kein Bild geladen.");
    if (!canWrite(format))
        return IX_TEXT("Kein Encoder für %1 verfügbar.").arg(QString::fromLatin1(format));
    if (!allowLoss && !exportLosses(d, format).isEmpty())
        return exportLosses(d, format).join(QLatin1Char('\n'));
    const bool large = exportNeedsStreaming(d.pixels);
    if (format == "jpeg" && (d.pixels.width()>65500 || d.pixels.height()>65500))
        return IX_TEXT("JPEG unterstützt höchstens 65.500 Pixel pro Achse. Bitte PNG verwenden.");
    if (large && qint64(d.pixels.width())*64 + qint64(d.pixels.height())*sizeof(void *) > imageMemoryLimit())
        return imageSizeLimitError({d.pixels.width(),1},64);
    if (large && format != "png" && format != "jpeg")
        return IX_TEXT("Für dieses große Bild ist nur der zeilenweise PNG- oder JPEG-Export freigegeben.");
    const auto targetRevision = [&] {
        return QFileInfo::exists(path) ? imageFileKey(path) : QString();
    };
    const auto current = targetRevision();
    const auto authorized = expectedTarget.value_or(current);
    const auto changedTarget = [] {
        return IX_TEXT("Die Zieldatei wurde inzwischen verändert. Bitte vor dem Speichern erneut prüfen.");
    };
    const auto regularTarget = [&] {
        const QFileInfo info(path);
        return !info.isSymLink() && !info.isJunction() && (!info.exists() || info.isFile());
    };
    if (!regularTarget()) return changedTarget();
    if (current != authorized)
        return changedTarget();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    QByteArray encoded;
    QBuffer buffer(&encoded);
    const bool attach = d.metadata && metadataWritable(format) && !large;
    QIODevice *destination = &file;
    if (attach) {
        buffer.open(QIODevice::WriteOnly);
        destination = &buffer;
    }
    // Only metadata rewriting requires a complete encoded memory buffer.
    // Other exports can stream into the same atomic QSaveFile transaction.
    QImageWriter writer(destination, format);
    writer.setQuality(quality);
    QImage output = d.pixels;
    if (output.hasAlphaChannel() && !alphaWritable(format)) {
        QImage flat;
        try { flat = allocateImage(output.size(), QImage::Format_RGB32, imageIsFileBacked(output) ? 0 : output.sizeInBytes()); }
        catch (const std::exception &error) { file.cancelWriting(); return localized(QString::fromUtf8(error.what())); }
        for (int y=0; y<output.height(); ++y) {
            QImage input(output.constScanLine(y),output.width(),1,output.bytesPerLine(),output.format());
            input.setColorTable(output.colorTable());
            const auto premultiplied=input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (premultiplied.isNull()) { file.cancelWriting(); return IX_TEXT("Speicher nicht verfügbar."); }
            const auto *source=reinterpret_cast<const QRgb *>(premultiplied.constBits());
            auto *target=reinterpret_cast<QRgb *>(flat.scanLine(y));
            for (int x=0;x<flat.width();++x) {
                const auto pixel=source[x]; const int white=255-qAlpha(pixel);
                target[x]=qRgb(qRed(pixel)+white,qGreen(pixel)+white,qBlue(pixel)+white);
            }
        }
        inheritImageProperties(output, flat);
        output = flat;
    }
    if (!writer.write(output)) {
        file.cancelWriting();
        return writer.errorString();
    }
    if (attach) {
        QString error;
        encoded = attachMetadata(std::move(encoded), *d.metadata, output.size(), d.dpiX(), d.dpiY(),
                                 d.modified, error);
        if (!error.isEmpty()) {
            file.cancelWriting();
            return error;
        }
    }
    if (attach && file.write(encoded) != encoded.size()) {
        file.cancelWriting();
        return file.errorString();
    }
    if (beforeCommit) {
        const auto error = beforeCommit();
        if (!error.isEmpty()) { file.cancelWriting(); return error; }
    }
    // Encoding/metadata work can take time. Never publish over an intervening
    // rewrite, creation or deletion. QSaveFile discards the staged output.
    if (!regularTarget() || targetRevision() != authorized) {
        file.cancelWriting();
        return changedTarget();
    }
    if (!file.commit())
        return file.errorString();
    return {};
}
QList<FormatCapability> CodecRegistry::capabilities() const {
    QList<FormatCapability> all;
    for (const auto &c : codecs_)
        all.append(c->capabilities());
    return all;
}
QStringList CodecRegistry::extensions() const {
    QStringList x;
    for (const auto &codec : codecs_)
        x.append(codec->extensions());
    x.removeDuplicates();
    return x;
}
QString CodecRegistry::openFilter() const {
    QStringList patterns;
    QMap<QString, QStringList> groups;
    // Opening needs reader keys, not full reader/writer discovery, which maps
    // optional decoder libraries into the process before the chooser is shown.
    for (const auto &extension : extensions()) {
        const auto pattern = QStringLiteral("*.%1").arg(extension);
        patterns.append(pattern);
        groups[QString::fromLatin1(normalized(extension.toLatin1())).toUpper()].append(pattern);
    }
    QStringList filters{IX_TEXT("Bilddateien") + QStringLiteral(" (%1)").arg(patterns.join(QLatin1Char(' ')))};
    for (auto group = groups.cbegin(); group != groups.cend(); ++group)
        filters.append(QStringLiteral("%1 (%2)").arg(group.key(), group.value().join(QLatin1Char(' '))));
    filters.append(IX_TEXT("Alle Dateien") + QStringLiteral(" (*)"));
    return filters.join(QStringLiteral(";;"));
}
QString CodecRegistry::saveFilter() const {
    QStringList filters;
    for (const auto &f : capabilities())
        if (f.write)
            filters << QStringLiteral("%1 (*.%2)").arg(f.name, f.extensions.first());
    return filters.join(QStringLiteral(";;"));
}
} // namespace ix
