// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
namespace ix {
// Configure once at startup, before creating windows or starting workers.
bool setUiLanguage(const QString &language);
QString uiLanguage();
QString localized(const QString &source);
} // namespace ix
#define IX_TEXT(text) ::ix::localized(QStringLiteral(text))
