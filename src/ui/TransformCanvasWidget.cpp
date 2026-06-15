#include "ui/TransformCanvasWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>

TransformCanvasWidget::TransformCanvasWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setFocusPolicy(Qt::StrongFocus);
}

void TransformCanvasWidget::setCanvasSize(int w, int h) {
    m_canvasW = w;
    m_canvasH = h;
    update();
}

void TransformCanvasWidget::setClips(const QVector<ClipItem> &clips) {
    m_clips = clips;
    update();
}

QVector<ClipItem> TransformCanvasWidget::getClips() const {
    return m_clips;
}

void TransformCanvasWidget::clear() {
    m_clips.clear();
    update();
}

void TransformCanvasWidget::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    drawCheckerboard(p);

    qreal aspectRatio = static_cast<qreal>(m_canvasW) / m_canvasH;
    qreal widgetAspect = static_cast<qreal>(width()) / height();

    QRectF canvasRect;
    if (aspectRatio > widgetAspect) {
        qreal w = width() - 20;
        qreal h = w / aspectRatio;
        canvasRect = QRectF(10, (height() - h) / 2, w, h);
    } else {
        qreal h = height() - 20;
        qreal w = h * aspectRatio;
        canvasRect = QRectF((width() - w) / 2, 10, w, h);
    }

    p.setPen(QPen(QColor(100, 100, 100), 1));
    p.setBrush(QColor(20, 20, 20));
    p.drawRect(canvasRect);

    for (int i = 0; i < m_clips.size(); ++i) {
        const auto &clip = m_clips[i];
        QRectF screenRect = itemToScreen(clip.rect);

        if (!clip.thumbnail.isNull()) {
            p.drawPixmap(screenRect.toRect(), clip.thumbnail);
        }

        QColor borderColor = clip.selected ? QColor(0, 200, 255) : QColor(100, 100, 100);
        p.setPen(QPen(borderColor, clip.selected ? 3 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(screenRect);

        if (clip.selected) {
            qreal handleSize = 8;
            p.setPen(Qt::NoPen);
            p.setBrush(borderColor);

            QRectF r = screenRect;
            p.drawEllipse(QRectF(r.left() - handleSize/2, r.top() - handleSize/2, handleSize, handleSize));
            p.drawEllipse(QRectF(r.right() - handleSize/2, r.top() - handleSize/2, handleSize, handleSize));
            p.drawEllipse(QRectF(r.left() - handleSize/2, r.bottom() - handleSize/2, handleSize, handleSize));
            p.drawEllipse(QRectF(r.right() - handleSize/2, r.bottom() - handleSize/2, handleSize, handleSize));
        }
    }
}

void TransformCanvasWidget::drawCheckerboard(QPainter &p) {
    const int squareSize = 20;
    for (int y = 0; y < height(); y += squareSize) {
        for (int x = 0; x < width(); x += squareSize) {
            int col = (x / squareSize) + (y / squareSize);
            QColor color = (col % 2 == 0) ? QColor(30, 30, 30) : QColor(40, 40, 40);
            p.fillRect(x, y, squareSize, squareSize, color);
        }
    }
}

QRectF TransformCanvasWidget::itemToScreen(const QRectF &itemRect) {
    qreal aspectRatio = static_cast<qreal>(m_canvasW) / m_canvasH;
    qreal widgetAspect = static_cast<qreal>(width()) / height();

    QRectF canvasRect;
    if (aspectRatio > widgetAspect) {
        qreal w = width() - 20;
        qreal h = w / aspectRatio;
        canvasRect = QRectF(10, (height() - h) / 2, w, h);
    } else {
        qreal h = height() - 20;
        qreal w = h * aspectRatio;
        canvasRect = QRectF((width() - w) / 2, 10, w, h);
    }

    return QRectF(
        canvasRect.left() + itemRect.left() * canvasRect.width(),
        canvasRect.top() + itemRect.top() * canvasRect.height(),
        itemRect.width() * canvasRect.width(),
        itemRect.height() * canvasRect.height()
    );
}

QRectF TransformCanvasWidget::screenToItem(const QRectF &screenRect) {
    qreal aspectRatio = static_cast<qreal>(m_canvasW) / m_canvasH;
    qreal widgetAspect = static_cast<qreal>(width()) / height();

    QRectF canvasRect;
    if (aspectRatio > widgetAspect) {
        qreal w = width() - 20;
        qreal h = w / aspectRatio;
        canvasRect = QRectF(10, (height() - h) / 2, w, h);
    } else {
        qreal h = height() - 20;
        qreal w = h * aspectRatio;
        canvasRect = QRectF((width() - w) / 2, 10, w, h);
    }

    return QRectF(
        (screenRect.left() - canvasRect.left()) / canvasRect.width(),
        (screenRect.top() - canvasRect.top()) / canvasRect.height(),
        screenRect.width() / canvasRect.width(),
        screenRect.height() / canvasRect.height()
    );
}

QPointF TransformCanvasWidget::screenToItem(const QPointF &screenPos) {
    qreal aspectRatio = static_cast<qreal>(m_canvasW) / m_canvasH;
    qreal widgetAspect = static_cast<qreal>(width()) / height();

    QRectF canvasRect;
    if (aspectRatio > widgetAspect) {
        qreal w = width() - 20;
        qreal h = w / aspectRatio;
        canvasRect = QRectF(10, (height() - h) / 2, w, h);
    } else {
        qreal h = height() - 20;
        qreal w = h * aspectRatio;
        canvasRect = QRectF((width() - w) / 2, 10, w, h);
    }

    return QPointF(
        (screenPos.x() - canvasRect.left()) / canvasRect.width(),
        (screenPos.y() - canvasRect.top()) / canvasRect.height()
    );
}

TransformCanvasWidget::Handle TransformCanvasWidget::getHandleAt(const QPointF &pos) {
    const qreal handleSize = 12;

    for (int i = 0; i < m_clips.size(); ++i) {
        if (!m_clips[i].selected) continue;

        QRectF screenRect = itemToScreen(m_clips[i].rect);
        QRectF nwHandle(screenRect.left() - handleSize/2, screenRect.top() - handleSize/2, handleSize, handleSize);
        QRectF neHandle(screenRect.right() - handleSize/2, screenRect.top() - handleSize/2, handleSize, handleSize);
        QRectF swHandle(screenRect.left() - handleSize/2, screenRect.bottom() - handleSize/2, handleSize, handleSize);
        QRectF seHandle(screenRect.right() - handleSize/2, screenRect.bottom() - handleSize/2, handleSize, handleSize);

        if (nwHandle.contains(pos)) return {Handle::ResizeNW, i, screenRect};
        if (neHandle.contains(pos)) return {Handle::ResizeNE, i, screenRect};
        if (swHandle.contains(pos)) return {Handle::ResizeSW, i, screenRect};
        if (seHandle.contains(pos)) return {Handle::ResizeSE, i, screenRect};

        if (screenRect.contains(pos)) return {Handle::Move, i, screenRect};
    }

    return {Handle::Move, -1, {}};
}

int TransformCanvasWidget::getItemAt(const QPointF &pos) {
    for (int i = m_clips.size() - 1; i >= 0; --i) {
        QRectF screenRect = itemToScreen(m_clips[i].rect);
        if (screenRect.contains(pos)) return i;
    }
    return -1;
}

void TransformCanvasWidget::mousePressEvent(QMouseEvent *event) {
    QPointF pos = event->pos();
    Handle handle = getHandleAt(pos);

    if (event->modifiers() & Qt::ControlModifier) {
        int idx = getItemAt(pos);
        if (idx >= 0) {
            m_clips[idx].selected = !m_clips[idx].selected;
            update();
        }
    } else {
        for (auto &clip : m_clips) clip.selected = false;

        int idx = getItemAt(pos);
        if (idx >= 0) {
            m_clips[idx].selected = true;
            handle = getHandleAt(pos);
        }

        m_draggedItemIndex = handle.itemIndex;
        m_dragType = handle.type;
        m_dragStart = pos;
        if (m_draggedItemIndex >= 0) {
            m_dragOriginalRect = m_clips[m_draggedItemIndex].rect;
        }

        update();
    }
}

void TransformCanvasWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_draggedItemIndex < 0) return;

    QPointF delta = event->pos() - m_dragStart;
    QPointF itemDelta = screenToItem(QPointF(delta.x(), delta.y())) - screenToItem(QPointF(0, 0));

    ClipItem &clip = m_clips[m_draggedItemIndex];
    QRectF newRect = m_dragOriginalRect;

    if (m_dragType == Handle::Move) {
        newRect.translate(itemDelta.x(), itemDelta.y());
    } else if (m_dragType == Handle::ResizeNW) {
        newRect.setLeft(newRect.left() + itemDelta.x());
        newRect.setTop(newRect.top() + itemDelta.y());
    } else if (m_dragType == Handle::ResizeNE) {
        newRect.setRight(newRect.right() + itemDelta.x());
        newRect.setTop(newRect.top() + itemDelta.y());
    } else if (m_dragType == Handle::ResizeSW) {
        newRect.setLeft(newRect.left() + itemDelta.x());
        newRect.setBottom(newRect.bottom() + itemDelta.y());
    } else if (m_dragType == Handle::ResizeSE) {
        newRect.setRight(newRect.right() + itemDelta.x());
        newRect.setBottom(newRect.bottom() + itemDelta.y());
    }

    newRect = newRect.intersected(QRectF(0, 0, 1, 1));
    if (newRect.width() > 0.02 && newRect.height() > 0.02) {
        clip.rect = newRect;
        emit clipsChanged();
        update();
    }
}

void TransformCanvasWidget::mouseReleaseEvent(QMouseEvent *event) {
    m_draggedItemIndex = -1;
}

void TransformCanvasWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    int idx = getItemAt(event->pos());
    if (idx >= 0) {
        m_clips[idx].rect = QRectF(0, 0, 1, 1);
        emit clipsChanged();
        update();
    }
}
