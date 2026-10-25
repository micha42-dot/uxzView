// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
namespace ix {
// Identity and native timestamp precision, independent of image decoding/caches.
QString imageFileKey(const QString &path);
} // namespace ix
