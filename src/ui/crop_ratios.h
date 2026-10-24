// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QList>
#include <QRegularExpression>
#include <QSize>
#include <QStringList>
#include <numeric>
namespace ix {
inline QString cropRatioLabel(QSize ratio) {
    return QStringLiteral("%1:%2").arg(ratio.width()).arg(ratio.height());
}
inline QList<QSize> standardCropRatios() {
    return {{16, 9}, {9, 16}, {1, 1}, {4, 3}, {3, 2}, {21, 9}};
}
// Accept positive integer ratios, normalize duplicates, and bound settings input.
inline bool parseCropRatios(const QString &text, QStringList &result) {
    result.clear();
    if (text.size() > 4096)
        return false;
    const auto lines = text.split(QLatin1Char('\n'));
    const QRegularExpression syntax(QStringLiteral("^\\s*([0-9]{1,5})\\s*:\\s*([0-9]{1,5})\\s*$"));
    for (const auto &line : lines) {
        if (line.trimmed().isEmpty())
            continue;
        const auto match = syntax.match(line);
        if (!match.hasMatch())
            return false;
        int width = match.captured(1).toInt(), height = match.captured(2).toInt();
        if (width < 1 || height < 1 || width > 10000 || height > 10000)
            return false;
        const int divisor = std::gcd(width, height);
        auto value = cropRatioLabel({width / divisor, height / divisor});
        if (!result.contains(value))
            result.append(value);
        if (result.size() > 32)
            return false;
    }
    return true;
}
inline QSize cropRatioSize(const QString &label) {
    const auto parts = label.split(QLatin1Char(':'));
    return parts.size() == 2 ? QSize(parts[0].toInt(), parts[1].toInt()) : QSize();
}
} // namespace ix
