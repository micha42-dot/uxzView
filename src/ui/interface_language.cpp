// SPDX-License-Identifier: GPL-3.0-or-later
#include "interface_language.h"
#include "core/localization.h"
#include "platform_support.h"
#include <QApplication>
#include <QEvent>
#include <QLibraryInfo>
#include <QPointer>
#include <QTranslator>
#include <QVariant>
#include <QWidget>
#include <QAction>
#include <QToolBar>

namespace ix {
namespace {
constexpr auto Texts = "ixUiTexts";
void writeText(QObject *target, const QByteArray &property, const QVariantList &binding) {
    if (binding.size() != 4) return;
    auto text = localized(binding[0].toString());
    for (const auto &argument : binding[3].toStringList()) text = text.arg(argument);
    target->setProperty(property.constData(), binding[2].toString() + text + binding[1].toString());
}
class LanguageController final : public QObject {
  public:
    explicit LanguageController(QObject *parent) : QObject(parent) {}
    bool apply(const QString &code) {
        if (applied_ == code && uiLanguage() == code) return true;
        if (!setUiLanguage(code)) return false;
        applied_ = code;
        if (installed_) QCoreApplication::removeTranslator(&translator_);
        installed_ = translator_.load(QStringLiteral("qtbase_") + code,
                                      QLibraryInfo::path(QLibraryInfo::TranslationsPath));
        if (installed_) QCoreApplication::installTranslator(&translator_);
        configureUiFont();
        // English has no qtbase catalog. Explicit notification also covers that
        // transition and builds without an installed Qt translation directory.
        QList<QPointer<QWidget>> windows;
        for (auto *window : QApplication::topLevelWidgets()) windows.append(window);
        for (const auto &window : windows) {
            if (!window) continue;
            window->setLocale(uiLocale());
            QEvent event(QEvent::LanguageChange);
            QCoreApplication::sendEvent(window, &event);
        }
        return true;
    }
  private:
    QTranslator translator_;
    QString applied_;
    bool installed_ = false;
};
} // namespace
bool applyInterfaceLanguage(const QString &code) {
    static QPointer<LanguageController> controller;
    if (!controller) controller = new LanguageController(qApp);
    return controller->apply(code);
}
void bindUiText(QObject *target, const char *property, const QString &source,
                const QString &suffix, const QString &prefix, const QStringList &arguments) {
    auto texts = target->property(Texts).toMap();
    const QVariantList binding{source, suffix, prefix, arguments};
    texts.insert(QString::fromLatin1(property), binding);
    target->setProperty(Texts, texts);
    writeText(target, QByteArray(property), binding);
}
void unbindUiText(QObject *target, const char *property) {
    auto texts = target->property(Texts).toMap();
    texts.remove(QString::fromLatin1(property));
    target->setProperty(Texts, texts.isEmpty() ? QVariant() : QVariant(texts));
}
void retranslateUiText(QObject *root) {
    auto objects = root->findChildren<QObject *>();
    objects.prepend(root);
    for (auto *object : objects) {
        const auto texts = object->property(Texts).toMap();
        for (auto it = texts.cbegin(); it != texts.cend(); ++it)
            writeText(object, it.key().toLatin1(), it.value().toList());
    }
    for (auto *object : objects)
        if (auto *toolbar = qobject_cast<QToolBar *>(object))
            for (auto *action : toolbar->actions()) {
                const auto source = action->property("ixToolbarDescription").toString();
                if (!source.isEmpty()) bindToolbarDescription(toolbar, action, source);
            }
}
void bindToolbarDescription(QToolBar *toolbar, QAction *action, const QString &source) {
    action->setProperty("ixToolbarDescription", source);
    const auto description = localized(source);
    auto tip = description;
    if (!action->shortcuts().isEmpty())
        tip += QStringLiteral(" (%1)").arg(action->shortcuts().first().toString(QKeySequence::NativeText));
    action->setToolTip(tip);
    action->setStatusTip(description);
    if (auto *widget = toolbar->widgetForAction(action)) {
        widget->setAccessibleName(description);
        widget->setAccessibleDescription(tip);
    }
}
} // namespace ix
