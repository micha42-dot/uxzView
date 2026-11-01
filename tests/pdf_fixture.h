// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFile>
#include <QList>
namespace ix::test {
// Small self-authored vector PDF; page sizes deliberately differ.
inline bool writePdfFixture(const QString &path, int width = 80, int height = 60) {
    const QByteArray media = QByteArray::number(width) + ' ' + QByteArray::number(height);
    const QByteArray red = "1 0 0 rg 0 0 " + media + " re f\n";
    const QByteArray blue = "0 0 1 rg 10 10 40 80 re f\n";
    const QList<QByteArray> objects{
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + media + "] /Resources << >> /Contents 5 0 R >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 60 100] /Resources << >> /Contents 6 0 R >>",
        "<< /Length " + QByteArray::number(red.size()) + " >>\nstream\n" + red + "endstream",
        "<< /Length " + QByteArray::number(blue.size()) + " >>\nstream\n" + blue + "endstream",
        "<< /Title (uxzView PDF fixture) /Author (Test fixture) >>"};
    QByteArray bytes("%PDF-1.4\n");
    QList<qsizetype> offsets{0};
    for (const auto &object : objects) {
        offsets.append(bytes.size());
        bytes += QByteArray::number(offsets.size()-1) + " 0 obj\n" + object + "\nendobj\n";
    }
    const auto xref = bytes.size();
    bytes += "xref\n0 8\n0000000000 65535 f \n";
    for (qsizetype i=1;i<offsets.size();++i)
        bytes += QByteArray::number(offsets[i]).rightJustified(10,'0') + " 00000 n \n";
    bytes += "trailer\n<< /Size 8 /Root 1 0 R /Info 7 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace ix::test
