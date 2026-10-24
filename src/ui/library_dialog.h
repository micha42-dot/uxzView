// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
#include <functional>
class QWidget;
namespace ix {
void libraryDialog(QWidget *parent, const QStringList &files, const std::function<void(QString)> &open,
                   const QString &journalRoot = {});
void journalDialog(QWidget *parent, const QString &journalRoot = {});
QString undoFileOperation(QWidget *parent, const QString &id = {}, const QString &journalRoot = {});
} // namespace ix
