// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "rotation.h"
#include "processing.h"
#include <QPainter>
#include <QTransform>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace ix {
QRect maximalOriginalRect(QSize source, QSize result, double degrees) {
    if (!source.isValid() || !result.isValid() || !std::isfinite(degrees))
        return {};
    const double rad = std::remainder(degrees, 360.0) * std::numbers::pi / 180.0;
    const double c = std::abs(std::cos(rad)), s = std::abs(std::sin(rad));
    // A centered rectangle with half-sides a/b lies in the rotated source when
    // c*a+s*b <= source.width/2 and s*a+c*b <= source.height/2.
    // Enumerate integer widths (at most 100,000) and use the largest feasible
    // height. Include half-pixel offsets caused by odd output dimensions.
    double limit = result.width() / 2.0;
    if (c > 1e-12)
        limit = std::min(limit, source.width() / (2 * c));
    if (s > 1e-12)
        limit = std::min(limit, source.height() / (2 * s));
    QRect best;
    qint64 area = 0;
    for (int w = 1; w <= int(std::floor(2 * limit + 1e-8)); ++w) {
        int x = (result.width() - w) / 2;
        double a = std::max(result.width() / 2.0 - x, x + w - result.width() / 2.0);
        double b = result.height() / 2.0;
        if (s > 1e-12)
            b = std::min(b, (source.width() / 2.0 - c * a) / s);
        if (c > 1e-12)
            b = std::min(b, (source.height() / 2.0 - s * a) / c);
        int h = std::clamp(int(std::floor(2 * b + 1e-8)), 0, result.height());
        if (h > 0 && h + (result.height() - h) % 2 > 2 * b + 1e-8)
            --h;
        if (qint64(w) * h > area) {
            area = qint64(w) * h;
            best = {x, (result.height() - h) / 2, w, h};
        }
    }
    return best;
}
RotationResult rotateImage(const QImage &source, const RotationOptions &o) {
    if (source.isNull())
        throw std::runtime_error("Kein Bild geladen.");
    if (!std::isfinite(o.angle) || o.angle < -360 || o.angle > 360 || o.sharpenAmount < 1 ||
        o.sharpenAmount > 99)
        throw std::runtime_error("Ungültige Rotationsparameter.");
    if (std::abs(std::remainder(o.angle, 360.0)) < 1e-10) {
        QImage result = source;
        if (o.sharpen) {
            double strength = o.sharpenAmount / 20.0;
            result = convolve(result,
                              {0, -strength, 0, -strength, 1 + 4 * strength, -strength, 0, -strength, 0}, 3);
        }
        return {result, o.selectOriginal ? source.rect() : QRect()};
    }
    QTransform transform;
    transform.rotate(o.angle);
    QSize size =
        o.keepCanvas ? source.size() : transform.mapRect(QRectF(source.rect())).toAlignedRect().size();
    if (!safeSize(size, std::max(8, source.depth() / 8)) ||
        qint64(size.width()) * size.height() * std::max(8, source.depth() / 8) * 3 + source.sizeInBytes() >
            ImageMemoryLimit)
        throw std::runtime_error("Rotation überschreitet das Speicherlimit von 512 MiB.");
    const auto format = imageWorkingFormat(source, source.hasAlphaChannel());
    const auto input = source.convertToFormat(format);
    if (input.isNull())
        throw std::runtime_error("Nicht genügend Speicher für Rotation.");
    QImage result(size, format);
    if (result.isNull())
        throw std::runtime_error("Nicht genügend Speicher für Rotation.");
    result.fill(source.hasAlphaChannel() ? QColor(Qt::transparent) : o.background);
    QPainter painter(&result);
    if (o.keepCanvas && o.originalAsBackground)
        painter.drawImage(0, 0, input);
    if (o.fastMethod) {
        auto alphaFormat = imageWorkingFormat(source);
        if (alphaFormat == QImage::Format_ARGB32)
            alphaFormat = QImage::Format_ARGB32_Premultiplied;
        auto rotated =
            input.convertToFormat(alphaFormat)
                .transformed(transform, o.antialias ? Qt::SmoothTransformation : Qt::FastTransformation);
        if (rotated.isNull())
            throw std::runtime_error("Rotation fehlgeschlagen.");
        painter.drawImage(
            QPointF((size.width() - rotated.width()) / 2.0, (size.height() - rotated.height()) / 2.0),
            rotated);
    } else {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, o.antialias);
        painter.translate(size.width() / 2.0, size.height() / 2.0);
        painter.rotate(o.angle);
        painter.translate(-source.width() / 2.0, -source.height() / 2.0);
        painter.drawImage(0, 0, input);
    }
    painter.end();
    inheritImageProperties(source, result);
    if (o.sharpen) {
        double strength = o.sharpenAmount / 20.0;
        result =
            convolve(result, {0, -strength, 0, -strength, 1 + 4 * strength, -strength, 0, -strength, 0}, 3);
    }
    return {std::move(result),
            o.selectOriginal ? maximalOriginalRect(source.size(), size, o.angle) : QRect()};
}
} // namespace ix
