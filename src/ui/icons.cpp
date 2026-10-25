// SPDX-License-Identifier: GPL-3.0-or-later
#include "icons.h"
#include <QIconEngine>
#include <QMap>
#include <QPainter>
#include <QPixmapCache>
#include <QSvgRenderer>
namespace ix {
namespace {
const QMap<QString, QString> &artwork() {
    // Original artwork on a 24-unit grid: rounded 1.65-unit strokes, generous
    // counters and optically balanced 18-unit silhouettes at 20–32 px.
    static const QMap<QString, QString> shapes{
        {QStringLiteral("open"),
         QStringLiteral("<path d='M3.5 17.5V6.5a2 2 0 0 1 2-2H9l2 2h6.5a2 2 0 0 1 2 2v1"
                        "M5.5 10h15l-2.1 8a2 2 0 0 1-1.9 1.5H5.5a2 2 0 0 1-2-2Z'/>")},
        {QStringLiteral("slideshow"),
         QStringLiteral("<rect x='3.5' y='4.5' width='17' height='12' rx='2'/>"
                        "<path d='m10 8 4.5 2.5-4.5 2.5Z' fill='currentColor' stroke='none'/>"
                        "<path d='M12 16.5v4m-3 0h6'/>")},
        {QStringLiteral("save"),
         QStringLiteral("<path d='M5.5 3.5H16l4.5 4.5v10.5a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2v-13"
                        "a2 2 0 0 1 2-2ZM7.5 3.5v5h8v-5M7.5 20.5V14a1 1 0 0 1 1-1h7a1 1 0 0 1 1 1v6.5'/>")},
        {QStringLiteral("print"),
         QStringLiteral("<path d='M7 8.5v-5h10v5M7 17H5.5a2 2 0 0 1-2-2v-4.5a2 2 0 0 1 2-2h13"
                        "a2 2 0 0 1 2 2V15a2 2 0 0 1-2 2H17'/><path d='M7 13.5h10v7H7Z'/>"
                        "<circle cx='17' cy='11' r='.8' fill='currentColor' stroke='none'/>")},
        {QStringLiteral("trash"),
         QStringLiteral("<path d='M4.5 6.5h15M9 6.5v-3h6v3M6.5 6.5l.7 12a2 2 0 0 0 2 2h5.6"
                        "a2 2 0 0 0 2-2l.7-12M10 10.5v6m4-6v6'/>")},
        {QStringLiteral("cut"),
         QStringLiteral("<circle cx='6' cy='6.5' r='2.5'/><circle cx='6' cy='17.5' r='2.5'/>"
                        "<path d='m8 8 11.5 11.5M8 16 19.5 4.5'/>")},
        {QStringLiteral("copy"),
         QStringLiteral("<rect x='8' y='8' width='12.5' height='12.5' rx='2'/>"
                        "<path d='M15.5 5.5a2 2 0 0 0-2-2h-8a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2'/>")},
        {QStringLiteral("paste"),
         QStringLiteral("<path d='M8.5 5.5h-2a2 2 0 0 0-2 2v11a2 2 0 0 0 2 2h11a2 2 0 0 0 2-2v-11"
                        "a2 2 0 0 0-2-2h-2M8.5 12.5h7m-7 4h5'/><rect x='8.5' y='3.5' width='7' height='4' rx='1'/>")},
        {QStringLiteral("undo"),
         QStringLiteral("<path d='m8.5 4.5-5 5 5 5M3.5 9.5h11a5 5 0 0 1 0 10h-3'/>")},
        {QStringLiteral("image-info"),
         QStringLiteral("<path d='M9 20.5H5.5a2 2 0 0 1-2-2v-13a2 2 0 0 1 2-2h13a2 2 0 0 1 2 2V9"
                        "M3.5 16l5-5 2.5 2.5'/><circle cx='15.5' cy='7.5' r='1' fill='currentColor' stroke='none'/>"
                        "<circle cx='17' cy='17' r='4.5'/><path d='M17 17v2'/>"
                        "<circle cx='17' cy='14.5' r='.75' fill='currentColor' stroke='none'/>")},
        {QStringLiteral("zoom-in"),
         QStringLiteral("<circle cx='10.5' cy='10.5' r='6.5'/><path d='m15.2 15.2 5.3 5.3M7.5 10.5h6m-3-3v6'/>")},
        {QStringLiteral("zoom-out"),
         QStringLiteral("<circle cx='10.5' cy='10.5' r='6.5'/><path d='m15.2 15.2 5.3 5.3M7.5 10.5h6'/>")},
        {QStringLiteral("previous-image"), QStringLiteral("<path d='m14.5 6.5-5.5 5.5 5.5 5.5'/>")},
        {QStringLiteral("next-image"), QStringLiteral("<path d='m9.5 6.5 5.5 5.5-5.5 5.5'/>")},
        {QStringLiteral("previous-page"),
         QStringLiteral("<path d='M14.5 3.5H6A1.5 1.5 0 0 0 4.5 5v14A1.5 1.5 0 0 0 6 20.5h12"
                        "a1.5 1.5 0 0 0 1.5-1.5V8.5l-5-5v5h5M13.5 12l-3 3 3 3'/>")},
        {QStringLiteral("next-page"),
         QStringLiteral("<path d='M14.5 3.5H6A1.5 1.5 0 0 0 4.5 5v14A1.5 1.5 0 0 0 6 20.5h12"
                        "a1.5 1.5 0 0 0 1.5-1.5V8.5l-5-5v5h5M10.5 12l3 3-3 3'/>")},
        {QStringLiteral("settings"),
         QStringLiteral("<path d='M3.5 7h3m5 0h9M3.5 17h9m5 0h3'/>"
                        "<circle cx='9' cy='7' r='2.5'/><circle cx='15' cy='17' r='2.5'/>")},
        {QStringLiteral("about"),
         QStringLiteral("<circle cx='12' cy='12' r='8.5'/><path d='M12 11v5.5'/>"
                        "<circle cx='12' cy='7.5' r='.9' fill='currentColor' stroke='none'/>")},
        {QStringLiteral("image"),
         QStringLiteral("<rect x='3.5' y='3.5' width='17' height='17' rx='2'/><path d='m3.5 16 5-5 6 6 3-3 3 3'/>"
                        "<circle cx='15.5' cy='8' r='1.5'/>")}};
    return shapes;
}
class OutlineEngine final : public QIconEngine {
  public:
    OutlineEngine(QString name, QPalette palette) : name_(std::move(name)), palette_(std::move(palette)) {}
    QIconEngine *clone() const override {
        return new OutlineEngine(name_, palette_);
    }
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
        const auto scale = p->device()->devicePixelRatioF();
        auto pix = scaledPixmap(rect.size(), mode, state, scale);
        p->drawPixmap(rect, pix);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State, qreal scale) override {
        if (size.isEmpty() || scale <= 0)
            return {};
        const auto color = palette_.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
                                          QPalette::WindowText);
        auto cacheKey = QStringLiteral("ix-outline-v4/%1/%2/%3/%4/%5")
                            .arg(name_, color.name(QColor::HexArgb))
                            .arg(size.width())
                            .arg(size.height())
                            .arg(scale);
        QPixmap result;
        if (QPixmapCache::find(cacheKey, &result))
            return result;
        result = QPixmap((size * scale).expandedTo({1, 1}));
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        auto svg = QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 "
                                  "0 24 24' fill='none' color='%1' stroke='%1' stroke-width='1.65' "
                                  "stroke-linecap='round' stroke-linejoin='round'>%2</svg>")
                       .arg(color.name(), artwork().value(name_));
        QSvgRenderer renderer(svg.toUtf8());
        QPainter painter(&result);
        painter.setOpacity(color.alphaF());
        auto drawing = QRectF(QPointF(0, 0), QSizeF(size));
        if (name_ == QStringLiteral("image") && (size.width() > 32 || size.height() > 32)) {
            drawing.setSize(QSizeF(size.boundedTo({32, 32})));
            drawing.moveCenter(QPointF(size.width() / 2.0, size.height() / 2.0));
        }
        renderer.render(&painter, drawing);
        painter.end();
        QPixmapCache::insert(cacheKey, result);
        return result;
    }

  private:
    QString name_;
    QPalette palette_;
};
} // namespace
QIcon uiIcon(const QString &name, const QPalette &palette) {
    return artwork().contains(name) ? QIcon(new OutlineEngine(name, palette)) : QIcon();
}
QStringList uiIconNames() {
    return artwork().keys();
}
} // namespace ix
