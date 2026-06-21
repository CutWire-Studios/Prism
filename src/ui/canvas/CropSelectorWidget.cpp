#include "ui/canvas/CropSelectorWidget.h"
#include "ui/canvas/CanvasGeometry.h"
#include <QBrush>
#include <QPainter>
#include <QPixmap>
#include <QMouseEvent>
#include <algorithm>

static constexpr double HS = 9.0;

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

CropSelectorWidget::CropSelectorWidget(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(200, 112);
}

void CropSelectorWidget::setFrame(const QImage &frame) {
    m_frame = frame;
    update();
}

void CropSelectorWidget::setCrop(float x, float y, float w, float h) {
    m_cx = std::clamp(x, 0.f, 0.99f);
    m_cy = std::clamp(y, 0.f, 0.99f);
    m_cw = std::clamp(w, 0.01f, 1.f);
    m_ch = std::clamp(h, 0.01f, 1.f);

    if (m_cx + m_cw > 1.f) m_cx = std::max(0.f, 1.f - m_cw);
    if (m_cy + m_ch > 1.f) m_cy = std::max(0.f, 1.f - m_ch);
    update();
}

// Returns the rectangle inside the widget where the frame is drawn (letterboxed).
QRectF CropSelectorWidget::frameRect() const {
    if (m_frame.isNull()) return rect();
    return CanvasGeometry::letterbox(double(m_frame.width()) / m_frame.height(),
                                     width(), height());
}

// Current crop rect in widget coordinates.
QRectF CropSelectorWidget::cropInWidget() const {
    return CanvasGeometry::mapRect(frameRect(), QRectF(m_cx, m_cy, m_cw, m_ch));
}

std::array<QRectF, 4> CropSelectorWidget::handles(const QRectF &crop) const {
    return CanvasGeometry::cornerHandles(crop, HS);
}

CropSelectorWidget::DragMode CropSelectorWidget::hitTest(QPointF pt) const {
    QRectF cr = cropInWidget();
    auto hs = handles(cr);
    DragMode modes[] = { DragMode::ResizeTL, DragMode::ResizeTR,
                         DragMode::ResizeBR, DragMode::ResizeBL };
    for (int i = 0; i < 4; ++i) {
        if (hs[i].contains(pt))
            return modes[i];
    }
    if (cr.contains(pt))
        return DragMode::Move;
    return DragMode::None;
}

void CropSelectorWidget::commitDrag(const QRectF &crop) {
    QRectF fr = frameRect();
    if (fr.width() < 1.0 || fr.height() < 1.0) return;

    QRectF norm = CanvasGeometry::unmapRect(fr, crop);
    m_cx = static_cast<float>(norm.left());
    m_cy = static_cast<float>(norm.top());
    m_cw = static_cast<float>(norm.width());
    m_ch = static_cast<float>(norm.height());
    emit cropChanged(m_cx, m_cy, m_cw, m_ch);
    update();
}

void CropSelectorWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;

    m_dragMode = hitTest(e->position());
    if (m_dragMode != DragMode::None) {
        m_dragOrigin = e->position();
        m_dragRect = cropInWidget();
        m_dragMoved = false;
        return;
    }

    // Start a fresh crop selection from an empty area.
    m_dragMode = DragMode::Create;
    m_dragOrigin = e->position();
    m_dragRect = QRectF(e->position(), e->position());
    m_dragMoved = false;
}

