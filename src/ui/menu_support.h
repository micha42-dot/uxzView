// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "interface_language.h"
#include <QMenu>
#include <QMenuBar>
namespace ix {
inline QMenu *ensureRootMenu(QMenuBar &bar, const QString &id, const QString &title) {
    if (auto *menu = bar.findChild<QMenu *>(id)) return menu;
    auto *menu = bar.addMenu(QString());
    menu->setObjectName(id);
    bindUiText(menu, "title", title);
    return menu;
}
} // namespace ix
