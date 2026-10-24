// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <atomic>
namespace ix {
class ExtensionStorage {
  public:
    enum class Domain { Formats, Tools };
    static QString defaultRoot(Domain domain = Domain::Formats);
    static QString requestedRoot(Domain domain = Domain::Formats);
    static QString activeRoot(Domain domain = Domain::Formats);
    static void initialize();
    static bool restartPending(Domain domain = Domain::Formats);
    // Copies into an empty destination and switches only after the next restart.
    // The source is retained. A cancelled/failed copy never changes the setting.
    static QString prepareRoot(const QString &destination, const std::atomic_bool &cancel, Domain domain = Domain::Formats);
};
} // namespace ix
