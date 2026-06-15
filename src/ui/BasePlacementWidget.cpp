#include "ui/BasePlacementWidget.h"
#include <QBrush>
#include <QPainter>
#include <QPixmap>
#include <QMouseEvent>
#include <algorithm>

static constexpr double HS  = 9.0;
static constexpr double HS2 = HS / 2.0;

static double clampRange(double value, double lo, double hi) {
    if (hi < lo) return lo;
    return std::clamp(value, lo, hi);
}

static const QBrush &checkerBrush() {
    static const QBrush brush = [] {
        QPixmap pm(16, 16);
        pm.fill(QColor(0x2b, 0x2d, 0x31));
        QPainter tp(&pm);
        tp.fillRect(0, 0, 8, 8, QColor(0x37, 0x3a, 0x3f));
        tp.fillRect(8, 8, 8, 8, QColor(0x37, 0x3a, 0x3f));
        return QBrush(pm);
    }();
    return brush;
}

BasePlacementWidget::BasePlacementWidget(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(200, 112);
}

void BasePlacementWidget::setFrame(const QImage &frame) {
    m_frame = frame;
    update();
}

void BasePlacementWidget::setPlacement(float x, float y, float w, float h) {
    m_px = std::clamp(x, 0.f, 0.99f);
    m_py = std::clamp(y, 0.f, 0.99f);
    m_pw = std::clamp(w, 0.01f, 1.f);
    m_ph = std::clamp(h, 0.01f, 1.f);
    if (m_px + m_pw > 1.f) m_px = std::max(0.f, 1.f - m_pw);
    if (m_py + m_ph > 1.f) m_py = std::max(0.f, 1.f - m_ph);
    update();
}

QRectF BasePlacementWidget::placementRect() const {
    return QRectF(m_px * width(), m_py * height(),
                  m_pw * width(), m_ph * height());
}

std::array<QRectF, 4> BasePlacementWidget::handles(const QRectF &r) const {
    return { QRectF(r.left()  - HS2, r.top()    - HS2, HS, HS),
             QRectF(r.right() - HS2, r.top()    - HS2, HS, HS),
             QRectF(r.right() - HS2, r.bottom() - HS2, HS, HS),
             QRectF(r.left()  - HS2, r.bottom() - HS2, HS, HS) };
}

BasePlacementWidget::DragMode BasePlacementWidget::hitTest(QPointF pt) const {
    QRectF r = placementRect();
    auto hs = handles(r);
    DragMode modes[] = { DragMode::ResizeTL, DragMode::ResizeTR,
                         DragMode::ResizeBR, DragMode::ResizeBL };
    for (int i = 0; i < 4; ++i) {
        if (hs[i].contains(pt))
            return modes[i];
    }
    if (r.contains(pt))
        return DragMode::Move;
    return DragMode::None;
}

void BasePlacementWidget::commitDrag(const QRectF &r) {
    if (width() < 1 || height() < 1) return;
    m_px = static_cast<float>(r.left() / width());
    m_py = static_cast<float>(r.top() / height());
    m_pw = static_cast<float>(r.width() / width());
    m_ph = static_cast<float>(r.height() / height());
    emit placementChanged(m_px, m_py, m_pw, m_ph);
    update();
}

void BasePlacementWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    m_dragMode = hitTest(e->position());
    if (m_dragMode == DragMode::None) return;
    m_dragOrigin = e->position();
    m_dragRect = placementRect();
    m_dragMoved = false;
}

