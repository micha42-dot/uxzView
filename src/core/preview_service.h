// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "codec_registry.h"
#include "file_revision.h"
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QThreadPool>
#include <atomic>
#include <functional>
#include <optional>
namespace ix {
QImage loadPreview(const QString &path, QSize bounds = {160, 120}, const std::atomic_bool *cancel = nullptr);
QImage loadPreview(const QString &path, QSize bounds, const std::atomic_bool *cancel, int priority);
bool isRawImage(const QString &path);
class PreviewService : public QObject {
    Q_OBJECT
  public:
    explicit PreviewService(QObject *parent = nullptr);
    ~PreviewService() override;
    void request(const QString &path, QSize bounds, int generation);
    void request(const QString &path, QSize bounds, int generation, int priority);
    void cancel();
  signals:
    void ready(QString path, QImage image, int generation);

  private:
    struct Request { QString path; QSize bounds; int generation, priority; };
    QQueue<Request> pending_;
    int running_ = 0;
    void startNext();
    QThreadPool pool_;
    std::shared_ptr<std::atomic_bool> cancelled_;
};
class ImageLoadCache {
  public:
    struct Usage {
        qint64 originals = 0, presentations = 0, decodedPreviews = 0, previews = 0, budget = 0;
        qint64 total() const { return originals + presentations + decodedPreviews + previews; }
    };
    struct PreviewPixels { QImage pixels; int delayMs = 20, loopCount = 0; };
    struct DecodeActivity { int active = 0, waiting = 0; };
    // Worker-only coordination of simultaneous, eligible native PNG/BMP full
    // decodes. No persistent result cache; failures retry with the caller's reader.
    static PreviewPixels decodePreviewPixels(const QString &path, QSize size, QImage::Format format,
        const QByteArray &codec, const std::function<PreviewPixels()> &decode,
        const std::atomic_bool *cancel = nullptr);
    static DecodeActivity decodeActivity();
    // Only unchanged, static, unrotated native PNG/BMP previews may hand off
    // full pixels. Metadata remains the foreground reader's responsibility.
    static std::optional<PreviewPixels> takePreviewPixels(const QString &path, QSize size,
                                                         QImage::Format format, const QByteArray &codec);
    // Called outside the cache lock only when decoding is actually needed.
    static ImageResult read(const CodecRegistry &registry, const QString &path, int page = 0,
                            const std::function<void()> &beforeDecode = {});
    // Cancellation before decoding avoids the read. A native read already in
    // progress may finish and populate the normal bounded original cache.
    static std::optional<ImageResult> read(const CodecRegistry &registry, const QString &path, int page,
                            const std::function<void()> &beforeDecode, const std::atomic_bool *cancel);
    // One presentation variant per unchanged cached original. Conversion runs
    // outside the lock; reuse checks original identity, policy and cache epoch.
    // Uses only spare original-cache capacity, never another independent budget.
    static QImage prepareDisplay(const QString &path, int page, const ImageDocument &document,
                                 bool manageColor, bool sharpen, const std::function<QImage()> &prepare);
    static Usage usage();
    // Lock-free estimate for worker admission; includes all cache kinds.
    static qint64 retainedBytes();
    static void clear();
    // Evict only these lexical paths, across all revisions/pages and previews.
    // Retain unrelated neighbors; older in-flight reads cannot reinsert evicted entries.
    static void invalidate(const QStringList &paths);
    static void configure(int imageMiB, int previewMiB, bool rawPreviews = true);
};
} // namespace ix
