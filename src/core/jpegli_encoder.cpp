// SPDX-License-Identifier: GPL-3.0-or-later
#include "jpegli_encoder.h"
#include "image_storage.h"
#include "localization.h"
#include <QColorSpace>
#include <QIODevice>
#include <lib/jpegli/encode.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace ix {
namespace {
void failed(j_common_ptr state) {
    char message[JMSG_LENGTH_MAX]{};
    state->err->format_message(state, message);
    throw std::runtime_error(message);
}
struct Encoder {
    jpeg_compress_struct state{};
    jpeg_error_mgr error{};
    ~Encoder() { if (state.mem) jpegli_destroy_compress(&state); }
};
struct Destination : jpeg_destination_mgr {
    QIODevice &device;
    std::array<JOCTET, 65536> bytes{};
    explicit Destination(QIODevice &output) : device(output) {
        init_destination = [](j_compress_ptr info) {
            auto &self = *static_cast<Destination *>(info->dest);
            self.next_output_byte = self.bytes.data();
            self.free_in_buffer = self.bytes.size();
        };
        empty_output_buffer = [](j_compress_ptr info) -> boolean {
            auto &self = *static_cast<Destination *>(info->dest);
            self.write(self.bytes.size());
            self.next_output_byte = self.bytes.data();
            self.free_in_buffer = self.bytes.size();
            return TRUE;
        };
        term_destination = [](j_compress_ptr info) {
            auto &self = *static_cast<Destination *>(info->dest);
            self.write(self.bytes.size() - self.free_in_buffer);
        };
    }
    void write(qint64 count) {
        qint64 offset = 0;
        while (offset < count) {
            const auto n = device.write(reinterpret_cast<const char *>(bytes.data()) + offset, count - offset);
            if (n <= 0) throw std::runtime_error(device.errorString().toUtf8().constData());
            offset += n;
        }
    }
};
}
qint64 jpegliWorkingBytes(int width) {
    return ((qint64(std::max(0, width)) + 15) / 16 * 16) * 4096 + 2 * MiB;
}
QString writeJpegli(QIODevice &destination, const QImage &image, int quality, qint64 byteBudget) {
    return writeJpegli(destination, image, quality, JpegliOptions{}, byteBudget);
}
QString writeJpegli(QIODevice &destination, const QImage &image, int quality,
                    const JpegliOptions &options, qint64 byteBudget) {
    // Sequential, non-optimized coding keeps jpegli's MCU-row streaming path.
    // Allow its float row planes, adaptation data, conversion row and IO buffer.
    const qint64 working = jpegliWorkingBytes(image.width());
    const qint64 resident = imageIsFileBacked(image) ? 0 : image.sizeInBytes();
    const qint64 limit = byteBudget > 0 ? std::min(byteBudget, imageMemoryLimit()) : imageMemoryLimit();
    if (working > limit || resident > limit - working)
        return IX_TEXT("Für den JPEG-Export reicht das Bildspeicherbudget nicht aus.");
    try {
        Destination output(destination); // Must outlive native compressor cleanup.
        Encoder encoder;
        auto &state = encoder.state;
        state.err = jpegli_std_error(&encoder.error);
        encoder.error.error_exit = failed;
        jpegli_create_compress(&state);
        state.dest = &output;
        state.image_width = image.width(); state.image_height = image.height();
        const bool cmyk = image.format() == QImage::Format_CMYK8888;
        const bool gray = (image.format() == QImage::Format_Grayscale8 || image.format() == QImage::Format_Grayscale16) &&
            (!image.colorSpace().isValid() || image.colorSpace().colorModel() == QColorSpace::ColorModel::Gray);
        state.input_components = cmyk ? 4 : gray ? 1 : 3;
        state.in_color_space = cmyk ? JCS_CMYK : gray ? JCS_GRAYSCALE : JCS_RGB;
        jpegli_set_defaults(&state);
        jpegli_set_quality(&state, quality < 0 ? 95 : std::clamp(quality, 0, 100), TRUE);
        // Progressive and 4:4:4 sampling are real encoder switches (not just
        // stored UI flags): they change the produced bitstream. Both must be
        // set after jpegli_set_defaults (which resets them); level 0 pins
        // sequential mode, level 2 selects the default progression.
        jpegli_set_progressive_level(&state, options.progressive ? 2 : 0);
        if (options.noSubsampling && state.num_components == 3) {
            for (int i = 0; i < 3; ++i) {
                state.comp_info[i].h_samp_factor = 1;
                state.comp_info[i].v_samp_factor = 1;
            }
        }
        state.optimize_coding = FALSE;
        state.write_JFIF_header = !cmyk; // JFIF defines grayscale/YCbCr, not CMYK.
        if (cmyk) state.write_Adobe_marker = TRUE;
        const bool hasDensity = image.dotsPerMeterX() > 0 && image.dotsPerMeterY() > 0;
        state.density_unit = hasDensity ? 1 : 0;
        state.X_density = hasDensity ? quint16(std::clamp(qRound(image.dotsPerMeterX() * .0254), 1, 65535)) : 1;
        state.Y_density = hasDensity ? quint16(std::clamp(qRound(image.dotsPerMeterY() * .0254), 1, 65535)) : 1;
        jpegli_start_compress(&state, TRUE);
        const bool convertColor = !cmyk && !gray && image.colorSpace().isValid() &&
            image.colorSpace().colorModel() != QColorSpace::ColorModel::Rgb;
        const auto colorSpace = convertColor ? QColorSpace(QColorSpace::SRgb) : image.colorSpace();
        const auto icc = colorSpace.iccProfile();
        if (icc.size() > 255 * 65519) return IX_TEXT("ICC-Farbprofil ist zu groß.");
        if (!icc.isEmpty()) jpegli_write_icc_profile(&state,
            reinterpret_cast<const JOCTET *>(icc.constData()), unsigned(icc.size()));
        for (const auto &key : image.textKeys()) {
            const auto comment = (key + QStringLiteral(": ") + image.text(key)).toUtf8();
            // JPEG COM markers have a 16-bit length. Truncating bytes here can
            // both discard metadata and split a UTF-8 character. Fail while
            // the caller still owns its uncommitted QSaveFile instead.
            if (comment.size() > 65533)
                return IX_TEXT("Bildkommentar ist für JPEG zu lang. Bitte PNG oder TIFF verwenden.");
            jpegli_write_marker(&state, JPEG_COM, reinterpret_cast<const JOCTET *>(comment.constData()), unsigned(comment.size()));
        }
        QImage whiteRow;
        // White-flatten in the destination layout directly: a single
        // RGB888 conversion per row instead of convert-to-premultiplied +
        // manual unpremultiply. Same pixels, one pass, no QRgb temporaries.
        if (image.hasAlphaChannel() && options.flattenAlpha) {
            whiteRow = QImage(image.width(), 1, QImage::Format_RGB888);
            if (whiteRow.isNull()) throw std::bad_alloc();
        }
        QImage cmykRow;
        if (cmyk) {
            cmykRow = QImage(image.width(),1,QImage::Format_CMYK8888);
            if (cmykRow.isNull()) throw std::bad_alloc();
        }
        for (int y = 0; y < image.height(); ++y) {
            QImage row(image.constScanLine(y), image.width(), 1, image.bytesPerLine(), image.format());
            row.setColorTable(image.colorTable());
            if (cmyk) {
                // Match the inverted CMYK convention used by Qt/Adobe readers.
                auto *target = cmykRow.bits(); const auto *source = row.constBits();
                for (qsizetype x=0;x<qsizetype(image.width())*4;++x) target[x]=uchar(255-source[x]);
                row = cmykRow;
            } else if (gray) row = row.convertToFormat(QImage::Format_Grayscale8);
            else if (convertColor) {
                row.setColorSpace(image.colorSpace());
                row = row.convertedToColorSpace(colorSpace, QImage::Format_RGB888);
            } else if (image.hasAlphaChannel() && options.flattenAlpha) {
                const auto premultiplied = row.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                if (premultiplied.isNull()) throw std::bad_alloc();
                const auto *source = reinterpret_cast<const QRgb *>(premultiplied.constBits());
                auto *target = whiteRow.bits();
                for (int x = 0; x < image.width(); ++x) {
                    const auto p = source[x]; const int white = 255 - qAlpha(p);
                    target[x*3] = uchar(qRed(p) + white);
                    target[x*3+1] = uchar(qGreen(p) + white);
                    target[x*3+2] = uchar(qBlue(p) + white);
                }
                row = whiteRow;
            } else row = row.convertToFormat(QImage::Format_RGB888);
            if (row.isNull()) throw std::bad_alloc();
            auto *pixels = const_cast<JSAMPROW>(reinterpret_cast<const JSAMPLE *>(row.constBits()));
            if (jpegli_write_scanlines(&state, &pixels, 1) != 1)
                throw std::runtime_error("jpegli: incomplete scanline");
        }
        jpegli_finish_compress(&state);
        return {};
    } catch (const std::exception &error) {
        return QStringLiteral("jpegli: ") + localized(QString::fromUtf8(error.what()));
    }
}
}
