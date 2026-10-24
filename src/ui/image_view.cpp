// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "image_view.h"
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QStyle>
#include <QWheelEvent>
#include <cmath>
namespace ix {
ImageView::ImageView(QWidget *p) : QAbstractScrollArea(p) {
    setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
}
void ImageView::setImage(QImage image) {
    displaySourceKey_ = 0;
    image_ = std::move(image);
    if (image_.isNull())
        displayCache_ = {};
    selection_ = {};
    selectionRatio_ = {};
    selecting_ = panning_ = moving_ = false;
    fullscreenClick_ = false;
    if (property("hidePointer").toBool())
        viewport()->setCursor(Qt::BlankCursor);
    else
        viewport()->unsetCursor();
    updateLayout();
    viewport()->update();
    emit selectionChanged({});
}
void ImageView::setZoom(double v) {
    fit_ = Fit::Original;
    zoom_ = std::clamp(v, .001, 64.0);
    updateLayout();
    viewport()->update();
}
void ImageView::setFit(Fit f) {
    browsingFit_ = fit_ = f;
    if (f == Fit::Original)
        zoom_ = 1;
    updateLayout();
    viewport()->update();
}
void ImageView::setCentered(bool v) {
    centered_ = v;
    viewport()->update();
}
void ImageView::setSmooth(bool v) {
    smooth_ = v;
    viewport()->update();
}
void ImageView::setDisplayCacheEnabled(bool enabled) {
    displayCacheEnabled_ = enabled;
    if (!enabled) {
        displayCache_ = {};
        displaySourceKey_ = 0;
    }
    viewport()->update();
}
void ImageView::setSelection(QRect r) {
    auto clipped = r.intersected(image_.rect());
    if (clipped == selection_)
        return;
    // Selection changes only affect the outlines, not the image between them.
    // Leave resampling of large/high-depth images to the small damaged strips.
    auto outline = [&](QRect selection) {
        if (selection.isEmpty())
            return QRegion();
        const QRectF rect(origin() + QPointF(selection.topLeft()) * zoom_,
                          QSizeF(selection.size()) * zoom_);
        const auto aligned = rect.toAlignedRect();
        return (QRegion(aligned.adjusted(-2, -2, 2, 2)) -
                QRegion(aligned.adjusted(3, 3, -3, -3))) & viewport()->rect();
    };
    const auto damaged = outline(selection_) | outline(clipped);
    if (clipped.size() != selection_.size())
        selectionRatio_ = clipped.size();
    selection_ = clipped;
    if (!damaged.isEmpty())
        viewport()->update(damaged);
    emit selectionChanged(selection_);
}
void ImageView::setSelectionAspectRatio(QSize ratio) {
    aspectRatio_ = ratio.width() > 0 && ratio.height() > 0 ? ratio : QSize();
    if (!aspectRatio_.isValid() || image_.isNull())
        return;
    const auto bounds = selection_.isEmpty() ? image_.rect() : selection_;
    const double scale = std::min(double(bounds.width()) / ratio.width(),
                                  double(bounds.height()) / ratio.height());
    const QSize size(std::clamp(qRound(ratio.width() * scale), 1, bounds.width()),
                     std::clamp(qRound(ratio.height() * scale), 1, bounds.height()));
    setSelection(QRect(bounds.x() + (bounds.width() - size.width()) / 2,
                       bounds.y() + (bounds.height() - size.height()) / 2, size.width(), size.height()));
}
QPointF ImageView::origin() const {
    return QPointF(centered_ ? std::max(0.0, (viewport()->width() - image_.width() * zoom_) / 2) : 0,
                   centered_ ? std::max(0.0, (viewport()->height() - image_.height() * zoom_) / 2) : 0) -
           QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
}
void ImageView::updateLayout() {
    if (image_.isNull()) {
        horizontalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setRange(0, 0);
        return;
    }
    const bool fitBoth = fit_ == Fit::Window || fit_ == Fit::LargeOnly;
    if (fitBoth) {
        horizontalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setRange(0, 0);
    }
    if (fit_ != Fit::Original) {
        // The theoretical maximum can exceed the current client while Qt is
        // restoring frames/scrollbars. Fit the actual viewport; a later resize
        // recalculates after old scrollbars disappear.
        const auto available = fitBoth ? viewport()->size() : maximumViewportSize();
        double w = double(std::max(1, available.width())) / image_.width(),
               h = double(std::max(1, available.height())) / image_.height();
        zoom_ = fit_ == Fit::Width       ? w
                : fit_ == Fit::Height    ? h
                : fit_ == Fit::ShortSide ? std::max(w, h)
                                         : std::min(w, h);
        if (fit_ == Fit::LargeOnly)
            zoom_ = std::min(1.0, zoom_);
    }
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setPageStep(viewport()->height());
    if (fitBoth) {
        // Keep ranges empty while Qt finishes removing any old scrollbars;
        // the resulting viewport resize recomputes the fitted scale.
        horizontalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setRange(0, 0);
    } else {
        // Division followed by multiplication can land just above an integer
        // pixel boundary; do not turn floating-point noise into a scrollbar.
        auto extent = [](double value) { return int(std::ceil(value - 1e-7)); };
        horizontalScrollBar()->setRange(0, std::max(0, extent(image_.width() * zoom_) - viewport()->width()));
        verticalScrollBar()->setRange(0, std::max(0, extent(image_.height() * zoom_) - viewport()->height()));
    }
    emit zoomChanged(zoom_);
}
void ImageView::setInformationOverlay(QString text, Qt::Corner corner) {
    if (information_ == text && informationCorner_ == corner)
        return;
    const auto old = informationOverlayRect();
    information_ = std::move(text);
    informationCorner_ = corner;
    viewport()->update(old.united(informationOverlayRect()));
}
QStringList ImageView::informationLines() const {
    if (information_.isEmpty() || viewport()->width() < 48 || viewport()->height() < 48)
        return {};
    const QFontMetrics metrics(font());
    auto lines = information_.split(QLatin1Char('\n'));
    const int maximum = (viewport()->height() - 40) / metrics.lineSpacing();
    if (maximum < 1)
        return {};
    if (lines.size() > maximum) {
        lines = lines.mid(0, maximum);
        lines.last() = QStringLiteral("…");
    }
    for (auto &line : lines)
        line = metrics.elidedText(line, Qt::ElideRight, viewport()->width() - 40);
    return lines;
}
QRect ImageView::informationOverlayRect() const {
    const auto lines = informationLines();
    if (lines.isEmpty())
        return {};
    const QFontMetrics metrics(font());
    int width = 0;
    for (const auto &line : lines)
        width = std::max(width, metrics.horizontalAdvance(line));
    const QSize size(width + 16, lines.size() * metrics.lineSpacing() + 16);
    const bool right = informationCorner_ == Qt::TopRightCorner || informationCorner_ == Qt::BottomRightCorner;
    const bool bottom = informationCorner_ == Qt::BottomLeftCorner || informationCorner_ == Qt::BottomRightCorner;
    return QRect(QPoint(right ? viewport()->width() - size.width() - 12 : 12,
                        bottom ? viewport()->height() - size.height() - 12 : 12), size);
}
void ImageView::paintEvent(QPaintEvent *event) {
    QPainter p(viewport());
    if (image_.isNull()) {
        p.fillRect(viewport()->rect(), background);
        return;
    }
    QPointF start = origin();
    QRectF source((QPointF(0, 0) - start) / zoom_, QSizeF(viewport()->size()) / zoom_);
    source = source.intersected(QRectF(image_.rect()));
    QRectF target(start + source.topLeft() * zoom_, source.size() * zoom_);
    auto drawBase = [&](QPainter &painter) {
        painter.fillRect(viewport()->rect(), background);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth_);
        painter.drawImage(target, image_, source);
    };
    const auto ratio = viewport()->devicePixelRatioF();
    const QSize raster(qCeil(viewport()->width() * ratio), qCeil(viewport()->height() * ratio));
    const auto transform = p.deviceTransform();
    auto equal = [](double a, double b) { return std::abs(a - b) < 1e-7; };
    // Respect redirected/high-resolution rendering and fractional device origins:
    // a cached viewport must use exactly the same physical pixel grid.
    const bool aligned = transform.isAffine() && equal(transform.m11(), ratio) && equal(transform.m22(), ratio) &&
        equal(transform.m12(), 0) && equal(transform.m21(), 0) &&
        equal(transform.dx(), std::round(transform.dx())) && equal(transform.dy(), std::round(transform.dy()));
    const qint64 rasterPixels = qint64(raster.width()) * raster.height();
    const bool cacheable = displayCacheEnabled_ && aligned && background.alpha() == 255 && zoom_ < 1 &&
        rasterPixels > 0 && rasterPixels <= 32 * 1024 * 1024 / 4 &&
        qint64(image_.width()) * image_.height() > rasterPixels * 2 &&
        image_.width() * zoom_ <= viewport()->width() + 1e-7 &&
        image_.height() * zoom_ <= viewport()->height() + 1e-7 &&
        image_.pixelFormat().typeInterpretation() != QPixelFormat::FloatingPoint;
    if (cacheable) {
        const auto key = image_.cacheKey();
        const bool same = displayRaster_ == raster && displayRatio_ == ratio && displaySourceKey_ == key &&
            displayZoom_ == zoom_ && displayOrigin_ == start && displayBackground_ == background && displaySmooth_ == smooth_;
        qint64 damagedPixels = 0;
        if (same && !displayCacheValid_)
            for (const auto &rect : event->region())
                damagedPixels += qint64(rect.width()) * rect.height();
        // First presentation stays on the direct path. Only another large
        // repaint pays for a reusable raster; tiny selection strips remain cheap.
        if (!same) {
            displayCacheValid_ = false;
            displaySourceKey_ = key;
            displayZoom_ = zoom_;
            displayOrigin_ = start;
            displayRaster_ = raster;
            displayRatio_ = ratio;
            displayBackground_ = background;
            displaySmooth_ = smooth_;
        } else if (!displayCacheValid_ && damagedPixels >= qint64(viewport()->width()) * viewport()->height() / 2) {
            if (displayCache_.size() != raster)
                displayCache_ = QImage(raster, QImage::Format_RGB32);
            if (!displayCache_.isNull()) {
                displayCache_.setDevicePixelRatio(ratio);
                QPainter base(&displayCache_);
                drawBase(base);
                displayCacheValid_ = true;
            }
        }
        if (displayCacheValid_)
            p.drawImage(QPointF(), displayCache_);
        else
            drawBase(p);
    } else {
        displayCache_ = {};
        displaySourceKey_ = 0;
        drawBase(p);
    }
    if (grid) {
        p.setPen(QPen(QColor(180, 180, 180), 1, Qt::DotLine));
        for (int i = 1; i < 3; ++i) {
            double x = start.x() + image_.width() * zoom_ * i / 3,
                   y = start.y() + image_.height() * zoom_ * i / 3;
            p.drawLine(QPointF(x, start.y()), QPointF(x, start.y() + image_.height() * zoom_));
            p.drawLine(QPointF(start.x(), y), QPointF(start.x() + image_.width() * zoom_, y));
        }
    }
    if (!selection_.isEmpty()) {
        QRectF r(start + QPointF(selection_.topLeft()) * zoom_, QSizeF(selection_.size()) * zoom_);
        p.setPen(QPen(Qt::black, 1));
        p.drawRect(r);
        p.setPen(QPen(Qt::white, 1, Qt::DashLine));
        p.drawRect(r);
    }
    const auto info = informationOverlayRect();
    if (!info.isEmpty()) {
        p.fillRect(info, QColor(0, 0, 0, 190));
        p.setPen(Qt::white);
        p.drawText(info.adjusted(8, 8, -8, -8), Qt::AlignLeft | Qt::AlignTop,
                   informationLines().join(QLatin1Char('\n')));
    }
}
void ImageView::resizeEvent(QResizeEvent *e) {
    QAbstractScrollArea::resizeEvent(e);
    updateLayout();
}
QPoint ImageView::imagePoint(QPoint p) const {
    auto v = (QPointF(p) - origin()) / zoom_;
    return QPoint(std::clamp(int(std::floor(v.x())), 0, std::max(0, image_.width() - 1)),
                  std::clamp(int(std::floor(v.y())), 0, std::max(0, image_.height() - 1)));
}

