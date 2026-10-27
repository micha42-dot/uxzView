// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "metadata.h"
#include <QFile>
#include <QMutex>
#include <algorithm>
#include <cstring>
#include <limits>
#ifdef IX_HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif
namespace ix {
namespace {
#ifdef IX_HAVE_EXIV2
#if EXIV2_TEST_VERSION(0, 28, 0)
// Keep Qt's Unicode paths and read only the ranges requested by Exiv2. Some
// parsers require mmap(); give those an owned, bounded snapshot, never a live
// OS mapping that another process could truncate while it is being parsed.
class MetadataIo final : public Exiv2::BasicIo {
  public:
    explicit MetadataIo(QFile &file)
        : file_(file), size_(file.size()), path_(file.fileName().toUtf8().toStdString()) {}
    int open() override {
        denied_ = false;
        return seek(0, beg);
    }
    int close() override {
        return 0; // The enclosing readMetadata scope owns the open file.
    }
    size_t write(const Exiv2::byte *, size_t) override {
        denied_ = true;
        return 0;
    }
    size_t write(BasicIo &) override {
        denied_ = true;
        return 0;
    }
    int putb(Exiv2::byte) override {
        denied_ = true;
        return EOF;
    }
    void transfer(BasicIo &) override {
        throw Exiv2::Error(Exiv2::ErrorCode::kerImageWriteFailed);
    }
    Exiv2::DataBuf read(size_t count) override {
        count = std::min(count, remaining());
        if (!count)
            return {};
        Exiv2::DataBuf buffer(count);
        buffer.resize(read(buffer.data(), count));
        return buffer;
    }
    size_t read(Exiv2::byte *buffer, size_t count) override {
        count = std::min(count, remaining());
        size_t copied = 0;
        while (copied < count) {
            const auto offset = position_ - bufferStart_;
            if (offset < 0 || offset >= buffer_.size()) {
                if (!file_.seek(position_))
                    break;
                bufferStart_ = position_;
                // PNG has many short chunk headers separated by pixel data.
                // Nearby seeks stay in this bounded buffer instead of issuing
                // a new filesystem request for each tiny header.
                buffer_ = file_.read(std::min(qint64(64 * 1024), size_ - position_));
                if (buffer_.isEmpty())
                    break;
                continue;
            }
            const auto bytes = std::min(count - copied, size_t(buffer_.size() - offset));
            std::memcpy(buffer + copied, buffer_.constData() + offset, bytes);
            position_ += qint64(bytes);
            copied += bytes;
        }
        return copied;
    }
    int getb() override {
        Exiv2::byte byte;
        return read(&byte, 1) == 1 ? byte : EOF;
    }
    int seek(int64_t offset, Position position) override {
        const qint64 anchor = position == beg ? 0 : position == cur ? position_ : size_;
        if (offset < -anchor || offset > std::numeric_limits<qint64>::max() - anchor)
            return -1;
        position_ = anchor + offset;
        return 0;
    }
    Exiv2::byte *mmap(bool writable = false) override {
        if (writable)
            throw Exiv2::Error(Exiv2::ErrorCode::kerFailedToMapFileForReadWrite, path_);
        if (mapped_.isEmpty()) {
            if (!file_.seek(0))
                throw Exiv2::Error(Exiv2::ErrorCode::kerInputDataReadFailed);
            mapped_ = file_.read(size_);
            if (mapped_.size() != size_)
                throw Exiv2::Error(Exiv2::ErrorCode::kerInputDataReadFailed);
        }
        bigBlock_ = reinterpret_cast<Exiv2::byte *>(mapped_.data());
        return bigBlock_;
    }
    int munmap() override {
        bigBlock_ = nullptr;
        mapped_ = {};
        return 0;
    }
    size_t tell() const override {
        return size_t(position_);
    }
    size_t size() const override {
        return size_t(size_);
    }
    bool isopen() const override {
        return file_.isOpen();
    }
    int error() const override {
        return denied_ || file_.error() != QFile::NoError;
    }
    bool eof() const override {
        return position_ >= size_;
    }
    const std::string &path() const noexcept override {
        return path_;
    }
    void populateFakeData() override {}

