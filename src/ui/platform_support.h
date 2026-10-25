// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <QFont>
#include <QKeySequence>
#include <functional>
#include <memory>
class QWidget;
namespace ix {
QFont referenceFont(int points = 8);
void configureDesktopAppearance();
void configureUiFont();
class WindowTransition {
  public:
    explicit WindowTransition(QWidget *window);
    ~WindowTransition();
    WindowTransition(const WindowTransition &) = delete;
    WindowTransition &operator=(const WindowTransition &) = delete;
  private:
    struct NativeState;
    QWidget *window_;
    bool updates_;
    std::unique_ptr<NativeState> native_;
};
QString configureGlobalCaptureShortcut(QWidget *owner, const QKeySequence &sequence,
                                       std::function<void()> capture);
} // namespace ix
