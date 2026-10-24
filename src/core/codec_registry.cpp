// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "codec_registry.h"
#include "apng_codec.h"
#include "localization.h"
#include "plugin_store.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <mutex>
namespace ix {
bool isRawFormat(const QByteArray &format) {
    static const auto formats = QByteArray("3fr arw bay bmq cap cine cr2 cr3 crw cs1 dc2 dcr dng drf eip erf fff "
        "ia iiq k25 kc2 kdc mdc mef mos mrw nef nrw obm orf pef ptx pxn qtk raf raw rdc rw2 rwl rwz "
        "sr2 srf srw sti x3f").split(' ');
    return formats.contains(format.toLower());
}
namespace {
QString backend(const QByteArray &f) {
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
        // Query the directory count before size(): the TIFF plugin may open a
        // second libtiff handle on the same device when counting pages. Reading
        // size first leaves that device past its header in some Qt versions.
        const int count = reader.imageCount();
        auto dimensions = reader.size();
        if (dimensions.isValid() && !safeSize(dimensions)) {
            out.error = IX_TEXT("Bild überschreitet das Speicherlimit (512 MiB / 100.000 Pixel pro Achse).");
            return out;
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
        d.pixels = reader.read();
        if (d.empty()) {
            out.error = reader.errorString();
            return out;
        }
        if (!safeSize(d.pixels.size(), std::max(8, d.pixels.depth() / 8))) {
            out.error = IX_TEXT("Bild zu groß.");
            d.pixels = {};
            return out;
        }
        d.path = QFileInfo(path).absoluteFilePath();
        // Some handlers clear their format after reading the final image.
        d.format = detectedFormat;
        d.page = page;
        d.pageCount = std::max(1, count);
        d.animated = reader.supportsAnimation() && (count != 1);
        d.delayMs = std::max(20, reader.nextImageDelay());
        d.loopCount = reader.loopCount();
        for (const auto &key : d.pixels.textKeys())
            d.text.insert(key, d.pixels.text(key));
        QFile file(path);
        d.fileBytes = file.size();
        // Conservative detection: Qt does not expose or preserve arbitrary EXIF/IPTC.
        if (file.open(QIODevice::ReadOnly)) {
            auto head = file.read(256 * 1024);
            d.sourceHasExif = head.contains("Exif\0\0") || head.contains("http://ns.adobe.com/xap/") ||
                              head.contains("Photoshop 3.0");
        }
        d.metadata = readMetadata(path, d.metadataWarning);
        if (d.metadata)
            d.sourceHasExif = d.metadata->hasExif || d.metadata->hasIptc || d.metadata->hasXmp;
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
            if (PluginStore::formatEnabled(key.toLatin1()))
                result.append(key);
        return result;
    }
    QList<FormatCapability> capabilities() const override {
        static const QList<FormatCapability> cached = [] {
            QMap<QByteArray, FormatCapability> formats;
            for (const auto &ext : QImageReader::supportedImageFormats()) {
                auto key = normalized(ext);
                auto &f = formats[key];
                f.name = QString::fromLatin1(key).toUpper();
                f.extensions.append(QString::fromLatin1(ext));
                f.read = true;
                f.library = backend(key);
            }
            for (const auto &ext : QImageWriter::supportedImageFormats()) {
                auto key = normalized(ext);
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
    static std::once_flag allocation;
    std::call_once(allocation, [] { QImageReader::setAllocationLimit(512); });
    add(std::make_shared<QtCodec>());
    add(std::make_shared<ApngCodec>());
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
            return result;
        }
    return {{}, IX_TEXT("Kein passender Decoder gefunden.")};
}
bool CodecRegistry::canWrite(const QByteArray &f) {
    if (!PluginStore::formatEnabled(normalized(f)))
        return false;
    if (normalized(f) == "icns")
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
    if (d.sourceHasExif && (!d.metadata || !metadataWritable(format)))
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
                            int quality, bool allowLoss) const {
    QByteArray format = normalized(requested.isEmpty() ? QFileInfo(path).suffix().toLatin1() : requested);
    if (d.empty())
        return IX_TEXT("Kein Bild geladen.");
    if (!canWrite(format))
        return IX_TEXT("Kein Encoder für %1 verfügbar.").arg(QString::fromLatin1(format));
    if (!allowLoss && !exportLosses(d, format).isEmpty())
        return exportLosses(d, format).join(QLatin1Char('\n'));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    QByteArray encoded;
    QBuffer buffer(&encoded);
    const bool attach = d.metadata && metadataWritable(format);
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
        QImage flat(output.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter painter(&flat);
        painter.drawImage(0, 0, output);
        painter.end();
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
    for (const auto &e : extensions())
        patterns << QStringLiteral("*.%1").arg(e);
    return QStringLiteral("Bilddateien (%1);;Alle Dateien (*)").arg(patterns.join(QLatin1Char(' ')));
}
QString CodecRegistry::saveFilter() const {
    QStringList filters;
    for (const auto &f : capabilities())
        if (f.write)
            filters << QStringLiteral("%1 (*.%2)").arg(f.name, f.extensions.first());
    return filters.join(QStringLiteral(";;"));
}
} // namespace ix