void BasePlacementWidget::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragMode == DragMode::None) {
        auto hit = hitTest(e->position());
        switch (hit) {
            case DragMode::Move:      setCursor(Qt::SizeAllCursor);    break;
            case DragMode::ResizeTL:
            case DragMode::ResizeBR:  setCursor(Qt::SizeFDiagCursor);  break;
            case DragMode::ResizeTR:
            case DragMode::ResizeBL:  setCursor(Qt::SizeBDiagCursor);  break;
            default:                  setCursor(Qt::CrossCursor);      break;
        }
        return;
    }

    QPointF d = e->position() - m_dragOrigin;
    if (!m_dragMoved && (std::abs(d.x()) >= 1.0 || std::abs(d.y()) >= 1.0))
        m_dragMoved = true;

    QRectF r = m_dragRect;
    const double minW = width()  * 0.02;
    const double minH = height() * 0.02;

    switch (m_dragMode) {
        case DragMode::Move: {
            const double x = clampRange(m_dragRect.left() + d.x(), 0.0, width() - m_dragRect.width());
            const double y = clampRange(m_dragRect.top()  + d.y(), 0.0, height() - m_dragRect.height());
            r.moveTopLeft({x, y});
            break;
        }
        case DragMode::ResizeTL: {
            const double left = clampRange(m_dragRect.left() + d.x(), 0.0, m_dragRect.right() - minW);
            const double top  = clampRange(m_dragRect.top()  + d.y(), 0.0, m_dragRect.bottom() - minH);
            r = QRectF(QPointF(left, top), m_dragRect.bottomRight());
            break;
        }
        case DragMode::ResizeTR: {
            const double right = clampRange(m_dragRect.right() + d.x(), m_dragRect.left() + minW, width());
            const double top   = clampRange(m_dragRect.top()   + d.y(), 0.0, m_dragRect.bottom() - minH);
            r = QRectF(QPointF(m_dragRect.left(), top), QPointF(right, m_dragRect.bottom()));
            break;
        }
        case DragMode::ResizeBR: {
            const double right  = clampRange(m_dragRect.right()  + d.x(), m_dragRect.left() + minW, width());
            const double bottom = clampRange(m_dragRect.bottom() + d.y(), m_dragRect.top()  + minH, height());
            r = QRectF(m_dragRect.topLeft(), QPointF(right, bottom));
            break;
        }
        case DragMode::ResizeBL: {
            const double left   = clampRange(m_dragRect.left()   + d.x(), 0.0, m_dragRect.right() - minW);
            const double bottom = clampRange(m_dragRect.bottom() + d.y(), m_dragRect.top() + minH, height());
            r = QRectF(QPointF(left, m_dragRect.top()), QPointF(m_dragRect.right(), bottom));
            break;
        }
        case DragMode::None:
            break;
    }

    r = r.normalized();
    if (r.left() < 0.0) r.moveLeft(0.0);
    if (r.top() < 0.0) r.moveTop(0.0);
    if (r.right() > width()) r.moveRight(width());
    if (r.bottom() > height()) r.moveBottom(height());
    commitDrag(r);
}

void BasePlacementWidget::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || m_dragMode == DragMode::None) return;
    if (!m_dragMoved) {
        m_px = 0.f; m_py = 0.f; m_pw = 1.f; m_ph = 1.f;
        emit placementChanged(m_px, m_py, m_pw, m_ph);
        update();
    }
    m_dragMode = DragMode::None;
    m_dragMoved = false;
}

void BasePlacementWidget::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    m_px = 0.f; m_py = 0.f; m_pw = 1.f; m_ph = 1.f;
    emit placementChanged(m_px, m_py, m_pw, m_ph);
    update();
}

void BasePlacementWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x18, 0x19, 0x1b));

    QRectF canvas = rect();
    p.fillRect(canvas, checkerBrush());
    p.setPen(QColor(0x33, 0x36, 0x3b));
    p.drawRect(canvas);

    QRectF r = placementRect();

    if (!m_frame.isNull()) {
        p.setOpacity(0.55);
        p.fillRect(canvas, Qt::black);
        p.setOpacity(1.0);

        QSizeF scaled = m_frame.size().scaled(r.size().toSize(), Qt::KeepAspectRatio);
        QRectF dst(r.center().x() - scaled.width() / 2.0,
                   r.center().y() - scaled.height() / 2.0,
                   scaled.width(), scaled.height());
        p.drawImage(dst, m_frame);
    } else {
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(canvas, Qt::AlignCenter, "No frame — press Play then switch here");
    }

    p.setPen(QPen(QColor(0x2a, 0x8f, 0xa0), 1.5, Qt::SolidLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);

    p.setBrush(QColor(0x4f, 0xc3, 0xd0));
    p.setPen(Qt::NoPen);
    for (const QPointF &corner : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()})
        p.drawRect(QRectF(corner.x() - HS2, corner.y() - HS2, HS, HS));

    p.setPen(QColor(0x55, 0x55, 0x55));
    p.setFont(QFont("Segoe UI", 9));
    p.drawText(QRectF(0, height() - 18, width(), 18),
               Qt::AlignCenter, "Drag inside to move  ·  Drag corners to resize  ·  Double-click to reset");
}
