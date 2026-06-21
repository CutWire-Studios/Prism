#pragma once

#include <QWidget>
#include <QVector>
#include <QPixmap>
#include <QRectF>

struct ClipItem {
    int clipId = 0;
    QPixmap thumbnail;
    QRectF rect;    // normalized [0,1]
    bool selected = false;
};

/// Interactive canvas that lays out multiple clips inside a fixed-aspect logical
/// canvas: select, drag to move, drag corner handles to resize, in normalized
/// [0,1] coordinates. See ui/canvas/CanvasGeometry.h for the shared math.
class TransformCanvasWidget : public QWidget {
    Q_OBJECT

public:
    explicit TransformCanvasWidget(QWidget *parent = nullptr);

    void setCanvasSize(int w, int h);
    void setClips(const QVector<ClipItem> &clips);
    QVector<ClipItem> getClips() const;

    void clear();

signals:
    void clipsChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    struct Handle {
        enum Type { Move, ResizeNW, ResizeNE, ResizeSW, ResizeSE };
        Type type = Move;
        int itemIndex = 0;
        QRectF bounds;
    };

    void drawCheckerboard(QPainter &p);
    Handle getHandleAt(const QPointF &pos);
    int getItemAt(const QPointF &pos);
    // Letterboxed pixel rect the logical canvas occupies inside the widget.
    QRectF canvasRect() const;
    QRectF itemToScreen(const QRectF &itemRect);
    QRectF screenToItem(const QRectF &screenRect);
    QPointF screenToItem(const QPointF &screenPos);

    int m_canvasW = 1280;
    int m_canvasH = 720;
    QVector<ClipItem> m_clips;

    int m_draggedItemIndex = -1;
    Handle::Type m_dragType = Handle::Move;
    QPointF m_dragStart;
    QRectF m_dragOriginalRect;
};
