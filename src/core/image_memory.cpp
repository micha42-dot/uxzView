// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_memory.h"
#include <QColorSpace>
#include <algorithm>
namespace ix {
namespace {
constexpr qint64 Limit = ImageMemoryLimit + 1;
qint64 add(qint64 a, qint64 b) { return std::min(Limit, a + b); }
qint64 text(const QString &value) {
    return std::min<qint64>(std::max(value.size(), value.capacity()), ImageMemoryLimit / 2) * 2;
}
qint64 map(const QMap<QString, QString> &values) {
    qint64 result = 0;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        result = add(result, add(128, add(text(it.key()), text(it.value()))));
    return result;
}
}
qint64 imageMemoryCost(const QImage &image) {
    qint64 result = std::min<qint64>(image.sizeInBytes(), ImageMemoryLimit);
    for (const auto &key : image.textKeys())
        result = add(result, add(128, add(text(key), text(image.text(key)))));
    const auto icc = image.colorSpace().iccProfile();
    return add(result, std::min<qint64>(icc.size(), ImageMemoryLimit / 2) * 2);
}
qint64 metadataMemoryCost(const Metadata &metadata) {
    return add(map(metadata.tags), std::clamp<qint64>(metadata.nativeBytes, 0, ImageMemoryLimit));
}
qint64 documentPropertiesMemoryCost(const ImageDocument &document) {
    auto result = add(sizeof(ImageDocument), text(document.path));
    result = add(result, text(document.sourceRevision) * 2);
    result = add(result, text(document.metadataWarning));
    return add(result, map(document.text));
}
qint64 documentMemoryCost(const ImageDocument &document) {
    auto result = add(imageMemoryCost(document.pixels), documentPropertiesMemoryCost(document));
    return document.metadata ? add(result, metadataMemoryCost(*document.metadata)) : result;
}
}
