// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QRgba64>
#include <algorithm>
#include <array>
#include <cmath>

namespace ix::detail {
inline double resizeToLinear(double value) {
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}
inline double resizeToSrgb(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return value <= .0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - .055;
}

// Internal byte-output transfer. Locate the quantized result instead of
// evaluating a continuous power function whose precision is discarded next.
// Close to a byte boundary, use the original two-stage conversion. In
// particular, do not interpolate a gamma table or change tie rounding.
class ResizeByteTransfer {
  public:
    ResizeByteTransfer() {
        for (size_t i = 0; i < boundaries_.size(); ++i)
            boundaries_[i] = resizeToLinear((double(i) + .5) / 255);
        for (size_t i = 0; i < guesses_.size(); ++i)
            guesses_[i] = reference(double(i) / Buckets);
    }
    quint8 convert(double value) const {
        if (!(value > 0)) return 0;
        if (value >= 1) return 255;
        int result = guesses_[size_t(value * Buckets)];
        while (result < 255 && value >= boundaries_[size_t(result)]) ++result;
        while (result > 0 && value < boundaries_[size_t(result - 1)]) --result;
        // The analytic inverse and the forward library function may round
        // differently at a boundary. This margin is far larger than their
        // floating-point error, and leaves those decisions to the old path.
        constexpr double margin = 1e-10;
        if ((result > 0 && std::abs(value - boundaries_[size_t(result - 1)]) < margin) ||
            (result < 255 && std::abs(value - boundaries_[size_t(result)]) < margin))
            return reference(value);
        return quint8(result);
    }
  private:
    static quint8 reference(double value) {
        const auto word = quint16(std::clamp(std::lround(resizeToSrgb(value) * 65535), 0L, 65535L));
        return QRgba64::fromRgba64(word, 0, 0, 0).red8();
    }
    static constexpr int Buckets = 4096;
    std::array<double, 255> boundaries_;
    std::array<quint8, Buckets> guesses_;
};
} // namespace ix::detail