bool ImageView::insideImage(QPointF point) const {
    return !image_.isNull() && QRectF(origin(), QSizeF(image_.size()) * zoom_).contains(point);
}
void ImageView::updateCursor(QPoint point) {
    if (property("hidePointer").toBool()) {
        viewport()->setCursor(Qt::BlankCursor);
        return;
    }
    if (selecting_) {
        viewport()->setCursor(Qt::CrossCursor);
        return;
    }
    if (panning_ || moving_) {
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (aspectRatio_.isValid() && insideImage(point)) {
        viewport()->setCursor(Qt::CrossCursor);
        return;
    }
    if (insideImage(point) && !selection_.isEmpty() && selection_.contains(imagePoint(point))) {
        // An original vector-drawn magnifying cursor, independent of OS themes.
        static const QCursor magnifier = [] {
            QPixmap pixmap(28, 28);
            pixmap.fill(Qt::transparent);
            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::white, 5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(17, 17, 24, 24);
            p.drawEllipse(QRectF(3, 3, 16, 16));
            p.setPen(QPen(Qt::black, 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(17, 17, 24, 24);
            p.setBrush(Qt::white);
            p.drawEllipse(QRectF(3, 3, 16, 16));
            p.drawLine(7, 11, 15, 11);
            p.drawLine(11, 7, 11, 15);
            p.end();
            return QCursor(pixmap, 11, 11);
        }();
        viewport()->setCursor(magnifier);
    } else
        viewport()->unsetCursor();
}
void ImageView::centerOn(QPointF point) {
    const auto offset = origin() + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
    horizontalScrollBar()->setValue(qRound(offset.x() + point.x() * zoom_ - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(qRound(offset.y() + point.y() * zoom_ - viewport()->height() / 2.0));
}
void ImageView::zoomAt(double value, QPointF point) {
    if (image_.isNull())
        return;
    const auto pixel = (point - origin()) / zoom_;
    setZoom(value);
    const auto offset = origin() + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
    horizontalScrollBar()->setValue(qRound(offset.x() + pixel.x() * zoom_ - point.x()));
    verticalScrollBar()->setValue(qRound(offset.y() + pixel.y() * zoom_ - point.y()));
}
void ImageView::zoomToSelection() {
    const auto selected = selection_;
    if (selected.isEmpty())
        return;
    // Account for both scrollbars when a detail of a larger image is expanded.
    const int extent = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
    const QSize available = maximumViewportSize() - QSize(extent, extent);
    const double factor = std::min(double(std::max(1, available.width())) / selected.width(),
                                   double(std::max(1, available.height())) / selected.height());
    setSelection({});
    setZoom(std::max(zoom_, factor));
    centerOn(QPointF(selected.x() + selected.width() / 2.0, selected.y() + selected.height() / 2.0));
}
void ImageView::mousePressEvent(QMouseEvent *e) {
    if (image_.isNull())
        return;
    setFocus(Qt::MouseFocusReason);
    const auto point = e->position().toPoint();
    fullscreenClick_ = window()->isFullScreen() && e->modifiers() == Qt::NoModifier &&
                       (e->button() == Qt::LeftButton || e->button() == Qt::RightButton);
    clickButton_ = e->button();
    clickDragged_ = false;
    panStart_ = point;
    if (e->button() == Qt::LeftButton) {
        if (!insideImage(point)) {
            setSelection({});
            return;
        }
        if (!fullscreenClick_ && (!aspectRatio_.isValid() || e->modifiers().testFlag(Qt::ControlModifier)) &&
            !selection_.isEmpty() && selection_.contains(imagePoint(point))) {
            if (e->modifiers().testFlag(Qt::ControlModifier))
                setSelection({});
            else
                zoomToSelection();
            updateCursor(point);
            e->accept();
            return;
        }
        anchor_ = imagePoint(point);
        panStart_ = point;
        selecting_ = true;
        setSelection({});
    } else if (e->button() == Qt::RightButton && !selection_.isEmpty() && insideImage(point) &&
               selection_.contains(imagePoint(point))) {
        moving_ = true;
        movingSelection_ = selection_;
        anchor_ = imagePoint(point);
    } else if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        panning_ = true;
        panStart_ = point;
        scrollStart_ = {horizontalScrollBar()->value(), verticalScrollBar()->value()};
    }
    updateCursor(point);
    e->accept();
}
void ImageView::mouseMoveEvent(QMouseEvent *e) {
    const auto point = e->position().toPoint();
    if ((point - panStart_).manhattanLength() >= 3)
        clickDragged_ = true;
    if (selecting_ && clickDragged_) {
        const auto end = imagePoint(point);
        auto rect = QRect(anchor_, end).normalized();
        if (aspectRatio_.isValid()) {
            const int sx = end.x() < anchor_.x() ? -1 : 1, sy = end.y() < anchor_.y() ? -1 : 1;
            const int availableWidth = sx > 0 ? image_.width() - anchor_.x() : anchor_.x() + 1;
            const int availableHeight = sy > 0 ? image_.height() - anchor_.y() : anchor_.y() + 1;
            double scale = std::max(double(rect.width()) / aspectRatio_.width(),
                                     double(rect.height()) / aspectRatio_.height());
            scale = std::min({scale, double(availableWidth) / aspectRatio_.width(),
                              double(availableHeight) / aspectRatio_.height()});
            const int width = std::clamp(qRound(aspectRatio_.width() * scale), 1, availableWidth);
            const int height = std::clamp(qRound(aspectRatio_.height() * scale), 1, availableHeight);
            rect = QRect(sx > 0 ? anchor_.x() : anchor_.x() - width + 1,
                         sy > 0 ? anchor_.y() : anchor_.y() - height + 1, width, height);
        }
        setSelection(rect);
    }
    if (moving_) {
        auto delta = imagePoint(point) - anchor_;
        if (e->modifiers().testFlag(Qt::ControlModifier))
            delta.setX(0);
        if (e->modifiers().testFlag(Qt::ShiftModifier))
            delta.setY(0);
        auto p = movingSelection_.topLeft() + delta;
        p.setX(std::clamp(p.x(), 0, image_.width() - movingSelection_.width()));
        p.setY(std::clamp(p.y(), 0, image_.height() - movingSelection_.height()));
        setSelection(QRect(p, movingSelection_.size()));
    }
    if (panning_) {
        auto delta = point - panStart_;
        horizontalScrollBar()->setValue(scrollStart_.x() - delta.x());
        verticalScrollBar()->setValue(scrollStart_.y() - delta.y());
    }
    updateCursor(point);
}
void ImageView::mouseReleaseEvent(QMouseEvent *e) {
    mouseMoveEvent(e);
    const bool navigateClick = fullscreenClick_ && !clickDragged_ && e->button() == clickButton_;
    const bool contextClick = !image_.isNull() && !window()->isFullScreen() && !clickDragged_ &&
                              e->button() == Qt::RightButton && clickButton_ == Qt::RightButton;
    fullscreenClick_ = false;
    clickButton_ = Qt::NoButton;
    selecting_ = panning_ = moving_ = false;
    updateCursor(e->position().toPoint());
    if (navigateClick)
        emit navigate(e->button() == Qt::LeftButton ? -1 : 1);
    else if (contextClick)
        emit recognitionMenuRequested(e->globalPosition().toPoint());
    e->accept();
}
void ImageView::contextMenuEvent(QContextMenuEvent *event) {
    if (event->reason() == QContextMenuEvent::Keyboard && !image_.isNull())
        emit recognitionMenuRequested(viewport()->mapToGlobal(viewport()->rect().center()));
    // Mouse menus are dispatched on release so a right drag keeps moving/panning.
    event->accept();
}
void ImageView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (window()->isFullScreen())
        mousePressEvent(event);
    else if (event->button() == Qt::LeftButton) {
        fullscreenClick_ = false;
        selecting_ = panning_ = moving_ = false;
        emit toggleFullscreen();
    }
}
void ImageView::wheelEvent(QWheelEvent *e) {
    auto delta = e->angleDelta();
    if (delta.isNull())
        delta = e->pixelDelta();
    if (delta.y() == 0) {
        if (delta.x() != 0)
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x() / 3);
        e->accept();
        return;
    }
    if (e->modifiers().testFlag(Qt::ControlModifier))
        zoomAt(zoom_ * (delta.y() > 0 ? 1.25 : .8), e->position());
    else if (e->modifiers().testFlag(Qt::ShiftModifier))
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.y() / 3);
    else if (verticalScrollBar()->maximum() > 0)
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y() / 3);
    else
        emit navigate(delta.y() > 0 ? -1 : 1);
    updateCursor(e->position().toPoint());
    e->accept();
}
bool ImageView::handleKey(QKeyEvent *e) {
    if (image_.isNull())
        return false;
    const auto mods =
        e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool ctrl = mods.testFlag(Qt::ControlModifier), shift = mods.testFlag(Qt::ShiftModifier),
               alt = mods.testFlag(Qt::AltModifier);
    const int key = e->key();
    if (mods == Qt::ShiftModifier && key == Qt::Key_T) {
        emit commandRequested(QStringLiteral("focusZoom"));
        return true;
    }
    if (mods == Qt::ShiftModifier && key == Qt::Key_P) {
        emit commandRequested(QStringLiteral("copyFilename"));
        return true;
    }
    if (mods == Qt::ControlModifier && key == Qt::Key_N) {
        emit commandRequested(QStringLiteral("newWindow"));
        return true;
    }
    if (ctrl && !alt && !mods.testFlag(Qt::MetaModifier) && key >= Qt::Key_0 && key <= Qt::Key_9) {
        int count = key == Qt::Key_0 ? 10 : key - Qt::Key_0;
        emit navigate(count * (shift ? -1 : 1));
        return true;
    }
    if (key == Qt::Key_A && mods == Qt::ControlModifier) {
        setSelection(selection_ == image_.rect() ? QRect() : image_.rect());
        return true;
    }
    if ((key == Qt::Key_Space || key == Qt::Key_Backspace) && mods == Qt::ControlModifier) {
        emit navigate(key == Qt::Key_Space ? 5 : -5);
        return true;
    }
    if (key == Qt::Key_PageDown || key == Qt::Key_PageUp) {
        if (alt || shift || mods.testFlag(Qt::MetaModifier))
            return false;
        int direction = key == Qt::Key_PageDown ? 1 : -1;
        if (ctrl)
            emit pageRequested(direction);
        else if (verticalScrollBar()->maximum() > 0)
            verticalScrollBar()->setValue(verticalScrollBar()->value() +
                                          direction * verticalScrollBar()->pageStep());
        else
            emit navigate(direction);
        return true;
    }
    if (key == Qt::Key_Home || key == Qt::Key_End) {
        if (alt || mods.testFlag(Qt::MetaModifier))
            return false;
        if (shift && !ctrl) {
            emit commandRequested(key == Qt::Key_Home ? QStringLiteral("firstPage")
                                                      : QStringLiteral("lastPage"));
            return true;
        }
        if (ctrl || horizontalScrollBar()->maximum() == 0)
            emit commandRequested(key == Qt::Key_Home ? QStringLiteral("first") : QStringLiteral("last"));
        else
            horizontalScrollBar()->setValue(key == Qt::Key_Home ? 0 : horizontalScrollBar()->maximum());
        return true;
    }
    const bool horizontal = key == Qt::Key_Left || key == Qt::Key_Right;
    const bool vertical = key == Qt::Key_Up || key == Qt::Key_Down;
    if (!horizontal && !vertical)
        return false;
    if (mods.testFlag(Qt::MetaModifier) || (ctrl && shift) || (alt && (ctrl || shift)))
        return false;
    const int direction = key == Qt::Key_Right || key == Qt::Key_Down ? 1 : -1;
    const bool scrollable = horizontalScrollBar()->maximum() > 0 || verticalScrollBar()->maximum() > 0;
    if (!selection_.isEmpty() && (ctrl || shift || alt || !scrollable)) {
        QRect next = selection_;
        if (ctrl || (alt && aspectRatio_.isValid())) {
            const auto ratio = aspectRatio_.isValid() ? aspectRatio_
                               : selectionRatio_.isValid() ? selectionRatio_ : selection_.size();
            QSize size = next.size();
            if (horizontal) {
                size.setWidth(std::max(1, size.width() + direction));
                size.setHeight(std::max(1, qRound(double(size.width()) * ratio.height() / ratio.width())));
            } else {
                size.setHeight(std::max(1, size.height() + direction));
                size.setWidth(std::max(1, qRound(double(size.height()) * ratio.width() / ratio.height())));
            }
            if (size.width() <= image_.width() - next.x() && size.height() <= image_.height() - next.y()) {
                next.setSize(size);
                setSelection(next);
                selectionRatio_ = ratio;
            }
            return true;
        }
        if (alt) {
            if (horizontal)
                next.setWidth(std::clamp(next.width() + direction, 1, image_.width() - next.x()));
            else
                next.setHeight(std::clamp(next.height() + direction, 1, image_.height() - next.y()));
        } else {
            auto p = next.topLeft();
            if (horizontal)
                p.rx() += direction;
            else
                p.ry() += direction;
            p.setX(std::clamp(p.x(), 0, image_.width() - next.width()));
            p.setY(std::clamp(p.y(), 0, image_.height() - next.height()));
            next.moveTopLeft(p);
        }
        setSelection(next);
        return true;
    }
    if (shift || alt)
        return false;
    auto *bar = horizontal ? horizontalScrollBar() : verticalScrollBar();
    if (bar->maximum() > 0)
        bar->setValue(bar->value() + direction * 25);
    else if (horizontal && selection_.isEmpty())
        emit navigate(direction * (ctrl ? 5 : 1));
    return true;
}
void ImageView::keyPressEvent(QKeyEvent *e) {
    if (handleKey(e)) {
        e->accept();
        return;
    }
    QAbstractScrollArea::keyPressEvent(e);
}
QRect ImageView::visibleImageRect() const {
    if (image_.isNull())
        return {};
    return QRectF(-origin() / zoom_, QSizeF(viewport()->size()) / zoom_)
        .toAlignedRect()
        .intersected(image_.rect());
}
QImage ImageView::screenshot() const {
    return viewport()->grab().toImage();
}
} // namespace ix
