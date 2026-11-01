// SPDX-License-Identifier: GPL-3.0-or-later
#include "memory_policy.h"
#include "localization.h"
#include <QImageReader>
#include <QSettings>
#include <algorithm>
#include <limits>
#include <mutex>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#elif defined(Q_OS_LINUX)
#include <sys/sysinfo.h>
#elif defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#endif
namespace ix {
namespace {
std::once_flag initialized;
qint64 memoryLimit = 512 * MiB;
}
quint64 physicalMemoryBytes() {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX state{}; state.dwLength = sizeof(state);
    if (GlobalMemoryStatusEx(&state)) return state.ullTotalPhys;
#elif defined(Q_OS_LINUX)
    struct sysinfo state{};
    if (sysinfo(&state) == 0 && state.mem_unit) {
        const auto unit = quint64(state.mem_unit), ram = quint64(state.totalram);
        return ram > std::numeric_limits<quint64>::max() / unit
            ? std::numeric_limits<quint64>::max() : ram * unit;
    }
#elif defined(Q_OS_MACOS)
    quint64 value = 0; size_t length = sizeof(value);
    if (sysctlbyname("hw.memsize", &value, &length, nullptr, 0) == 0) return value;
#endif
    return 1024 * MiB;
}
qint64 calculateImageMemoryLimit(quint64 physical, qint64 manualMiB, int addressBits) {
    const qint64 automatic = qint64(std::clamp<quint64>(physical / 2, 512 * MiB, 8192 * MiB)) / MiB * MiB;
    const qint64 requested = manualMiB > 0 ? std::clamp<qint64>(manualMiB, 1, 1048576) * MiB : automatic;
    return std::min(requested, addressBits <= 32 ? 1024 * MiB : 1048576 * MiB);
}
void initializeImageMemoryLimit(qint64 overrideMiB) {
    std::call_once(initialized, [overrideMiB] {
        const auto manual = overrideMiB < 0
            ? QSettings().value(QStringLiteral("performance/memoryLimitMiB"), 0).toLongLong() : overrideMiB;
        memoryLimit = calculateImageMemoryLimit(physicalMemoryBytes(), manual);
        QImageReader::setAllocationLimit(int(memoryLimit / MiB));
    });
}
qint64 imageMemoryLimit() { initializeImageMemoryLimit(); return memoryLimit; }
QString imageSizeLimitError(QSize requested, int bpp) {
    const auto limit = imageMemoryLimit();
    const auto width = std::max(1, std::min(requested.width(), ImageAxisLimit));
    const auto maxHeight = std::min<qint64>(ImageAxisLimit, limit / std::max(1, bpp) / width);
    return IX_TEXT("Speicherbudget: %1 MiB. Bei %2 Pixel Breite sind höchstens %3 Pixel Höhe möglich.")
        .arg(limit / MiB).arg(width).arg(maxHeight);
}
} // namespace ix
