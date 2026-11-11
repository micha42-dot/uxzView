// SPDX-License-Identifier: GPL-3.0-or-later
#include "save_formats.h"
#include "codec_registry.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <algorithm>
namespace ix {
QByteArray canonicalSaveFormat(QByteArray format) {
    format = format.toLower();
    if (format == "jpg" || format == "jpe" || format == "jfif") return "jpeg";
    if (format == "tif") return "tiff";
    return format;
}
QString SaveFormat::suffix() const {
    return id == "jpeg" ? QStringLiteral("jpg") : extensions.value(0);
}
QString SaveFormat::filter() const {
    QStringList patterns;
    for (const auto &extension : extensions) patterns.append(QStringLiteral("*.") + extension);
    return label + QStringLiteral(" (") + patterns.join(QLatin1Char(' ')) + QLatin1Char(')');
}
QList<SaveFormat> writableSaveFormats(const CodecRegistry &registry) {
    // Dialog-open fast path: the registry's codec set is fixed at startup,
    // so capabilities are scanned once per registry instead of on every save.
    // A caller-side cache would go stale when plugins toggle formats; the
    // registry address is the stable key here (MainWindow owns one).
    // GUI-thread only (save/batch dialogs, settings): no locking needed.
    static QHash<const CodecRegistry *, QList<SaveFormat>> cache;
    if (const auto it = cache.constFind(&registry); it != cache.cend())
        return *it;
    QList<SaveFormat> result;
    for (const auto &cap : registry.capabilities()) {
        if (!cap.write || cap.extensions.isEmpty()) continue;
        const auto id = canonicalSaveFormat(cap.extensions.first().toLatin1());
        if (!registry.canWrite(id)) continue;
        if (std::any_of(result.cbegin(), result.cend(), [&](const auto &entry) { return entry.id == id; })) continue;
        result.append({id, cap.name, cap.extensions});
    }
    cache.insert(&registry, result);
    return result;
}
SaveTarget preferredSaveTarget(const QString &sourcePath, QByteArray sourceFormat,
                               const QList<SaveFormat> &formats, bool preferOriginal,
                               QByteArray fixedFormat) {
    if (formats.isEmpty()) return {};
    const auto source = canonicalSaveFormat(sourceFormat.isEmpty()
        ? QFileInfo(sourcePath).suffix().toLatin1() : sourceFormat);
    const auto wanted = preferOriginal ? source : canonicalSaveFormat(fixedFormat);
    auto selected = std::find_if(formats.cbegin(), formats.cend(), [&](const auto &f) { return f.id == wanted; });
    const bool unavailable = selected == formats.cend();
    if (unavailable) selected = std::find_if(formats.cbegin(), formats.cend(), [](const auto &f) { return f.id == "png"; });
    if (selected == formats.cend()) selected = formats.cbegin();
    const auto suffix = canonicalSaveFormat(QFileInfo(sourcePath).suffix().toLatin1());
    SaveTarget result{*selected, sourcePath, unavailable || sourcePath.isEmpty() || source != selected->id || suffix != selected->id};
    if (!sourcePath.isEmpty() && suffix != selected->id)
        result.suggestedPath = QFileInfo(sourcePath).dir().filePath(QFileInfo(sourcePath).completeBaseName() +
                                                                 QLatin1Char('.') + selected->suffix());
    return result;
}
SaveTarget selectedSaveTarget(QString path, const QString &filter, const QList<SaveFormat> &formats) {
    if (path.isEmpty()) return {};
    const auto suffix = canonicalSaveFormat(QFileInfo(path).suffix().toLatin1());
    auto selected = std::find_if(formats.cbegin(), formats.cend(), [&](const auto &f) { return f.id == suffix; });
    if (selected == formats.cend()) {
        selected = std::find_if(formats.cbegin(), formats.cend(), [&](const auto &f) { return f.filter() == filter; });
        if (selected == formats.cend()) return {};
        path += QLatin1Char('.') + selected->suffix();
    }
    return {*selected, path, false};
}
}
