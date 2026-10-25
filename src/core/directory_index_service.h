// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "directory_index.h"
#include <QObject>
#include <QThreadPool>
#include <functional>
#include <memory>
#include <optional>

Q_DECLARE_METATYPE(ix::DirectorySnapshot)

namespace ix {
struct DirectoryRequest {
    QString directory;
    QStringList extensions;
    int sorting = 0;
    bool reverse = false;
    bool findLatest = false;
};

// GUI-thread owner of one cancellable scan and at most one pending request.
// Workers receive values and never access the service or its consumers.
class DirectoryIndexService : public QObject {
    Q_OBJECT
  public:
    using Scanner = std::function<DirectorySnapshot(const DirectoryRequest &, const std::atomic_bool *)>;
    explicit DirectoryIndexService(QObject *parent = nullptr, Scanner scanner = {});
    ~DirectoryIndexService() override;
    void request(DirectoryRequest request);
    void cancel();
    bool isBusy() const { return running_ || pending_.has_value(); }
  signals:
    void ready(ix::DirectorySnapshot snapshot);
    void failed(QString error);
  private:
    void startNext();
    Scanner scanner_;
    QThreadPool pool_;
    std::optional<DirectoryRequest> pending_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    bool running_ = false;
};
} // namespace ix
