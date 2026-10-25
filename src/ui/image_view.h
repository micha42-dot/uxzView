// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include <QAbstractScrollArea>
#include <QImage>
namespace ix {
class ImageView : public QAbstractScrollArea {
    Q_OBJECT
  public:
    enum class Fit { Original, Window, LargeOnly, Width, Height, ShortSide };
    explicit ImageView(QWidget *parent = nullptr);
    void setImage(QImage image);
    const QImage &image() const {
        return image_;
    }
    void setZoom(double value);
    double zoom() const {
        return zoom_;
    }
    void setFit(Fit value);
    Fit fit() const {
        return fit_;
    }
    Fit browsingFit() const {
        return browsingFit_;
    }
    void setCentered(bool value);
    void setSmooth(bool value);
    void setDisplayCacheEnabled(bool enabled);
    void setInformationOverlay(QString text, Qt::Corner corner);
    QString informationOverlay() const { return information_; }
    QRect informationOverlayRect() const;
    QRect selection() const {
        return selection_;
    }
    void setSelection(QRect rect);
    void setSelectionAspectRatio(QSize ratio);
    QSize selectionAspectRatio() const {
        return aspectRatio_;
    }
    QColor background = Qt::black;
    bool grid = false;
    QImage screenshot() const;
    QRect visibleImageRect() const;
    void zoomToSelection();
    void zoomAt(double value, QPointF viewportPoint);
    bool handleKey(QKeyEvent *event);
  signals:
    void zoomChanged(double);
    void selectionChanged(QRect);
    void navigate(int);
    void toggleFullscreen();
    void commandRequested(const QString &command);
    void pageRequested(int direction);
    void recognitionMenuRequested(QPoint globalPosition);

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

  private:
    QImage image_;
    // A viewport-sized raster avoids resampling the full original on every
    // expose/selection repaint. Original pixels and all overlays stay separate.
    QImage displayCache_;
    qint64 displaySourceKey_ = 0;
    double displayZoom_ = 0;
    QPointF displayOrigin_;
    QSize displayRaster_;
    qreal displayRatio_ = 0;
    QColor displayBackground_;
    bool displaySmooth_ = false, displayCacheEnabled_ = true, displayCacheValid_ = false;
    QString information_;
    Qt::Corner informationCorner_ = Qt::TopLeftCorner;
    QStringList informationLines() const;
    QRect selection_;
    QPoint anchor_, panStart_, scrollStart_;
    QRect movingSelection_;
    QSize selectionRatio_;
    QSize aspectRatio_;
    bool fullscreenClick_ = false, clickDragged_ = false;
    Qt::MouseButton clickButton_ = Qt::NoButton;
    bool moving_ = false;
    double zoom_ = 1;
    Fit fit_ = Fit::LargeOnly, browsingFit_ = Fit::LargeOnly;
    bool centered_ = true, smooth_ = true, selecting_ = false, panning_ = false;
    void updateLayout();
    QPoint imagePoint(QPoint point) const;
    QPointF origin() const;
    bool insideImage(QPointF point) const;
    void updateCursor(QPoint point);
    void centerOn(QPointF imagePoint);
};
} // namespace ix
