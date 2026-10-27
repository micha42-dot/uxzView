// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearance.h"
#include <QApplication>
#include <QPalette>
#include <QPointer>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyle>
#include <utility>

namespace ix {
namespace {
QColor color(const char *value) { return QColor(QString::fromLatin1(value)); }
QPalette colors(bool dark) {
    QPalette p;
    const auto window = color(dark ? "#1d2026" : "#f3f3f3");
    const auto text = color(dark ? "#e5e8ee" : "#20242b");
    const auto accent = color(dark ? "#80adfa" : "#285fce");
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, color(dark ? "#15181d" : "#ffffff"));
    p.setColor(QPalette::AlternateBase, color(dark ? "#242830" : "#f7f8fa"));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, color(dark ? "#292e37" : "#f3f3f3"));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::Accent, accent);
    p.setColor(QPalette::HighlightedText, color(dark ? "#101925" : "#ffffff"));
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::LinkVisited, color(dark ? "#c3a4ef" : "#7143a3"));
    p.setColor(QPalette::BrightText, color(dark ? "#ffaaa5" : "#a42828"));
    p.setColor(QPalette::PlaceholderText, color(dark ? "#929ba9" : "#707782"));
    p.setColor(QPalette::Light, color(dark ? "#414956" : "#ffffff"));
    p.setColor(QPalette::Midlight, color(dark ? "#343b46" : "#e4e6e9"));
    p.setColor(QPalette::Mid, color(dark ? "#414956" : "#b9bfc8"));
    p.setColor(QPalette::Dark, color(dark ? "#101318" : "#8c939d"));
    p.setColor(QPalette::Shadow, color(dark ? "#090b0f" : "#6d7580"));
    p.setColor(QPalette::ToolTipBase, color(dark ? "#303641" : "#252a32"));
    p.setColor(QPalette::ToolTipText, color("#f2f4f8"));
    const auto disabled = color(dark ? "#858e9d" : "#858c96");
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                            QPalette::PlaceholderText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, color(dark ? "#3a4352" : "#d9dde3"));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return p;
}
class Appearance final : public QObject {
  public:
    explicit Appearance(QObject *parent) : QObject(parent) {
        fallbackDark_ = QApplication::palette().color(QPalette::Window).lightnessF() < .5;
        // Fusion honors every palette role and keeps control geometry stable
        // across modes. No style reload occurs during image loading/painting.
        if (QApplication::style()->objectName().compare(QStringLiteral("fusion"), Qt::CaseInsensitive))
            if (auto *style = QStyleFactory::create(QStringLiteral("Fusion")))
                QApplication::setStyle(style);
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) { if (mode_ == QStringLiteral("system")) update(); });
    }
    void apply(QString mode) {
        if (mode != QStringLiteral("light") && mode != QStringLiteral("dark"))
            mode = QStringLiteral("system");
        if (initialized_ && mode == mode_) return;
        mode_ = std::move(mode);
        initialized_ = true;
        QGuiApplication::styleHints()->setColorScheme(mode_ == QStringLiteral("dark") ? Qt::ColorScheme::Dark
            : mode_ == QStringLiteral("light") ? Qt::ColorScheme::Light : Qt::ColorScheme::Unknown);
        update();
    }
  private:
    QString mode_;
    bool initialized_ = false, fallbackDark_ = false;
    void update() {
        const auto scheme = QGuiApplication::styleHints()->colorScheme();
        const bool dark = mode_ == QStringLiteral("dark") || (mode_ == QStringLiteral("system") &&
            (scheme == Qt::ColorScheme::Dark || (scheme == Qt::ColorScheme::Unknown && fallbackDark_)));
        const auto palette = colors(dark);
        if (QApplication::palette() != palette)
            QApplication::setPalette(palette);
    }
};
} // namespace
void applyInterfaceTheme(const QString &theme) {
    static QPointer<Appearance> appearance;
    if (!appearance) appearance = new Appearance(qApp);
    appearance->apply(theme);
}
} // namespace ix
