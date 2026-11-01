// SPDX-License-Identifier: GPL-3.0-or-later
#include "file_operations.h"
#include "file_revision.h"
#include "image_catalog.h"
#include "localization.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QTemporaryDir>
#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif
namespace ix {
namespace {
QRecursiveMutex operationMutex;
bool occupied(const QString &path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}
#ifdef Q_OS_UNIX
bool renameNoReplace(const QString &from, const QString &to, int &error) {
    const auto source = QFile::encodeName(from), target = QFile::encodeName(to);
#if defined(Q_OS_LINUX) && defined(SYS_renameat2)
    if (::syscall(SYS_renameat2, AT_FDCWD, source.constData(), AT_FDCWD, target.constData(), RENAME_NOREPLACE) == 0)
        return true;
    error = errno;
    if (error != ENOSYS && error != EINVAL && error != EOPNOTSUPP) return false;
#elif defined(Q_OS_MACOS)
    if (::renameatx_np(AT_FDCWD, source.constData(), AT_FDCWD, target.constData(), RENAME_EXCL) == 0)
        return true;
    error = errno;
    if (error != ENOTSUP && error != EINVAL) return false;
#endif
    // Unlike Qt 6.8's final fallback, never call overwriting POSIX rename().
    // link() publishes only an absent name. An unlink failure leaves both names
    // and an error rather than risking removal of a concurrently replaced name.
    if (::link(source.constData(), target.constData()) != 0) { error = errno; return false; }
    if (::unlink(source.constData()) == 0) return true;
    error = errno;
    return false;
}
#endif
bool moveNoReplace(const QString &from, const QString &to, const QString &revision) {
#ifdef Q_OS_UNIX
    int error = 0;
    if (renameNoReplace(from, to, error)) return true;
    if (error != EXDEV) return false;
    // Copy into a private directory on the destination filesystem, then publish
    // the complete file using the same no-replace primitive. Qt retains its
    // platform copy optimizations and permission handling inside that directory.
    QTemporaryDir staging(QFileInfo(to).absolutePath() + QStringLiteral("/.uxzview-move-XXXXXX"));
    if (!staging.isValid()) return false;
    const auto staged = staging.filePath(QStringLiteral("image"));
    if (!QFile::copy(from, staged) || imageFileKey(from) != revision) return false;
    if (!renameNoReplace(staged, to, error)) return false;
    if (imageFileKey(from) != revision) return false;
    return QFile::remove(from);
#else
    Q_UNUSED(revision);
    return QFile::rename(from, to);
#endif
}
}
struct FileOperations::CatalogBatch::State {
    QMutexLocker<QRecursiveMutex> guard{&operationMutex};
    ImageCatalog catalog;
    QList<QPair<QString, QString>> moves;
};
FileOperations::CatalogBatch::CatalogBatch() : state_(std::make_unique<State>()) {}
FileOperations::CatalogBatch::~CatalogBatch() {
    try {
        const auto error = commit();
        if (!error.isEmpty()) qWarning("Catalog batch teardown: %s", qPrintable(error));
    } catch (...) { qWarning("Catalog batch teardown failed."); }
}
QString FileOperations::CatalogBatch::commit() {
    auto moves = std::move(state_->moves);
    state_->moves = {};
    return state_->catalog.relocate(moves);
}
QString sha256File(const QString &path, const std::atomic_bool *cancel) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancel && cancel->load())
            return {};
        const auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFile::NoError)
            return {};
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
FileOperations::FileOperations(TrashMover trashMover, BeforeChange beforeChange, CatalogBatch *catalogBatch)
    : trashMover_(std::move(trashMover)), beforeChange_(std::move(beforeChange)), catalogBatch_(catalogBatch) {
    if (!trashMover_) {
        trashMover_ = [](const QString &path, QString *destination) {
            QFile file(path);
            destination->clear();
            return file.moveToTrash() ? QString() : file.errorString();
        };
    }
}
FileOperations::OperationResult FileOperations::perform(const QString &kind, const QString &source,
                                                       const QString &destination, const QString &expectedSource) {
    if (kind != QStringLiteral("copy") && kind != QStringLiteral("move") && kind != QStringLiteral("delete"))
        return {IX_TEXT("Unbekannte Dateioperation.")};
    QMutexLocker lock(&operationMutex);
    const QFileInfo input(source);
    if (!input.isFile() || input.isSymLink())
        return {IX_TEXT("Quelle ist keine normale Datei oder ist eine Verknüpfung.")};
    const auto from = input.absoluteFilePath();
    const auto sourceRevision = imageFileKey(from);
    const auto changed = [&] { return sourceRevision != imageFileKey(from); };
    if (!expectedSource.isEmpty() && expectedSource != sourceRevision)
        return {IX_TEXT("Die Datei wurde während des Lesens verändert. Bitte erneut öffnen.")};
    if (kind == QStringLiteral("delete")) {
        if (beforeChange_) {
            const auto error = beforeChange_();
            if (!error.isEmpty())
                return {error};
        }
        if (changed())
            return {IX_TEXT("Die Datei wurde während des Lesens verändert. Bitte erneut öffnen.")};
        QString target;
        const auto error = trashMover_(from, &target);
        return {error.isEmpty() ? QString() : IX_TEXT("Datei konnte nicht in den Systempapierkorb verschoben werden: %1").arg(error), error.isEmpty()};
    }
    if (destination.isEmpty())
        return {IX_TEXT("Ziel fehlt.")};
    const auto to = QFileInfo(destination).absoluteFilePath();
    if (occupied(to) || from == to)
        return {IX_TEXT("Zieldatei existiert bereits.")};
    if (beforeChange_) {
        const auto error = beforeChange_();
        if (!error.isEmpty())
            return {error};
    }
    if (changed())
        return {IX_TEXT("Die Datei wurde während des Lesens verändert. Bitte erneut öffnen.")};
    // No history, private backup or full-content hashing. Native rename is
    // normally metadata-only; cross-volume moves stage on the target volume.
    const bool applied = kind == QStringLiteral("copy") ? QFile::copy(from, to) : moveNoReplace(from, to, sourceRevision);
    if (!applied)
        return {IX_TEXT("Dateioperation fehlgeschlagen.")};
    const auto targetRevision = imageFileKey(to);
    QString warning;
    if (kind == QStringLiteral("move")) {
        if (catalogBatch_) catalogBatch_->state_->moves.append({from, to});
        else warning = ImageCatalog().relocate(from, to);
    }
    return {{}, true, sourceRevision, targetRevision, warning};
}
} // namespace ix
