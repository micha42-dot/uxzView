// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "application.h"
#include <QFileOpenEvent>
namespace ix {
bool Application::event(QEvent *event) {
    if (event->type() == QEvent::FileOpen) {
        auto *open = static_cast<QFileOpenEvent *>(event);
        if (!open->file().isEmpty() && fileOpened) {
            fileOpened(open->file());
            event->accept();
            return true;
        }
    }
    return QApplication::event(event);
}
} // namespace ix
