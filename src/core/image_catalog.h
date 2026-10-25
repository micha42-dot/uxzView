// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QJsonObject>
#include <QStringList>
#include <atomic>
#include <functional>
namespace ix {
struct DuplicatePair {
    QString first, second;
    int distance = 0;
    bool identical = false;
};
struct DuplicateResult {
    QList<DuplicatePair> pairs;
    QStringList errors;
    bool truncated = false;
};
class ImageCatalog {
  public:
    explicit ImageCatalog(QString file = {});
    QString path() const {
        return file_;
    }
    QJsonObject records() const;
    QJsonObject record(const QString &path) const;
    QString setAnnotation(const QString &path, int rating, const QString &color);
    QString setAnnotations(const QStringList &paths, int rating, const QString &color);
    QString relocate(const QString &from, const QString &to);
    QStringList search(const QStringList &files, const QString &text, int minimumRating = 0,
                       const QString &color = {}) const;
    QString index(const QStringList &files, const std::atomic_bool &cancel,
                  const std::function<void(int, int)> &progress = {});
    static DuplicateResult duplicates(const QStringList &files, bool perceptual, int threshold,
                                      const std::atomic_bool &cancel,
                                      const std::function<void(int, int)> &progress = {});
    static quint64 differenceHash(const QImage &image);

  private:
    QString file_;
    QJsonObject read() const;
    QString write(const QJsonObject &object) const;
};
} // namespace ix
