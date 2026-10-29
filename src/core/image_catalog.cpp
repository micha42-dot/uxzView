// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_storage.h"
#include "image_catalog.h"
#include "file_operations.h"
#include "json_storage.h"
#include "metadata.h"
#include "preview_service.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QLockFile>
#include <QMutex>
#include <QSaveFile>
#include <algorithm>
#include <bit>
#include <cmath>
#include <vector>
namespace ix {
namespace {
QMutex catalogMutex;
QString key(const QString &path) {
    return QFileInfo(path).absoluteFilePath();
}
bool validColor(const QString &color) {
    return QStringList{QString(),
                       QStringLiteral("red"),
                       QStringLiteral("yellow"),
                       QStringLiteral("green"),
                       QStringLiteral("blue"),
                       QStringLiteral("purple")}
        .contains(color);
}
} // namespace
ImageCatalog::ImageCatalog(QString file)
    : file_(file.isEmpty() ? AppStorage::dataRoot() +
                                 QStringLiteral("/catalog.json")
                           : std::move(file)) {}
QJsonObject ImageCatalog::read() const {
    QFile file(file_);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024 * 1024)
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QString ImageCatalog::write(const QJsonObject &object) const {
    auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > 64 * 1024 * 1024)
        return QStringLiteral(
            "Bildkatalog hat seine Größenbegrenzung erreicht; vorhandene Daten bleiben erhalten.");
    QDir().mkpath(QFileInfo(file_).absolutePath());
    QSaveFile file(file_);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    if (file.write(bytes) != bytes.size() || !file.commit())
        return file.errorString();
    return {};
}
QJsonObject ImageCatalog::records() const {
    QMutexLocker guard(&catalogMutex);
    return read();
}
QJsonObject ImageCatalog::record(const QString &path) const {
    QMutexLocker guard(&catalogMutex);
    return read().value(key(path)).toObject();
}
QString ImageCatalog::setAnnotation(const QString &path, int rating, const QString &color) {
    return setAnnotations({path}, rating, color);
}
QString ImageCatalog::setAnnotations(const QStringList &paths, int rating, const QString &color) {
    if (rating < 0 || rating > 5 || !validColor(color))
        return QStringLiteral("Ungültige Bewertung oder Farbmarkierung.");
    QMutexLocker guard(&catalogMutex);
    QDir().mkpath(QFileInfo(file_).absolutePath());
    QLockFile lock(file_ + QStringLiteral(".lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Bildkatalog ist belegt.");
    auto storageError = jsonStorageError(file_, false, 256 * 1024 * 1024);
    if (!storageError.isEmpty())
        return storageError;
    auto object = read();
    for (const auto &path : paths) {
        auto r = object.value(key(path)).toObject();
        r.insert(QStringLiteral("rating"), rating);
        r.insert(QStringLiteral("color"), color);
        object.insert(key(path), r);
    }
    return write(object);
}
QString ImageCatalog::relocate(const QString &from, const QString &to) {
    QMutexLocker guard(&catalogMutex);
    if (!QFileInfo::exists(file_))
        return {};
    QLockFile lock(file_ + QStringLiteral(".lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Bildkatalog ist belegt.");
    auto storageError = jsonStorageError(file_, false, 256 * 1024 * 1024);
    if (!storageError.isEmpty())
        return storageError;
    auto object = read();
    auto value = object.take(key(from));
    if (value.isUndefined()) return {};
    object.insert(key(to), value);
    return write(object);
}
QStringList ImageCatalog::search(const QStringList &files, const QString &text, int minimum,
                                 const QString &color) const {
    QMutexLocker guard(&catalogMutex);
    const auto all = read();
    QStringList result;
    const auto terms = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const auto &path : files) {
        auto r = all.value(key(path)).toObject();
        if (r.value(QStringLiteral("rating")).toInt() < minimum ||
            (!color.isEmpty() && r.value(QStringLiteral("color")).toString() != color))
            continue;
        QString haystack = path + QLatin1Char(' ') + r.value(QStringLiteral("metadataText")).toString();
        bool match = true;
        for (const auto &term : terms)
            if (!haystack.contains(term, Qt::CaseInsensitive)) {
                match = false;
                break;
            }
        if (match)
            result.append(path);
    }
    return result;
}
QString ImageCatalog::index(const QStringList &files, const std::atomic_bool &cancel,
                            const std::function<void(int, int)> &progress) {
    QJsonObject updates;
    const auto snapshot = records();
    for (int i = 0; i < files.size(); ++i) {
        if (cancel.load())
            break;
        const auto &path = files[i];
        auto stamp = imageFileKey(path);
        auto r = snapshot.value(key(path)).toObject();
        if (r.value(QStringLiteral("stamp")).toString() != stamp) {
            QImageReader reader(path);
            auto size = reader.size();
            QStringList text{QFileInfo(path).fileName(), QString::fromLatin1(reader.format()),
                             QStringLiteral("%1x%2").arg(size.width()).arg(size.height())};
            QString warning;
            auto metadata = readMetadata(path, warning);
            if (metadata)
                for (auto it = metadata->tags.begin(); it != metadata->tags.end(); ++it)
                    text.append(it.key() + QLatin1Char('=') + it.value());
            QJsonObject data{{QStringLiteral("stamp"), stamp},
                             {QStringLiteral("metadataText"), text.join(QLatin1Char(' '))},
                             {QStringLiteral("width"), size.width()},
                             {QStringLiteral("height"), size.height()},
                             {QStringLiteral("warning"), warning}};
            updates.insert(key(path), data);
        }
        if (progress)
            progress(i + 1, files.size());
    }
    QMutexLocker guard(&catalogMutex);
    QDir().mkpath(QFileInfo(file_).absolutePath());
    QLockFile lock(file_ + QStringLiteral(".lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Bildkatalog ist belegt.");
    auto storageError = jsonStorageError(file_, false, 256 * 1024 * 1024);
    if (!storageError.isEmpty())
        return storageError;
    auto all = read();
    for (auto it = updates.begin(); it != updates.end(); ++it) {
        auto r = all.value(it.key()).toObject();
        auto data = it.value().toObject();
        for (auto p = data.begin(); p != data.end(); ++p)
            r.insert(p.key(), p.value());
        all.insert(it.key(), r);
    }
    return write(all);
}
quint64 ImageCatalog::differenceHash(const QImage &image) {
    if (image.isNull())
        return 0;
    auto reduced = image.scaled(9, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_Grayscale8);
    quint64 hash = 0;
    for (int y = 0; y < 8; ++y) {
        const auto *row = reduced.constScanLine(y);
        for (int x = 0; x < 8; ++x)
            hash = (hash << 1) | (row[x] > row[x + 1] ? 1ULL : 0ULL);
    }
    return hash;
}
DuplicateResult ImageCatalog::duplicates(const QStringList &files, bool perceptual, int threshold,
                                         const std::atomic_bool &cancel,
                                         const std::function<void(int, int)> &progress) {
    DuplicateResult result;
    threshold = std::clamp(threshold, 0, 16);
    QHash<QString, QString> exact;
    struct Fingerprint {
        QString file;
        QRgb mean;
        double ratio;
    };
    struct Node {
        quint64 hash;
        QList<Fingerprint> members;
        QMap<int, int> children;
    };
    std::vector<Node> tree;
    auto append = [&](const QString &first, const QString &second, int distance, bool identical) {
        if (result.pairs.size() >= 10000) {
            result.truncated = true;
            return;
        }
        result.pairs.append({first, second, distance, identical});
    };
    for (int i = 0; i < files.size(); ++i) {
        if (cancel.load())
            break;
        const auto &path = files[i];
        if (!perceptual) {
            auto hash = sha256File(path, &cancel);
            if (hash.isEmpty()) {
                if (!cancel.load())
                    result.errors.append(path);
                continue;
            }
            if (exact.contains(hash))
                append(exact.value(hash), path, 0, true);
            else
                exact.insert(hash, path);
        } else {
            auto preview = loadPreview(path, {144, 128}, &cancel);
            if (preview.isNull()) {
                if (!cancel.load())
                    result.errors.append(path);
                continue;
            }
            const auto hash = differenceHash(preview);
            const auto mean =
                preview.scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixel(0, 0);
            const double ratio = double(preview.width()) / preview.height();
            Fingerprint current{path, mean, ratio};
            if (tree.empty())
                tree.push_back({hash, {current}, {}});
            else {
                std::vector<int> stack{0};
                while (!stack.empty() && !cancel.load() && !result.truncated) {
                    const auto &node = tree[size_t(stack.back())];
                    stack.pop_back();
                    int distance = std::popcount(hash ^ node.hash);
                    if (distance <= threshold)
                        for (const auto &member : node.members) {
                            const int color = std::max({std::abs(qRed(mean) - qRed(member.mean)),
                                                        std::abs(qGreen(mean) - qGreen(member.mean)),
                                                        std::abs(qBlue(mean) - qBlue(member.mean))});
                            if (color <= 36 && std::abs(std::log(ratio / member.ratio)) < .08)
                                append(member.file, path, distance, false);
                            if (result.truncated)
                                break;
                        }
                    for (auto it = node.children.lowerBound(distance - threshold);
                         it != node.children.end() && it.key() <= distance + threshold; ++it)
                        stack.push_back(it.value());
                }
                int index = 0;
                while (true) {
                    int d = std::popcount(hash ^ tree[size_t(index)].hash);
                    if (!d) {
                        tree[size_t(index)].members.append(current);
                        break;
                    }
                    auto it = tree[size_t(index)].children.constFind(d);
                    if (it == tree[size_t(index)].children.cend()) {
                        tree[size_t(index)].children.insert(d, int(tree.size()));
                        tree.push_back({hash, {current}, {}});
                        break;
                    }
                    index = it.value();
                }
            }
        }
        if (progress)
            progress(i + 1, files.size());
        if (result.truncated)
            break;
    }
    return result;
}
} // namespace ix