  private:
    size_t remaining() const {
        return size_t(std::max(qint64(0), size_ - position_));
    }
    QFile &file_;
    qint64 size_;
    std::string path_;
    QByteArray mapped_, buffer_;
    qint64 position_ = 0, bufferStart_ = 0;
    bool denied_ = false;
};
#endif
// XMP initialization and namespace registration are shared Exiv2 state.
QMutex metadataMutex;
#endif
} // namespace
struct Metadata::Data {
#ifdef IX_HAVE_EXIV2
    Exiv2::ExifData exif;
    Exiv2::IptcData iptc;
    Exiv2::XmpData xmp;
#endif
};
bool metadataWritable(QByteArray format) {
    format = format.toLower();
    return format == "jpeg" || format == "jpg" || format == "png" || format == "tiff" || format == "tif" ||
           format == "webp";
}
std::shared_ptr<const Metadata> readMetadata(const QString &path, QString &warning) {
#ifdef IX_HAVE_EXIV2
    QMutexLocker metadataGuard(&metadataMutex);
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            warning = file.errorString();
            return {};
        }
        if (file.size() > 128LL * 1024 * 1024) {
            warning = QStringLiteral("Metadaten nicht eingelesen: Quelldatei größer als 128 MiB.");
            return {};
        }
#if EXIV2_TEST_VERSION(0, 28, 0)
        auto image = Exiv2::ImageFactory::open(std::make_unique<MetadataIo>(file));
#else
        // Retain compatibility with distributions still shipping Exiv2 0.27.
        auto bytes = file.readAll();
        auto image = Exiv2::ImageFactory::open(reinterpret_cast<const Exiv2::byte *>(bytes.constData()),
                                               size_t(bytes.size()));
#endif
        if (image.get() == nullptr)
            return {};
        image->readMetadata();
        auto md = std::make_shared<Metadata>();
        md->data = std::make_shared<Metadata::Data>();
        md->data->exif = image->exifData();
        md->data->iptc = image->iptcData();
        md->data->xmp = image->xmpData();
        md->hasExif = !md->data->exif.empty();
        md->hasIptc = !md->data->iptc.empty();
        md->hasXmp = !md->data->xmp.empty();
        auto collect = [&](const auto &container) {
            constexpr qint64 limit = 512LL * 1024 * 1024;
            for (const auto &tag : container) {
                const auto key = tag.key(), value = tag.toString();
                qint64 payload = std::min<qint64>(value.size(), limit);
                try {
                    payload = std::max(payload, std::clamp<qint64>(tag.size(), 0, limit));
                    if constexpr (requires { tag.sizeDataArea(); })
                        payload += std::clamp<qint64>(tag.sizeDataArea(), 0, limit);
                } catch (const Exiv2::Error &) {
                    // Accounting must not discard otherwise readable metadata.
                    payload = std::max(payload, std::min(file.size(), limit));
                }
                md->nativeBytes = std::min(limit, md->nativeBytes + payload +
                                                   std::min<qint64>(key.size(), limit) + 128);
                md->tags.insert(QString::fromStdString(key), QString::fromStdString(value));
            }
        };
        collect(md->data->exif);
        collect(md->data->iptc);
        collect(md->data->xmp);
        return md;
    } catch (const Exiv2::Error &e) {
        warning = QString::fromUtf8(e.what());
        return {};
    }
#else
    Q_UNUSED(path);
    warning = QStringLiteral("Exiv2 ist in diesem Build nicht verfügbar.");
    return {};
#endif
}
QByteArray attachMetadata(QByteArray encoded, const Metadata &md, QSize size, double dpiX, double dpiY,
                          bool modified, QString &error) {
#ifdef IX_HAVE_EXIV2
    QMutexLocker metadataGuard(&metadataMutex);
    try {
        if (!md.data)
            return encoded;
        auto image = Exiv2::ImageFactory::open(reinterpret_cast<const Exiv2::byte *>(encoded.constData()),
                                               size_t(encoded.size()));
        if (image.get() == nullptr) {
            error = QStringLiteral("Metadaten-Encoder unterstützt das Zielformat nicht.");
            return {};
        }
        image->readMetadata();
        auto exif = md.data->exif;
        auto xmp = md.data->xmp;
        if (!exif.empty()) {
            exif["Exif.Image.Orientation"] = uint16_t(1);
            exif["Exif.Photo.PixelXDimension"] = uint32_t(size.width());
            exif["Exif.Photo.PixelYDimension"] = uint32_t(size.height());
            if (dpiX > 0 && dpiY > 0) {
                exif["Exif.Image.XResolution"] = Exiv2::URational(uint32_t(dpiX * 1000), 1000);
                exif["Exif.Image.YResolution"] = Exiv2::URational(uint32_t(dpiY * 1000), 1000);
                exif["Exif.Image.ResolutionUnit"] = uint16_t(2);
            }
            if (modified)
                Exiv2::ExifThumb(exif).erase();
        }
        if (!xmp.empty()) {
            xmp["Xmp.tiff.Orientation"] = uint16_t(1);
            xmp["Xmp.exif.PixelXDimension"] = uint32_t(size.width());
            xmp["Xmp.exif.PixelYDimension"] = uint32_t(size.height());
        }
        image->setExifData(exif);
        image->setIptcData(md.data->iptc);
        image->setXmpData(xmp);
        image->writeMetadata();
        auto &io = image->io();
        io.open();
        auto bytes = io.read(io.size());
#if EXIV2_TEST_VERSION(0, 28, 0)
        return QByteArray(reinterpret_cast<const char *>(bytes.c_data()), qsizetype(bytes.size()));
#else
        return QByteArray(reinterpret_cast<const char *>(bytes.pData_), qsizetype(bytes.size_));
#endif
    } catch (const Exiv2::Error &e) {
        error = QStringLiteral("Metadaten konnten nicht erhalten werden: ") + QString::fromUtf8(e.what());
        return {};
    }
#else
    Q_UNUSED(encoded);
    Q_UNUSED(md);
    Q_UNUSED(size);
    Q_UNUSED(dpiX);
    Q_UNUSED(dpiY);
    Q_UNUSED(modified);
    error = QStringLiteral("Exiv2 ist nicht verfügbar.");
    return {};
#endif
}
} // namespace ix
