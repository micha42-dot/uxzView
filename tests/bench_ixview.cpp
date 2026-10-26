// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation_fixture.h"
#include "core/apng_codec.h"
#include "core/codec_registry.h"
#include "core/directory_index.h"
#include "core/image_loading.h"
#include "core/metadata.h"
#include "core/preview_service.h"
#include "ui/image_view.h"
#include "ui/main_window.h"
#include "ui/platform_support.h"
#include "ui/preview_strip.h"
#include "ui/slideshow_controller.h"
#include <QElapsedTimer>
#include <QColorSpace>
#include <QCollator>
#include <QDirListing>
#include <QImageReader>
#include <QLibrary>
#include <QJsonDocument>
#include <QtTest>
#include <QtWidgets>
#include <vector>
namespace {
QJsonObject profilePreviewReuse() {
    QTemporaryDir temporary(QDir::current().filePath(QStringLiteral("build/preview-reuse-XXXXXX")));
    if (!temporary.isValid())
        return {{QStringLiteral("error"), QStringLiteral("Cannot create preview fixtures")}};
    QImage pixels(4000, 3000, QImage::Format_RGB32);
    pixels.setColorSpace(QColorSpace::SRgb);
    for (int y = 0; y < pixels.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(pixels.scanLine(y));
        for (int x = 0; x < pixels.width(); ++x)
            row[x] = qRgb((x * 7 + y) % 256, (x + y * 3) % 256, (x * 2 + y * 11) % 256);
    }
    QJsonArray cases;
    ix::CodecRegistry registry;
    const auto restore = qScopeGuard([] {
        ix::ImageLoadCache::clear();
        ix::ImageLoadCache::configure(192, 16, true);
    });
    for (const auto &format : {QStringLiteral("png"), QStringLiteral("bmp")}) {
        const auto path = temporary.filePath(QStringLiteral("fixture.") + format);
        if (!pixels.save(path))
            return {{QStringLiteral("error"), QStringLiteral("Cannot save preview fixture")}};
        for (const QSize bounds : {QSize(68, 50), QSize(512, 512)}) {
            QJsonArray decoder, borrowed, decoderPrime, borrowedPrime;
            for (int sample = 0; sample < 5; ++sample) {
                for (int arm = 0; arm < 2; ++arm) {
                    const bool reuse = (sample + arm) % 2;
                    ix::ImageLoadCache::clear();
                    ix::ImageLoadCache::configure(reuse ? 192 : 0, 0, true);
                    QElapsedTimer timer;
                    timer.start();
                    const auto original = ix::ImageLoadCache::read(registry, path);
                    const auto prime = timer.nsecsElapsed() / 1e6;
                    if (!original.ok())
                        return {{QStringLiteral("error"), original.error}};
                    const auto key = original.document.pixels.cacheKey();
                    const auto expected = original.document.pixels.scaled(bounds, Qt::KeepAspectRatio,
                                                                         Qt::SmoothTransformation);
                    timer.restart();
                    const auto preview = ix::loadPreview(path, bounds);
                    const auto elapsed = timer.nsecsElapsed() / 1e6;
                    if (preview.isNull() || preview != expected || original.document.pixels.cacheKey() != key)
                        return {{QStringLiteral("error"), QStringLiteral("Preview pixels or original changed")}};
                    (reuse ? borrowed : decoder).append(elapsed);
                    (reuse ? borrowedPrime : decoderPrime).append(prime);
                }
            }
            cases.append(QJsonObject{{QStringLiteral("format"), format},
                                     {QStringLiteral("bounds"), QStringLiteral("%1x%2").arg(bounds.width()).arg(bounds.height())},
                                     {QStringLiteral("decoder_ms"), decoder},
                                     {QStringLiteral("borrowed_ms"), borrowed},
                                     {QStringLiteral("decoder_prime_ms"), decoderPrime},
                                     {QStringLiteral("borrowed_prime_ms"), borrowedPrime}});
        }
    }
    return {{QStringLiteral("cases"), cases}, {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
            {QStringLiteral("note"), QStringLiteral("Warm filesystem, 12 MP PNG/BMP, original decoded before every timed preview. Alternating order, preview cache disabled, exact pixel comparison. Priming cost reported separately; not first-open or end-to-end viewer latency.")}};
}
class FirstImagePaint final : public QObject {
  public:
    std::function<void()> completed;
    bool pending = false;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint && pending) {
            pending = false;
            QTimer::singleShot(0, this, [this] { if (completed) completed(); });
        }
        return false;
    }
};
QJsonObject profileFirstOpen(const QString &path) {
    qInfo("ix-first-open profile-start");
    QTemporaryDir profile(QDir::current().filePath(QStringLiteral("build/first-open-XXXXXX")));
    if (!profile.isValid() || !QFileInfo(path).isFile())
        return {{QStringLiteral("error"), QStringLiteral("Missing image or temporary profile")}};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    qApp->setProperty("ixStorageData", profile.path());
    qInfo("ix-first-open create-window");
    ix::MainWindow window;
    window.resize(1200, 800);
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    qInfo("ix-first-open show-window");
    window.show();
    QTest::qWait(200); // The window is already running before the measured open.
    qInfo("ix-first-open window-ready exposed=%d", window.windowHandle() && window.windowHandle()->isExposed());
    auto *view = window.findChild<ix::ImageView *>();
    FirstImagePaint observer;
    view->viewport()->installEventFilter(&observer);
    QJsonArray samples;
    for (int pass = 0; pass < 3; ++pass) {
        qInfo("ix-first-open pass %d start", pass);
        ix::ImageLoadCache::clear(); // Repeat decoding, not just cache retrieval.
        QEventLoop loop;
        QTimer heartbeat, deadline;
        QElapsedTimer elapsed;
        double installed = -1, painted = -1, previousBeat = 0, maxGap = 0;
        auto ready = QObject::connect(view, &ix::ImageView::selectionChanged, &loop, [&] {
            if (window.document().path == path && !window.document().empty() && installed < 0) {
                installed = elapsed.nsecsElapsed() / 1e6;
                observer.pending = true;
            }
        });
        observer.completed = [&] {
            painted = elapsed.nsecsElapsed() / 1e6;
            loop.quit();
        };
        QObject::connect(&heartbeat, &QTimer::timeout, &loop, [&] {
            const double now = elapsed.nsecsElapsed() / 1e6;
            maxGap = std::max(maxGap, now - previousBeat);
            previousBeat = now;
        });
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        elapsed.start();
        heartbeat.start(1);
        deadline.start(30000);
        window.openPath(path);
        const double submitted = elapsed.nsecsElapsed() / 1e6;
        loop.exec();
        observer.completed = {};
        QObject::disconnect(ready);
        if (painted < 0)
            return {{QStringLiteral("error"), QStringLiteral("Image was not painted within 30 seconds")}};
        qInfo("ix-first-open pass %d painted", pass);
        samples.append(QJsonObject{{QStringLiteral("open_return_ms"), submitted},
            {QStringLiteral("document_install_ms"), installed},
            {QStringLiteral("paint_completed_ms"), painted},
            {QStringLiteral("max_gui_heartbeat_gap_ms"), maxGap}});
        QTest::qWait(50);
    }
    qInfo("ix-first-open destroy-window");
    return {{QStringLiteral("samples"), samples},
        {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("note"), QStringLiteral("Already-running empty window, isolated default profile, three opens of the same image with document caches cleared. First sample includes lazy initialization. Paint completion is an event-loop observation, not a compositor/display measurement. OS file caches are not cleared.")}};
}
QJsonObject profileRawCache() {
    const auto folder = qEnvironmentVariable("IX_RAW_FIXTURES",
        QCoreApplication::applicationDirPath() + QStringLiteral("/quality-fixtures"));
    const auto path = QDir(folder).filePath(QStringLiteral("pentax-k10d.dng"));
    if (!QFileInfo::exists(path)) return {{QStringLiteral("error"), QStringLiteral("DNG fixture missing")}};
    ix::CodecRegistry registry;
    QJsonArray rows;
    const auto reset = qScopeGuard([] { ix::ImageLoadCache::clear(); ix::ImageLoadCache::configure(192, 16, true); });
    for (int previewMiB : {16, 0}) {
        ix::ImageLoadCache::clear();
        ix::ImageLoadCache::configure(192, previewMiB, true);
        const ix::ImageLoadRequest request{path, 0, true, false, true};
        const auto prime = ix::loadImage(registry, request);
        if (!prime.error.isEmpty() || prime.document.empty()) return {{QStringLiteral("error"), prime.error}};
        if (ix::loadPreview(path, {1600, 1100}).isNull())
            return {{QStringLiteral("error"), QStringLiteral("Embedded RAW preview unavailable")}};
        QJsonArray before, after;
        int oldPreviews = 0, newPreviews = 0;
        for (int sample = 0; sample < 5; ++sample) {
            for (bool old : {sample % 2 == 0, sample % 2 != 0}) {
                QElapsedTimer timer;
                timer.start();
                auto chosen = request;
                if (old) {
                    if (!ix::loadPreview(path, {1600, 1100}).isNull()) ++oldPreviews;
                    chosen.rawPreview = false;
                }
                const auto result = ix::loadImage(registry, chosen, [&](QImage) { ++newPreviews; });
                (old ? before : after).append(timer.nsecsElapsed() / 1e6);
                if (!result.error.isEmpty() || result.document.pixels.cacheKey() != prime.document.pixels.cacheKey()
                    || result.display != prime.display)
                    return {{QStringLiteral("error"), QStringLiteral("Cached RAW pixels/display changed")}};
            }
        }
        rows.append(QJsonObject{{QStringLiteral("preview_cache_MiB"), previewMiB},
            {QStringLiteral("before_ms"), before}, {QStringLiteral("after_ms"), after},
            {QStringLiteral("before_preview_deliveries"), oldPreviews}, {QStringLiteral("after_preview_deliveries"), newPreviews}});
    }
    return {{QStringLiteral("samples"), rows}, {QStringLiteral("note"), QStringLiteral(
        "Five alternating samples per preview-cache budget; one cached Pentax DNG, warm filesystem, color management enabled. "
        "Reference always requests the preview before the cached original; current pipeline requests it only on a miss. "
        "Includes display conversion, excludes GUI painting. No claim for first RAW development, uncached originals or all cameras.")}};
}
QJsonObject profileSlideshow() {
    QStringList files;
    for (int i = 0; i < 10000; ++i) files.append(QString::number(i));
    QJsonArray before, after;
    for (int repeat = 0; repeat < 5; ++repeat) {
        for (bool old : {repeat % 2 == 0, repeat % 2 != 0}) {
            ix::SlideshowController controller;
            auto remaining = files;
            remaining.removeFirst();
            QStringList selected;
            QElapsedTimer timer;
            timer.start();
            for (int step = 0; step < 1000; ++step) {
                if (old) {
                    const QSet<QString> available(files.begin(), files.end());
                    remaining.removeIf([&](const QString &path) { return !available.contains(path); });
                    selected.append(remaining.takeAt(QRandomGenerator::global()->bounded(int(remaining.size()))));
                } else selected.append(controller.next(files, 0, true, false).path);
            }
            (old ? before : after).append(timer.nsecsElapsed() / 1e6);
            const QSet<QString> unique(selected.begin(), selected.end());
            if (unique.size() != 1000 || unique.contains(files.first()) || unique.contains(QString()))
                return {{QStringLiteral("error"), QStringLiteral("Invalid slideshow round")}};
        }
    }
    return {{QStringLiteral("before_ms"), before}, {QStringLiteral("after_ms"), after},
        {QStringLiteral("note"), QStringLiteral("Five alternating in-process samples; 1000 selections from 10000 unchanged paths. Previous QSet/filter/random-removal algorithm versus current controller. No file I/O, decoding or rendering; not a whole-viewer speedup.")}};
}
QJsonObject profileRawPreviews() {
    // Optional baseline DLL/so is a local measurement artifact, never shipped.
    QLibrary baseline(qEnvironmentVariable("IX_RAW_BASELINE_MODULE"));
    using Read = bool (*)(const QString &, QImage &);
    const auto oldRead = reinterpret_cast<Read>(baseline.resolve("ixReadRawPreview"));
    const auto folder = qEnvironmentVariable("IX_RAW_FIXTURES",
        QCoreApplication::applicationDirPath() + QStringLiteral("/quality-fixtures"));
    QJsonArray rows;
    for (const auto &file : QDir(folder).entryInfoList(QDir::Files)) {
        if (!ix::isRawImage(file.fileName())) continue;
        for (const QSize bounds : {QSize(68, 50), QSize(512, 512)}) {
            QJsonArray oldTimes, newTimes;
            QSize oldSize, newSize;
            for (int repeat = 0; repeat < 7; ++repeat) {
                for (bool old : {repeat % 2 == 0, repeat % 2 != 0}) {
                    if (old && !oldRead) continue;
                    ix::ImageLoadCache::clear();
                    QElapsedTimer timer; timer.start();
                    QImage image;
                    if (old) {
                        oldRead(file.absoluteFilePath(), image);
                        image = image.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    } else image = ix::loadPreview(file.absoluteFilePath(), bounds);
                    (old ? oldTimes : newTimes).append(timer.nsecsElapsed() / 1e6);
                    if (image.isNull()) return {{QStringLiteral("error"), file.fileName()}};
                    (old ? oldSize : newSize) = image.size();
                }
            }
            if (oldRead && oldSize != newSize)
                return {{QStringLiteral("error"), QStringLiteral("Preview dimensions changed: ") + file.fileName()}};
            rows.append(QJsonObject{{QStringLiteral("file"), file.fileName()},
                {QStringLiteral("bounds"), QStringLiteral("%1x%2").arg(bounds.width()).arg(bounds.height())},
                {QStringLiteral("before_ms"), oldTimes}, {QStringLiteral("after_ms"), newTimes}});
        }
    }
    return {{QStringLiteral("samples"), rows}, {QStringLiteral("note"),
        QStringLiteral("Seven alternating calls, warm filesystem, empty thumbnail cache. Measures embedded previews only, including RAW parsing; optional baseline uses the previously built module. Not full RAW development.")}};
}
QJsonObject profileAnimations(const QString &root, const ix::CodecRegistry &registry) {
    QJsonArray rows;
    for (int budget : {0, 16, 192}) {
        ix::ImageLoadCache::configure(budget, 16, true);
        for (int count : {20, 40, 80}) {
            const auto path = root + QStringLiteral("/animation-%1.png").arg(count);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return {{QStringLiteral("error"), file.errorString()}};
            file.write(ix::test::apngFixture(count));
            file.close();
            QJsonArray sequential, stateless, sequentialCompositions, statelessCompositions;
            QJsonArray sequentialParses, statelessParses;
            for (int repeat = 0; repeat < 3; ++repeat)
                for (bool reset : {repeat % 2 == 0, repeat % 2 != 0}) {
                    ix::ImageLoadCache::clear();
                    const auto before = ix::ApngCodec::cacheStats();
                    QElapsedTimer timer;
                    timer.start();
                    for (int page = 0; page < count; ++page) {
                        if (reset)
                            ix::ApngCodec::clearSession();
                        const auto frame = ix::ImageLoadCache::read(registry, path, page);
                        if (!frame.ok())
                            return {{QStringLiteral("error"), frame.error}};
                    }
                    const auto elapsed = timer.nsecsElapsed() / 1e6;
                    const auto after = ix::ApngCodec::cacheStats();
                    (reset ? stateless : sequential).append(elapsed);
                    (reset ? statelessCompositions : sequentialCompositions)
                        .append(double(after.framesComposited - before.framesComposited));
                    (reset ? statelessParses : sequentialParses)
                        .append(double(after.filesParsed - before.filesParsed));
                }
            rows.append(QJsonObject{{QStringLiteral("frames"), count},
                {QStringLiteral("frame_cache_mib"), budget},
                {QStringLiteral("session_ms"), sequential}, {QStringLiteral("stateless_ms"), stateless},
                {QStringLiteral("session_compositions"), sequentialCompositions},
                {QStringLiteral("stateless_compositions"), statelessCompositions},
                {QStringLiteral("session_parses"), sequentialParses},
                {QStringLiteral("stateless_parses"), statelessParses}});
        }
    }
    ix::ImageLoadCache::configure(192, 16, true);
    ix::ImageLoadCache::clear();
    return {{QStringLiteral("samples"), rows}, {QStringLiteral("note"),
        QStringLiteral("Windows Release/offscreen; 128x128 opaque APNG frames; warm filesystem; three alternating passes. The stateless control clears decoder state before every frame, recreating prior replay work. Each pass starts with an empty document cache; budgets are 0/16/192 MiB.")}};
}
QJsonObject profileInteraction(const QString &root) {
    QJsonObject report;
    const QImage source(root + QStringLiteral("/large.jpg"));
    for (const auto format : {QImage::Format_RGB32, QImage::Format_RGBA64}) {
        ix::ImageView view;
        view.resize(1200, 800);
        view.setImage(source.convertToFormat(format));
        view.setFit(ix::ImageView::Fit::Window);
        view.show();
        QTest::qWait(30);
        for (bool cached : {false, true}) {
            view.setDisplayCacheEnabled(cached);
            view.viewport()->repaint();
            view.viewport()->repaint();
            QJsonArray incremental, full;
            for (int i = 0; i < 40; ++i) {
            for (bool repaintAll : {i % 2 == 0, i % 2 != 0}) {
                view.setSelection(QRect(500, 500, 2500, 2000));
                QCoreApplication::processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
                QElapsedTimer timer;
                timer.start();
                view.setSelection(QRect(510 + i * 3, 510 + i * 2, 2500, 2000));
                if (repaintAll)
                    view.viewport()->update();
                QCoreApplication::processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
                (repaintAll ? full : incremental).append(timer.nsecsElapsed() / 1e6);
            }
            }
            const auto bits = format == QImage::Format_RGB32 ? QStringLiteral("8bit") : QStringLiteral("16bit");
            const auto prefix = bits + (cached ? QStringLiteral("_cached") : QStringLiteral("_uncached"));
            report.insert(prefix + QStringLiteral("_selection_ms"), incremental);
            report.insert(prefix + QStringLiteral("_selection_full_repaint_ms"), full);
            if (cached) {
                report.insert(bits + QStringLiteral("_selection_ms"), incremental);
                report.insert(bits + QStringLiteral("_selection_full_repaint_ms"), full);
            }
        }
    }
    for (const auto &extension : {QStringLiteral("jpg"), QStringLiteral("png")}) {
        QJsonArray times;
        for (int i = 0; i < 7; ++i) {
            QString warning;
            QElapsedTimer timer;
            timer.start();
            const auto metadata = ix::readMetadata(root + QStringLiteral("/large.") + extension, warning);
            if (!metadata)
                report.insert(extension + QStringLiteral("_metadata_warning"), warning);
            times.append(timer.nsecsElapsed() / 1e6);
        }
        report.insert(extension + QStringLiteral("_metadata_ms"), times);
    }
    report.insert(QStringLiteral("note"), QStringLiteral("24 MP, 1200x800 offscreen, warm filesystem. Alternating selection movement/full repaint in each cache mode; timings include GUI events. Cache modes receive two priming paints. Metadata is measured separately without image decoding."));
    return report;
}
QJsonObject profilePreviews(const QString &root) {
    QSettings().setValue(QStringLiteral("preview/previous"), 5);
    QSettings().setValue(QStringLiteral("preview/next"), 5);
    QStringList files;
    for (int i = 0; i < 13; ++i) {
        const auto path = root + QStringLiteral("/thumb-%1.png").arg(i);
        if (!QFileInfo::exists(path)) {
            QImage image(320, 240, QImage::Format_RGB32);
            image.fill(QColor::fromHsv(i * 25, 180, 190));
            if (!image.save(path))
                return {{QStringLiteral("error"), QStringLiteral("thumbnail fixture write failed")}};
        }
        files.append(path);
        ix::loadPreview(path, {68, 50});
    }
    ix::PreviewStrip strip;
    strip.resize(1200, 104);
    strip.show();
    strip.setFiles(files, 6);
    QTest::qWait(100);
    QJsonArray samples, rebuilt;
    for (int i = 0; i < 30; ++i) {
        for (bool reset : {i % 2 == 0, i % 2 != 0}) {
            strip.setFiles(files, 6);
            QTest::qWait(25);
            auto incoming = files;
            if (reset)
                incoming.detach();
            QElapsedTimer timer;
            timer.start();
            strip.setFiles(std::move(incoming), 7);
            QCoreApplication::processEvents();
            (reset ? rebuilt : samples).append(timer.nsecsElapsed() / 1e6);
            QTest::qWait(25);
        }
    }
    return {{QStringLiteral("preview_strip_step_ms"), samples},
            {QStringLiteral("preview_strip_force_rebuild_ms"), rebuilt},
            {QStringLiteral("note"), QStringLiteral("Ten neighbors, warm thumbnail cache, 1200x104 offscreen widget. Alternating order compares a shared-list step with a forced new directory snapshot in the same process; includes current UI events, excludes decode completion.")}};
}
QJsonObject profile(const QString &root, const ix::CodecRegistry &registry) {
    QJsonObject report;
    const auto folder = root + QStringLiteral("/folder-10000");
    auto measure = [&](const QString &name, auto fn, int repeats = 5) {
        QJsonArray samples;
        for (int i = 0; i < repeats; ++i) {
            QElapsedTimer timer;
            timer.start();
            fn();
            samples.append(timer.nsecsElapsed() / 1e6);
        }
        report.insert(name, samples);
    };
    const QStringList filters{QStringLiteral("*.png")};
    QFileInfoList entries;
    measure(QStringLiteral("directory_enumerate_no_permission_probe_ms"), [&] {
        entries = QDir(folder).entryInfoList(filters, QDir::Files, QDir::NoSort);
    }, 3);
    measure(QStringLiteral("directory_enumerate_readable_ms"), [&] {
        entries = QDir(folder).entryInfoList(filters, QDir::Files | QDir::Readable, QDir::NoSort);
    }, 3);
    QCollator collator(QLocale(QLocale::German, QLocale::Germany));
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::vector<QCollatorSortKey> keys;
    QStringList paths;
    measure(QStringLiteral("directory_paths_sortkeys_ms"), [&] {
        keys.clear(); paths.clear();
        for (const auto &file : entries) {
            paths.append(file.absoluteFilePath());
            keys.push_back(collator.sortKey(file.fileName()));
        }
    }, 3);
    measure(QStringLiteral("directory_sortkeys_only_ms"), [&] {
        keys.clear();
        for (const auto &file : entries)
            keys.push_back(collator.sortKey(file.fileName()));
    }, 3);
    measure(QStringLiteral("directory_paths_only_ms"), [&] {
        paths.clear();
        for (const auto &file : entries)
            paths.append(file.absoluteFilePath());
    }, 3);
    measure(QStringLiteral("directory_stream_readable_ms"), [&] {
        paths.clear();
        for (const auto &entry : QDirListing(folder, filters, QDirListing::IteratorFlag::FilesOnly))
            if (entry.isReadable())
                paths.append(entry.filePath());
    }, 3);
    measure(QStringLiteral("directory_stream_no_permission_probe_ms"), [&] {
        paths.clear();
        for (const auto &entry : QDirListing(folder, filters, QDirListing::IteratorFlag::FilesOnly))
            paths.append(entry.filePath());
    }, 3);
    report.insert(QStringLiteral("directory_entries"), entries.size());
    measure(QStringLiteral("directory_scan_single_extension_ms"), [&] {
        auto snapshot = ix::scanDirectory(folder, {QStringLiteral("png")}, 0, false, nullptr, false);
        if (snapshot.files.size() != entries.size())
            qFatal("Directory benchmark produced an incomplete index");
    }, 3);
    measure(QStringLiteral("directory_scan_all_extensions_ms"), [&] {
        auto snapshot = ix::scanDirectory(folder, registry.extensions(), 0, false, nullptr, false);
        if (snapshot.files.size() != entries.size())
            qFatal("Directory benchmark produced an incomplete index");
    }, 3);
    for (const auto &ext : {QStringLiteral("jpg"), QStringLiteral("png")}) {
        const auto path = root + QStringLiteral("/large.") + ext;
        measure(ext + QStringLiteral("_decode_only_ms"), [&] {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            auto image = reader.read();
            if (image.isNull())
                qFatal("Decode benchmark failed to read its fixture");
        });
        measure(ext + QStringLiteral("_metadata_only_ms"), [&] {
            QString warning;
            auto metadata = ix::readMetadata(path, warning);
            Q_UNUSED(metadata);
        });
    }
    measure(QStringLiteral("file_revision_1000_ms"), [&] {
        for (int i = 0; i < 1000; ++i)
            ix::imageFileKey(root + QStringLiteral("/large.jpg"));
    });
    QImage source(root + QStringLiteral("/large.jpg"));
    for (const auto &format : {QImage::Format_RGB32, QImage::Format_RGBA64}) {
        ix::ImageView view;
        view.resize(1200, 800);
        view.setImage(source.convertToFormat(format));
        view.setFit(ix::ImageView::Fit::Window);
        view.show();
        QCoreApplication::processEvents();
        QImage canvas(view.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        const auto prefix = format == QImage::Format_RGB32 ? QStringLiteral("display_8bit")
                                                         : QStringLiteral("display_16bit");
        for (bool cached : {false, true}) {
            view.setDisplayCacheEnabled(cached);
            QCoreApplication::processEvents();
            const auto suffix = cached ? QStringLiteral("_cached") : QStringLiteral("_uncached");
            measure(prefix + suffix + QStringLiteral("_repaint_ms"), [&] { view.viewport()->render(&canvas); }, 20);
            const auto pixels = view.image();
            measure(prefix + suffix + QStringLiteral("_first_paint_ms"), [&] {
                view.setImage(pixels);
                view.viewport()->render(&canvas);
            });
        }
    }
    report.insert(QStringLiteral("note"), QStringLiteral("Warm filesystem; timings are per operation, not end-to-end. Metadata and drawing retain the full image. Same fixtures and profiler must be used before and after."));
    report.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
    return report;
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("IxViewBench"));
    QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
    ix::configureDesktopAppearance();
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("windows")));
    if (app.arguments().contains(QStringLiteral("--profile-preview-reuse"))) {
        const auto report = profilePreviewReuse();
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-first-open"))) {
        const auto input = QFileInfo(app.arguments().value(3)).absoluteFilePath();
        const auto report = profileFirstOpen(input);
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-raw-cache"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        const auto report = profileRawCache();
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-slideshow"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        const auto report = profileSlideshow();
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    QString root = QDir::current().filePath(QStringLiteral("build/perf-fixtures"));
    QDir().mkpath(root);
    ix::CodecRegistry registry;
    ix::ImageDocument d;
    QString jpeg = root + QStringLiteral("/large.jpg"), png = root + QStringLiteral("/large.png");
    if (!QFileInfo::exists(jpeg) || !QFileInfo::exists(png)) {
        d.pixels = QImage(6000, 4000, QImage::Format_RGB32);
        for (int y = 0; y < 4000; ++y) {
            auto *p = reinterpret_cast<QRgb *>(d.pixels.scanLine(y));
            for (int x = 0; x < 6000; ++x)
                p[x] = qRgb((x * 7 + y) % 256, (x + y * 3) % 256, (x * 2 + y * 11) % 256);
        }
        if (!registry.save(d, jpeg, "jpeg", 95, true).isEmpty() ||
            !registry.save(d, png, "png", 95, true).isEmpty())
            return 2;
        d.pixels = {};
    }
    if (app.arguments().contains(QStringLiteral("--profile-raw-previews"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly)) return 4;
        const auto report = profileRawPreviews();
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) || report.value(QStringLiteral("samples")).toArray().isEmpty() ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-animations"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly))
            return 4;
        const auto report = profileAnimations(root, registry);
        out.write(QJsonDocument(report).toJson());
        return report.contains(QStringLiteral("error")) ? 5 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-interaction"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly))
            return 4;
        out.write(QJsonDocument(profileInteraction(root)).toJson());
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-previews"))) {
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly))
            return 4;
        out.write(QJsonDocument(profilePreviews(root)).toJson());
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile"))) {
        if (QDir(root + QStringLiteral("/folder-10000")).entryList({QStringLiteral("*.png")}, QDir::Files).size() != 10000) {
            qWarning("Run the standard benchmark once to prepare its 10000-file fixture first.");
            return 9;
        }
        QFile out(app.arguments().value(1));
        if (!out.open(QIODevice::WriteOnly))
            return 4;
        out.write(QJsonDocument(profile(root, registry)).toJson());
        return 0;
    }
    QJsonObject report;
    QElapsedTimer timer;
    for (const auto &path : {jpeg, png}) {
        QJsonArray times;
        for (int i = 0; i < 5; ++i) {
            timer.start();
            auto r = registry.read(path);
            if (!r.ok())
                return 3;
            times.append(timer.nsecsElapsed() / 1e6);
        }
        report.insert(QFileInfo(path).suffix() + QStringLiteral("_24MP_ms"), times);
    }
    ix::ImageLoadCache::clear();
    QJsonArray largeCache;
    for (int i = 0; i < 5; ++i) {
        timer.start();
        auto result = ix::ImageLoadCache::read(registry, jpeg);
        if (!result.ok())
            return 8;
        largeCache.append(timer.nsecsElapsed() / 1e6);
    }
    report.insert(QStringLiteral("cached_24MP_jpeg_ms"), largeCache);
    QJsonArray startup;
    for (int i = 0; i < 5; ++i) {
        timer.start();
        {
            ix::MainWindow window;
            window.show();
            QCoreApplication::processEvents();
            startup.append(timer.nsecsElapsed() / 1e6);
        }
    }
    report.insert(QStringLiteral("window_offscreen_ms"), startup);

    QString folder = root + QStringLiteral("/folder-10000");
    QDir().mkpath(folder);
    for (int i = 0; i < 10000; ++i) {
        QFile file(folder + QStringLiteral("/image%1.png").arg(i));
        if (!file.exists()) {
            if (!file.open(QIODevice::WriteOnly))
                return 5;
            file.write("fixture");
        }
    }
    timer.start();
    auto snapshot = ix::scanDirectory(folder, {QStringLiteral("png")}, 0, false, nullptr, false);
    report.insert(QStringLiteral("folder_10000_scan_ms"), timer.nsecsElapsed() / 1e6);
    volatile qint64 sink = 0;
    timer.start();
    for (int i = 0; i < 10000; ++i)
        sink = sink + snapshot.files.indexOf(snapshot.files[i]);
    report.insert(QStringLiteral("folder_10000_linear_lookup_ms"), timer.nsecsElapsed() / 1e6);
    timer.start();
    for (int i = 0; i < 10000; ++i)
        sink = sink + snapshot.positions.value(snapshot.files[i]);
    report.insert(QStringLiteral("folder_10000_hash_lookup_ms"), timer.nsecsElapsed() / 1e6);
    QString small = root + QStringLiteral("/cache-2MP.jpg");
    if (!QFileInfo::exists(small)) {
        d.pixels = QImage(jpeg).scaled(1920, 1280);
        if (!registry.save(d, small, "jpeg", 95, true).isEmpty())
            return 6;
        d.pixels = {};
    }
    ix::ImageLoadCache::clear();
    QJsonArray cache;
    for (int i = 0; i < 5; ++i) {
        timer.start();
        auto read = ix::ImageLoadCache::read(registry, small);
        if (!read.ok())
            return 7;
        cache.append(timer.nsecsElapsed() / 1e6);
    }
    report.insert(QStringLiteral("cached_2MP_jpeg_ms"), cache);
    report.insert(QStringLiteral("note"),
                  QStringLiteral("Windows offscreen, warm filesystem; timings vary. Folder lookup compares "
                                 "identical data structures in this build. This benchmark does not measure RAW."));
    report.insert(QStringLiteral("platform"), QSysInfo::prettyProductName());
    report.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
    QFile out(app.arguments().value(1, QStringLiteral("tests/output/performance-before.json")));
    if (!out.open(QIODevice::WriteOnly))
        return 4;
    out.write(QJsonDocument(report).toJson());
    return 0;
}
