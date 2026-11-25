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
#include <QMap>
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
QJsonObject ImageCatalog::read(QString *error) const {
    QJsonDocument document;
    const auto status = jsonStorageError(file_, false, 256 * 1024 * 1024, &document);
    if (error) *error = status;
    return document.object();
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
    QString storageError;
    auto object = read(&storageError);
    if (!storageError.isEmpty())
        return storageError;
    bool changed = false;
    for (const auto &path : paths) {
        auto r = object.value(key(path)).toObject();
        if (r.value(QStringLiteral("rating")) == QJsonValue(rating) &&
            r.value(QStringLiteral("color")) == QJsonValue(color)) continue;
        r.insert(QStringLiteral("rating"), rating);
        r.insert(QStringLiteral("color"), color);
        object.insert(key(path), r);
        changed = true;
    }
    return changed ? write(object) : QString();
}
QString ImageCatalog::relocate(const QString &from, const QString &to) {
    QMutexLocker guard(&catalogMutex);
    if (!QFileInfo::exists(file_))
        return {};
    QLockFile lock(file_ + QStringLiteral(".lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Bildkatalog ist belegt.");
    QString storageError;
    auto object = read(&storageError);
    if (!storageError.isEmpty())
        return storageError;
    auto value = object.take(key(from));
    if (value.isUndefined()) return {};
    object.insert(key(to), value);
    return write(object);
}
QString ImageCatalog::relocate(const QList<QPair<QString, QString>> &moves) {
    if (moves.isEmpty()) return {};
    if (moves.size() == 1) return relocate(moves.first().first, moves.first().second);
    QMutexLocker guard(&catalogMutex);
    if (!QFileInfo::exists(file_)) return {};
    QLockFile lock(file_ + QStringLiteral(".lock"));
    if (!lock.tryLock(5000)) return QStringLiteral("Bildkatalog ist belegt.");
    QString storageError;
    const auto snapshot = read(&storageError);
    if (!storageError.isEmpty()) return storageError;
    if (std::none_of(moves.cbegin(), moves.cend(), [&](const auto &move) {
        return snapshot.contains(key(move.first));
    })) return {};
    // Apply the ordered operations in a tree, not repeated sorted-JSON-array
    // inserts/removals. Reused names must not merge unrelated file identities.
    QMap<QString, QJsonValue> records;
    for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it) records.insert(it.key(), it.value());
    for (const auto &move : moves) {
        const auto from = records.find(key(move.first));
        if (from == records.end()) continue;
        auto value = from.value();
        records.erase(from);
        records.insert(key(move.second), std::move(value));
    }
    QJsonObject updated;
    for (auto it = records.cbegin(); it != records.cend(); ++it) updated.insert(it.key(), it.value());
    return write(updated);
}
QStringList ImageCatalog::search(const QStringList &files, const QString &text, int minimum,
                                 const QString &color) const {
    const auto all = records();
    QStringList result;
    const auto terms = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const auto &path : files) {
        if (matches(path, all.value(key(path)).toObject(), terms, minimum, color))
            result.append(path);
    }
    return result;
}
bool ImageCatalog::matches(const QString &path, const QJsonObject &record, const QStringList &terms,
                           int minimumRating, const QString &color) {
    if (record.value(QStringLiteral("rating")).toInt() < minimumRating ||
        (!color.isEmpty() && record.value(QStringLiteral("color")).toString() != color))
        return false;
    if (terms.isEmpty()) return true;
    const auto metadata = record.value(QStringLiteral("metadataText")).toString();
    return std::all_of(terms.cbegin(), terms.cend(), [&](const QString &term) {
        return path.contains(term, Qt::CaseInsensitive) || metadata.contains(term, Qt::CaseInsensitive);
    });
}
QString ImageCatalog::index(const QStringList &files, const std::atomic_bool &cancel,
                            const std::function<void(int, int)> &progress) {
    QJsonObject updates;
    QString storageError;
    QJsonObject snapshot;
    {
        QMutexLocker guard(&catalogMutex);
        snapshot = read(&storageError);
    }
    if (!storageError.isEmpty()) return storageError;
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
    if (updates.isEmpty()) return {};
    QMutexLocker guard(&catalogMutex);
    QDir().mkpath(QFileInfo(file_).absolutePath());
    QLockFile lock(file_ + QStringLiteral(".lock"));
    if (!lock.tryLock(5000))
        return QStringLiteral("Bildkatalog ist belegt.");
    auto all = read(&storageError);
    if (!storageError.isEmpty())
        return storageError;
    bool changed = false;
    for (auto it = updates.constBegin(); it != updates.constEnd(); ++it) {
        const auto data = it.value().toObject();
        if (imageFileKey(it.key()) != data.value(QStringLiteral("stamp")).toString()) continue;
        auto r = all.value(it.key()).toObject();
        for (auto p = data.begin(); p != data.end(); ++p)
            r.insert(p.key(), p.value());
        all.insert(it.key(), r);
        changed = true;
    }
    return changed ? write(all) : QString();
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
    QList<qint64> sizes;
    QHash<qint64, qsizetype> sizeCounts;
    if (!perceptual) {
        sizes.reserve(files.size());
        for (const auto &path : files) {
            if (cancel.load()) return result;
            const QFileInfo info(path);
            const qint64 size = info.isFile() ? info.size() : -1;
            sizes.append(size);
            if (size >= 0) ++sizeCounts[size];
        }
    }
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
            const auto size = sizes.at(i);
            if (size < 0) {
                result.errors.append(path);
            } else if (sizeCounts.value(size) == 1) {
                // No other candidate has this length. Still report inaccessible or changed files.
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly) || file.size() != size)
                    result.errors.append(path);
            } else {
                const auto hash = sha256File(path, &cancel);
                if (cancel.load()) break;
                if (hash.isEmpty() || QFileInfo(path).size() != size)
                    result.errors.append(path);
                else if (const auto it = exact.constFind(hash); it != exact.cend())
                    append(it.value(), path, 0, true);
                else
                    exact.insert(hash, path);
            }
        } else {
            auto preview = loadPreview(path, {144, 128}, &cancel);
            if (preview.isNull()) {
                if (cancel.load()) break;
                result.errors.append(path);
                if (progress) progress(i + 1, files.size());
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
