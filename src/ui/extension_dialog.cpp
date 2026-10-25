// SPDX-License-Identifier: GPL-3.0-or-later
#include "extension_dialog.h"
#include "core/localization.h"
#include "core/plugin_store.h"
#include <QLibrary>
#include <QMessageBox>
namespace ix {
bool extensionManagerDialog(QWidget *parent, const QString &selectedTool) {
#ifdef IX_EXTENSIONS_MODULE
    QLibrary library(optionalModulePath(QStringLiteral(IX_EXTENSIONS_MODULE)));
    library.setLoadHints(QLibrary::PreventUnloadHint);
    if (!selectedTool.isEmpty()) {
        using OpenTool = void (*)(QWidget *, const char *);
        if (auto open = reinterpret_cast<OpenTool>(library.resolve("ixOpenFunctionExtensionManager"))) {
            const auto id = selectedTool.toUtf8();
            open(parent, id.constData());
            return true;
        }
    }
    using Open = void (*)(QWidget *);
    if (auto open = reinterpret_cast<Open>(library.resolve("ixOpenExtensionManager"))) {
        open(parent);
        return true;
    }
    QMessageBox::warning(parent, QStringLiteral("üxzView"),
        IX_TEXT("Erweiterungsverwaltung konnte nicht geladen werden: ") + library.errorString());
#else
    Q_UNUSED(selectedTool);
    QMessageBox::warning(parent, QStringLiteral("üxzView"),
        IX_TEXT("Die optionale Erweiterungsverwaltung ist in diesem Build nicht enthalten."));
#endif
    return false;
}
}
