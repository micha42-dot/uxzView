// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
namespace ix {
// Identity and native timestamp precision, independent of image decoding/caches.
// macOS deliberately leaves ctime out: the system rewrites it for asynchronous
// xattr updates (LaunchServices, quarantine/macl, File Provider), which made
// unchanged files look modified mid-read. See the note in the implementation.
QString imageFileKey(const QString &path);
} // namespace ix
