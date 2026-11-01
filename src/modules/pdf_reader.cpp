// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/image_document.h"
#include <QColorSpace>
#include <QFileInfo>
#include <QPainter>
#include <QPdfDocument>
#include <QVariant>
#include <cmath>
#include <utility>

static ix::ImageResult readPdf(const QString &path, int page, QSize bounds = {}) {
    QPdfDocument pdf(nullptr); // Worker-owned; Qt serializes PDFium internally.
    const auto error = pdf.load(path);
    if (error == QPdfDocument::Error::IncorrectPassword || error == QPdfDocument::Error::UnsupportedSecurityScheme)
        return {{}, QStringLiteral("PDF ist kennwortgeschützt oder verwendet eine nicht unterstützte Verschlüsselung.")};
    if (error != QPdfDocument::Error::None || pdf.status() != QPdfDocument::Status::Ready)
        return {{}, QStringLiteral("PDF konnte nicht gelesen werden.")};
    if (page < 0 || page >= pdf.pageCount()) return {{}, QStringLiteral("Seite nicht vorhanden.")};
    const auto points = pdf.pagePointSize(page);
    const double width = std::ceil(points.width() * 2), height = std::ceil(points.height() * 2);
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 ||
        width > 100000 || height > 100000 || width * height > ix::ImageMemoryLimit / 8)
        return {{}, QStringLiteral("PDF-Seite überschreitet das Speicherlimit.")};
    auto size = QSize(int(width), int(height));
    if (bounds.isValid()) size.scale(bounds, Qt::KeepAspectRatio);
    auto image = pdf.render(page, size);
    if (image.isNull()) return {{}, QStringLiteral("PDF-Seite konnte nicht dargestellt werden.")};
    // PDF canvas is white. Fill behind transparent marks without another full raster.
    {
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOver);
        painter.fillRect(image.rect(), Qt::white);
    }
    image.setColorSpace(QColorSpace::SRgb);
    image.setDotsPerMeterX(5669); image.setDotsPerMeterY(5669); // 144 DPI
    const auto title = pdf.metaData(QPdfDocument::MetaDataField::Title).toString();
    const auto author = pdf.metaData(QPdfDocument::MetaDataField::Author).toString();
    if (!title.isEmpty()) image.setText(QStringLiteral("Title"), title);
    if (!author.isEmpty()) image.setText(QStringLiteral("Author"), author);
    ix::ImageDocument document;
    document.pixels = std::move(image);
    document.path = QFileInfo(path).absoluteFilePath();
    document.format = QByteArray("pdf");
    document.page = page; document.pageCount = pdf.pageCount();
    document.fileBytes = QFileInfo(path).size();
    for (const auto &key : document.pixels.textKeys()) document.text.insert(key, document.pixels.text(key));
    return {std::move(document), {}};
}

extern "C" Q_DECL_EXPORT ix::ImageResult ixReadPdf(const QString &path, int page) {
    return readPdf(path, page);
}
extern "C" Q_DECL_EXPORT QImage ixReadPdfPreview(const QString &path, QSize bounds) {
    if (!bounds.isValid() || bounds.width() > 2048 || bounds.height() > 2048) return {};
    return readPdf(path, 0, bounds).document.pixels;
}
