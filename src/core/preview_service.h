// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "codec_registry.h"
#include "file_revision.h"
#include <QObject>
#include <QQueue>
#include <QThreadPool>
#include <atomic>
#include <functional>
namespace ix {
QImage loadPreview(const QString &path, QSize bounds = {160, 120}, const std::atomic_bool *cancel = nullptr);
bool isRawImage(const QString &path);
class PreviewService : public QObject {
    Q_OBJECT
  public:
    explicit PreviewService(QObject *parent = nullptr);
    ~PreviewService() override;
    void request(const QString &path, QSize bounds, int generation);
    void cancel();
  signals:
    void ready(QString path, QImage image, int generation);

  private:
    struct Request { QString path; QSize bounds; int generation; };
    QQueue<Request> pending_;
    int running_ = 0;
    void startNext();
    QThreadPool pool_;
    std::shared_ptr<std::atomic_bool> cancelled_;
};
class ImageLoadCache {
  public:
    // Called outside the cache lock only when decoding is actually needed.
    static ImageResult read(const CodecRegistry &registry, const QString &path, int page = 0,
                            const std::function<void()> &beforeDecode = {});
    static void clear();
    static void configure(int imageMiB, int previewMiB, bool rawPreviews = true);
};
} // namespace ix