void CropSelectorWidget::mouseMoveEvent(QMouseEvent *e) {
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

    QRectF fr = frameRect();
    if (fr.width() < 1.0 || fr.height() < 1.0) return;

    QPointF d = e->position() - m_dragOrigin;
    if (!m_dragMoved && (std::abs(d.x()) >= 1.0 || std::abs(d.y()) >= 1.0))
        m_dragMoved = true;
    QRectF r = m_dragRect;
    const double minW = fr.width() * 0.02;
    const double minH = fr.height() * 0.02;

    switch (m_dragMode) {
        case DragMode::Create: {
            const double left   = clampRange(std::min(m_dragOrigin.x(), e->position().x()), fr.left(), fr.right());
            const double top    = clampRange(std::min(m_dragOrigin.y(), e->position().y()), fr.top(), fr.bottom());
            const double right  = clampRange(std::max(m_dragOrigin.x(), e->position().x()), fr.left(), fr.right());
            const double bottom = clampRange(std::max(m_dragOrigin.y(), e->position().y()), fr.top(), fr.bottom());
            r = QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
            break;
        }
        case DragMode::Move: {
            const double x = clampRange(m_dragRect.left() + d.x(), fr.left(), fr.right() - m_dragRect.width());
            const double y = clampRange(m_dragRect.top()  + d.y(), fr.top(),  fr.bottom() - m_dragRect.height());
            r.moveTopLeft({x, y});
            break;
        }
        case DragMode::ResizeTL: {
            const double left = clampRange(m_dragRect.left() + d.x(), fr.left(), m_dragRect.right() - minW);
            const double top  = clampRange(m_dragRect.top()  + d.y(), fr.top(),  m_dragRect.bottom() - minH);
            r = QRectF(QPointF(left, top), m_dragRect.bottomRight());
            break;
        }
        case DragMode::ResizeTR: {
            const double right = clampRange(m_dragRect.right() + d.x(), m_dragRect.left() + minW, fr.right());
            const double top   = clampRange(m_dragRect.top()   + d.y(), fr.top(), m_dragRect.bottom() - minH);
            r = QRectF(QPointF(m_dragRect.left(), top), QPointF(right, m_dragRect.bottom()));
            break;
        }
        case DragMode::ResizeBR: {
            const double right  = clampRange(m_dragRect.right()  + d.x(), m_dragRect.left() + minW, fr.right());
            const double bottom = clampRange(m_dragRect.bottom() + d.y(), m_dragRect.top()  + minH, fr.bottom());
            r = QRectF(m_dragRect.topLeft(), QPointF(right, bottom));
            break;
        }
        case DragMode::ResizeBL: {
            const double left   = clampRange(m_dragRect.left()   + d.x(), fr.left(), m_dragRect.right() - minW);
            const double bottom = clampRange(m_dragRect.bottom() + d.y(), m_dragRect.top() + minH, fr.bottom());
            r = QRectF(QPointF(left, m_dragRect.top()), QPointF(m_dragRect.right(), bottom));
            break;
        }
        case DragMode::None:
            break;
    }

    // Keep the crop rect valid even when the user drags against an edge.
    r = r.normalized();
    if (r.width() > fr.width()) {
        r.setLeft(fr.left());
        r.setRight(fr.right());
    }
    if (r.height() > fr.height()) {
        r.setTop(fr.top());
        r.setBottom(fr.bottom());
    }
    if (r.left() < fr.left()) r.moveLeft(fr.left());
    if (r.top() < fr.top()) r.moveTop(fr.top());
    if (r.right() > fr.right()) r.moveRight(fr.right());
    if (r.bottom() > fr.bottom()) r.moveBottom(fr.bottom());

    commitDrag(r);
}

void CropSelectorWidget::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || m_dragMode == DragMode::None) return;

    QRectF fr = frameRect();
    if (fr.width() < 1.0 || fr.height() < 1.0) {
        m_dragMode = DragMode::None;
        return;
    }

    if (m_dragMode == DragMode::Create && !m_dragMoved) {
        // Treat a tiny click on empty space as a reset to the full frame.
        m_cx = 0.f; m_cy = 0.f; m_cw = 1.f; m_ch = 1.f;
        emit cropChanged(m_cx, m_cy, m_cw, m_ch);
        update();
    }

    m_dragMode = DragMode::None;
    m_dragMoved = false;
}

void CropSelectorWidget::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    m_cx = 0.f; m_cy = 0.f; m_cw = 1.f; m_ch = 1.f;
    emit cropChanged(m_cx, m_cy, m_cw, m_ch);
    update();
}

void CropSelectorWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x18, 0x19, 0x1b));

    QRectF fr = frameRect();
    p.fillRect(fr, checkerBrush());

    // Draw the frame
    if (!m_frame.isNull())
        p.drawImage(fr, m_frame);
    else {
        p.setPen(QColor(0x33, 0x36, 0x3b));
        p.drawRect(fr);
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(fr, Qt::AlignCenter, "No frame — press Play then switch here");
    }

    QRectF cr = cropInWidget();

    // Dim everything outside the crop rect
    if (!m_frame.isNull()) {
        p.setOpacity(0.55);
        p.fillRect(QRectF(fr.left(), fr.top(), fr.width(), cr.top() - fr.top()), Qt::black);
        p.fillRect(QRectF(fr.left(), cr.bottom(), fr.width(), fr.bottom() - cr.bottom()), Qt::black);
        p.fillRect(QRectF(fr.left(), cr.top(), cr.left() - fr.left(), cr.height()), Qt::black);
        p.fillRect(QRectF(cr.right(), cr.top(), fr.right() - cr.right(), cr.height()), Qt::black);
        p.setOpacity(1.0);
    }

    // Crop border
    p.setPen(QPen(QColor(0x2a, 0x8f, 0xa0), 1.5, Qt::SolidLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(cr);

    // Corner handles
    p.setBrush(QColor(0x4f, 0xc3, 0xd0));
    p.setPen(Qt::NoPen);
    for (const QRectF &h : handles(cr))
        p.drawRect(h);

    // Hint text
    p.setPen(QColor(0x55, 0x55, 0x55));
    p.setFont(QFont("Segoe UI", 9));
    p.drawText(QRectF(0, height() - 18, width(), 18),
               Qt::AlignCenter, "Drag inside to move  ·  Drag corners to resize  ·  Double-click to reset");
}
