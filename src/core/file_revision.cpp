// SPDX-License-Identifier: GPL-3.0-or-later
#include "file_revision.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#elif defined(Q_OS_UNIX)
#include <sys/stat.h>
#endif
namespace ix {
QString imageFileKey(const QString &path) {
    QFileInfo f(path);
    QString revision;
#ifdef Q_OS_WIN
    // NTFS change time is independent of the user-settable last-write time.
    // Keep its native precision; millisecond timestamps miss rapid rewrites.
    const auto absolute = f.absoluteFilePath();
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(absolute.utf16()), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        FILE_BASIC_INFO basic{};
        BY_HANDLE_FILE_INFORMATION identity{};
        const bool haveBasic = GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic));
        const bool haveIdentity = GetFileInformationByHandle(handle, &identity);
        CloseHandle(handle);
        if (haveBasic && haveIdentity) {
            const auto bytes = (quint64(identity.nFileSizeHigh) << 32) | identity.nFileSizeLow;
            // The handle already provides size, all timestamps and identity.
            // QFileInfo would query the same metadata again (including another
            // Windows handle for change time). Keep native timestamp precision.
            return absolute + QLatin1Char('|') + QStringLiteral("%1/%2/%3/%4/%5/%6/%7")
                .arg(bytes).arg(basic.LastWriteTime.QuadPart).arg(basic.ChangeTime.QuadPart)
                .arg(basic.CreationTime.QuadPart).arg(identity.dwVolumeSerialNumber)
                .arg(identity.nFileIndexHigh).arg(identity.nFileIndexLow);
        }
        if (haveBasic)
            revision += QString::number(basic.ChangeTime.QuadPart);
        if (haveIdentity)
            revision += QStringLiteral("/%1/%2/%3")
                            .arg(identity.dwVolumeSerialNumber)
                            .arg(identity.nFileIndexHigh)
                            .arg(identity.nFileIndexLow);
    }
#elif defined(Q_OS_UNIX)
    struct stat info{};
    if (::stat(QFile::encodeName(f.absoluteFilePath()).constData(), &info) == 0) {
#ifdef Q_OS_MACOS
        const auto changed = info.st_ctimespec;
        const auto modified = info.st_mtimespec;
#else
        const auto changed = info.st_ctim;
        const auto modified = info.st_mtim;
#endif
        return f.absoluteFilePath() + QLatin1Char('|') + QStringLiteral("%1/%2/%3/%4/%5/%6/%7")
                       .arg(qint64(info.st_size))
                       .arg(qint64(modified.tv_sec)).arg(qint64(modified.tv_nsec))
                       .arg(qint64(changed.tv_sec)).arg(qint64(changed.tv_nsec))
                       .arg(qint64(info.st_dev))
                       .arg(quint64(info.st_ino));
    }
#endif
    return f.absoluteFilePath() + QLatin1Char('|') + QString::number(f.size()) + QLatin1Char('|') +
           QString::number(f.lastModified().toMSecsSinceEpoch()) + QLatin1Char('|') +
           QString::number(f.metadataChangeTime().toMSecsSinceEpoch()) + QLatin1Char('|') +
           QString::number(f.birthTime().toMSecsSinceEpoch()) + QLatin1Char('|') + revision;
}
} // namespace ix
