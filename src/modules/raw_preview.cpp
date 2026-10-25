// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QTransform>
#include <libraw/libraw.h>
extern "C" Q_DECL_EXPORT bool ixReadRawPreviewSized(const QString &path, QSize bounds, QImage &image) {
    if (!bounds.isValid() || bounds.width() > 2048 || bounds.height() > 2048)
        return false;
    LibRaw raw;
#ifdef Q_OS_WIN
    const int opened = raw.open_file(path.toStdWString().c_str());
#else
    const int opened = raw.open_file(QFile::encodeName(path).constData());
#endif
    if (opened != LIBRAW_SUCCESS || raw.unpack_thumb() != LIBRAW_SUCCESS)
        return false;
    const auto &thumb = raw.imgdata.thumbnail;
    if (thumb.tlength <= 0 || thumb.tlength > 32 * 1024 * 1024)
        return false;
    const bool rotated = raw.imgdata.sizes.flip == 5 || raw.imgdata.sizes.flip == 6;
    const auto decodeBounds = rotated ? bounds.transposed() : bounds;
    if (thumb.tformat == LIBRAW_THUMBNAIL_JPEG) {
        // Embedded JPEGs can be full-resolution. Let libjpeg downsample during
        // decoding instead of allocating/decompressing the full camera preview.
        auto bytes = QByteArray::fromRawData(thumb.thumb, thumb.tlength);
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "JPEG");
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 100000 || size.height() > 100000 ||
            qint64(size.width()) * size.height() > 512 * 1024 * 1024 / 8)
            return false;
        reader.setScaledSize(size.scaled(decodeBounds, Qt::KeepAspectRatio));
        image = reader.read();
    }
    else if (thumb.tformat == LIBRAW_THUMBNAIL_BITMAP && thumb.tcolors == 3 && thumb.twidth > 0 &&
             thumb.theight > 0 && quint64(thumb.twidth) * thumb.theight * 3 <= thumb.tlength)
        image = QImage(reinterpret_cast<const uchar *>(thumb.thumb), thumb.twidth, thumb.theight,
                       thumb.twidth * 3, QImage::Format_RGB888)
                    .scaled(decodeBounds, Qt::KeepAspectRatio, Qt::SmoothTransformation).copy();
    if (image.isNull())
        return false;
    QTransform orientation;
    switch (raw.imgdata.sizes.flip) {
    case 3:
        orientation.rotate(180);
        break;
    case 5:
        orientation.rotate(-90);
        break;
    case 6:
        orientation.rotate(90);
        break;
    default:
        break;
    }
    if (!orientation.isIdentity())
        image = image.transformed(orientation);
    return true;
}
