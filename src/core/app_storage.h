// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QVariantMap>
#include <atomic>
namespace ix {
class AppStorage {
  public:
    static QString applicationHome();
    // Initialize once, before settings, plugins or workers are created.
    static QString initialize(const QString &home = {}, bool portable = false);
    static bool isPortable();
    static bool portableRequested(const QString &home = {});
    static QString dataRoot();
    static QString cacheRoot();
    static QString preparePortable(const QString &home, const QVariantMap &settings,
                                   const QString &sourceData, const std::atomic_bool &cancel);
    static QString disablePortable(const QString &home = {});
};
} // namespace ix
