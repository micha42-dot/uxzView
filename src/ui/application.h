// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <QApplication>
#include <functional>
namespace ix {
// Finder sends file-open events instead of command-line arguments to a running bundle.
class Application : public QApplication {
  public:
    using QApplication::QApplication;
    std::function<void(const QString &)> fileOpened;

  protected:
    bool event(QEvent *event) override;
};
} // namespace ix
