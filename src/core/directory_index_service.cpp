// SPDX-License-Identifier: GPL-3.0-or-later
#include "directory_index_service.h"
#include "localization.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent>
#include <exception>
#include <utility>

namespace ix {
namespace {
struct ScanResult {
    DirectorySnapshot snapshot;
    QString error;
};
} // namespace

DirectoryIndexService::DirectoryIndexService(QObject *parent, Scanner scanner)
    : QObject(parent), scanner_(std::move(scanner)) {
    qRegisterMetaType<DirectorySnapshot>();
    if (!scanner_)
        scanner_ = [](const DirectoryRequest &request, const std::atomic_bool *cancel) {
            return scanDirectory(request.directory, request.extensions, request.sorting,
                                 request.reverse, cancel, request.findLatest);
        };
    pool_.setMaxThreadCount(1);
    pool_.setExpiryTimeout(10000);
    pool_.setThreadPriority(QThread::LowPriority);
}

DirectoryIndexService::~DirectoryIndexService() {
    cancel();
    pool_.waitForDone();
}

void DirectoryIndexService::request(DirectoryRequest request) {
    pending_ = std::move(request);
    if (cancelled_)
        cancelled_->store(true);
    startNext();
}

void DirectoryIndexService::cancel() {
    pending_.reset();
    if (cancelled_)
        cancelled_->store(true);
}

void DirectoryIndexService::startNext() {
    if (running_ || !pending_)
        return;
    auto request = std::move(*pending_);
    pending_.reset();
    const auto scanner = scanner_;
    const auto token = std::make_shared<std::atomic_bool>(false);
    cancelled_ = token;
    running_ = true;
    auto *watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, token] {
        auto result = watcher->result();
        watcher->deleteLater();
        running_ = false;
        cancelled_.reset();
        const QPointer<DirectoryIndexService> alive(this);
        if (!token->load()) {
            if (!result.error.isEmpty())
                emit failed(result.error);
            else
                emit ready(std::move(result.snapshot));
        }
        // A result consumer can request another scan or destroy its owner.
        if (alive)
            startNext();
    });
    watcher->setFuture(QtConcurrent::run(&pool_, [request = std::move(request), scanner, token] {
        try {
            return ScanResult{scanner(request, token.get()), {}};
        } catch (const std::exception &error) {
            return ScanResult{{}, QString::fromUtf8(error.what())};
        } catch (...) {
            return ScanResult{{}, IX_TEXT("Ordner konnte nicht gelesen werden.")};
        }
    }));
}
} // namespace ix
