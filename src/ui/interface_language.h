// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
class QObject;
class QAction;
class QToolBar;
namespace ix {
// GUI-thread API. Invalid codes leave the active UI unchanged.
bool applyInterfaceLanguage(const QString &code);
// Retain the source instead of trying to reverse an existing translation.
void bindUiText(QObject *target, const char *property, const QString &source,
                const QString &suffix = {}, const QString &prefix = {}, const QStringList &arguments = {});
void unbindUiText(QObject *target, const char *property);
void retranslateUiText(QObject *root);
void bindToolbarDescription(QToolBar *toolbar, QAction *action, const QString &source);
} // namespace ix
