// SPDX-License-Identifier: GPL-3.0-or-later
#include "file_operations.h"
#include "file_revision.h"
#include "image_catalog.h"
#include "localization.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
namespace ix {
namespace {
QMutex operationMutex;
bool occupied(const QString &path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}
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
FileOperations::FileOperations(TrashMover trashMover, BeforeChange beforeChange)
    : trashMover_(std::move(trashMover)), beforeChange_(std::move(beforeChange)) {
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
    // normally metadata-only; Qt handles cross-volume copies when necessary.
    const bool applied = kind == QStringLiteral("copy") ? QFile::copy(from, to) : QFile::rename(from, to);
    if (!applied)
        return {IX_TEXT("Dateioperation fehlgeschlagen.")};
    const auto targetRevision = imageFileKey(to);
    const auto warning = kind == QStringLiteral("move") ? ImageCatalog().relocate(from, to) : QString();
    return {warning, true, sourceRevision, targetRevision};
}
} // namespace ix
