// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QLocale>
namespace ix {
// GUI-thread setter; published immutable catalogs permit concurrent readers.
// Does not mutate the process-wide default locale. Widgets use uiLocale().
bool setUiLanguage(const QString &language);
QString uiLanguage();
QLocale uiLocale();
QString localized(const QString &source);
} // namespace ix
#define IX_TEXT(text) ::ix::localized(QStringLiteral(text))
