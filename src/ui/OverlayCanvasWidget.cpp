#include "ui/OverlayCanvasWidget.h"
#include <QBrush>
#include <QPainter>
#include <QPixmap>
#include <QMouseEvent>
#include <algorithm>

static constexpr double HS  = 9.0;   // handle size
static constexpr double HS2 = HS / 2.0;

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

OverlayCanvasWidget::OverlayCanvasWidget(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(200, 112);
}

void OverlayCanvasWidget::setFrame(const QImage &f) { m_frame = f; update(); }

void OverlayCanvasWidget::setOverlays(const QList<OverlayItem> &overlays) {
    m_overlays = overlays;
    if (m_selectedIdx >= m_overlays.size()) m_selectedIdx = -1;
    update();
}

void OverlayCanvasWidget::setSelectedIndex(int idx) {
    m_selectedIdx = idx;
    update();
}

// ── Geometry helpers ─────────────────────────────────────────────────────────

QRectF OverlayCanvasWidget::frameRect() const {
    double fw = m_frame.isNull() ? 16.0 : m_frame.width();
    double fh = m_frame.isNull() ? 9.0  : m_frame.height();
    double scale = std::min(width() / fw, height() / fh);
    double dw = fw * scale, dh = fh * scale;
    return QRectF((width() - dw) / 2.0, (height() - dh) / 2.0, dw, dh);
}

QRectF OverlayCanvasWidget::ovToWidget(const OverlayItem &ov) const {
    QRectF fr = frameRect();
    return QRectF(fr.left() + ov.x * fr.width(),  fr.top() + ov.y * fr.height(),
                  ov.w * fr.width(),               ov.h * fr.height());
}

OverlayItem OverlayCanvasWidget::applyWidgetRect(OverlayItem ov, const QRectF &r) const {
    QRectF fr = frameRect();
    if (fr.width() < 1 || fr.height() < 1) return ov;
    ov.x = std::clamp((float)((r.left()   - fr.left()) / fr.width()),  0.f, 0.99f);
    ov.y = std::clamp((float)((r.top()    - fr.top())  / fr.height()), 0.f, 0.99f);
    ov.w = std::clamp((float)(r.width()   / fr.width()),  0.01f, 1.f);
    ov.h = std::clamp((float)(r.height()  / fr.height()), 0.01f, 1.f);
    return ov;
}

// TL, TR, BR, BL corner handles
std::array<QRectF, 4> OverlayCanvasWidget::handles(const OverlayItem &ov) const {
    QRectF r = ovToWidget(ov);
    return { QRectF(r.left()  - HS2, r.top()    - HS2, HS, HS),
             QRectF(r.right() - HS2, r.top()    - HS2, HS, HS),
             QRectF(r.right() - HS2, r.bottom() - HS2, HS, HS),
             QRectF(r.left()  - HS2, r.bottom() - HS2, HS, HS) };
}

OverlayCanvasWidget::HitResult OverlayCanvasWidget::hitTest(QPointF pt) const {
    // Handles of selected overlay take priority
    if (m_selectedIdx >= 0 && m_selectedIdx < m_overlays.size()) {
        auto h = handles(m_overlays[m_selectedIdx]);
        DragMode hm[] = { DragMode::ResizeTL, DragMode::ResizeTR,
                          DragMode::ResizeBR, DragMode::ResizeBL };
        for (int i = 0; i < 4; ++i)
            if (h[i].contains(pt)) return { m_selectedIdx, hm[i] };
    }
    // Overlay bodies (topmost first = reverse order)
    for (int i = m_overlays.size() - 1; i >= 0; --i) {
        if (m_overlays[i].visible && ovToWidget(m_overlays[i]).contains(pt))
            return { i, DragMode::Move };
    }
    return { -1, DragMode::None };
}

// ── Mouse events ─────────────────────────────────────────────────────────────

void OverlayCanvasWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    auto hit = hitTest(e->position());
    if (hit.index != m_selectedIdx) {
        m_selectedIdx = hit.index;
        emit overlaySelected(m_selectedIdx);
        update();
    }
    if (hit.mode != DragMode::None && hit.index >= 0) {
        m_dragMode   = hit.mode;
        m_dragOrigin = e->position();
        m_dragItem   = m_overlays[hit.index];
        m_dragRect   = ovToWidget(m_dragItem);
    }
}

void OverlayCanvasWidget::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragMode == DragMode::None) {
        auto hit = hitTest(e->position());
        switch (hit.mode) {
            case DragMode::Move:      setCursor(Qt::SizeAllCursor);    break;
            case DragMode::ResizeTL:
            case DragMode::ResizeBR:  setCursor(Qt::SizeFDiagCursor);  break;
            case DragMode::ResizeTR:
            case DragMode::ResizeBL:  setCursor(Qt::SizeBDiagCursor);  break;
            default:                  setCursor(Qt::ArrowCursor);       break;
        }
        return;
    }
    if (m_selectedIdx < 0) return;

    QPointF d = e->position() - m_dragOrigin;
    QRectF  fr = frameRect();
    QRectF  r  = m_dragRect;

    switch (m_dragMode) {
        case DragMode::Move:
            r.translate(d);
            // Clamp inside frame
            if (r.left()   < fr.left())   r.moveLeft(fr.left());
            if (r.top()    < fr.top())    r.moveTop(fr.top());
            if (r.right()  > fr.right())  r.moveRight(fr.right());
            if (r.bottom() > fr.bottom()) r.moveBottom(fr.bottom());
            break;
        case DragMode::ResizeTL: r.setTopLeft(r.topLeft()         + d); break;
        case DragMode::ResizeTR: r.setTopRight(r.topRight()       + d); break;
        case DragMode::ResizeBR: r.setBottomRight(r.bottomRight() + d); break;
        case DragMode::ResizeBL: r.setBottomLeft(r.bottomLeft()   + d); break;
        default: break;
    }
    // Enforce minimum size (2% of frame)
    double minW = fr.width()  * 0.02, minH = fr.height() * 0.02;
    if (r.width()  < minW) r.setWidth(minW);
    if (r.height() < minH) r.setHeight(minH);

    m_overlays[m_selectedIdx] = applyWidgetRect(m_dragItem, r);
    emit overlayChanged(m_selectedIdx, m_overlays[m_selectedIdx]);
    update();
}

void OverlayCanvasWidget::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) m_dragMode = DragMode::None;
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void OverlayCanvasWidget::drawOverlay(QPainter &p, const OverlayItem &ov, bool selected) const {
    if (!ov.visible && !selected) return;

    QRectF r = ovToWidget(ov);
    p.setOpacity(ov.visible ? static_cast<double>(ov.opacity) : 0.35);

    if (ov.type == OverlayItem::Type::Image) {
        if (!m_pixCache.contains(ov.content))
            m_pixCache.insert(ov.content, QPixmap(ov.content));
        const QPixmap &pm = m_pixCache[ov.content];
        if (!pm.isNull()) {
            // Center the image within the rect, maintaining aspect ratio
            QSizeF scaled = pm.size().scaled(r.size().toSize(),
                                             Qt::KeepAspectRatio);
            QRectF dst(r.center().x() - scaled.width() / 2,
                       r.center().y() - scaled.height() / 2,
                       scaled.width(), scaled.height());
            p.drawPixmap(dst.toRect(),
                pm.scaled(dst.size().toSize(), Qt::KeepAspectRatio,
                          Qt::SmoothTransformation));
        } else {
            p.setPen(QColor(0xe0, 0x50, 0x50));
            p.drawRect(r);
            p.drawText(r, Qt::AlignCenter, "⚠ image\nnot found");
        }
    } else {
        // Scale font relative to canvas width vs. reference 1280px output width
        QRectF fr = frameRect();
        double scale = fr.width() > 0 ? fr.width() / 1280.0 : 1.0;
        QFont f;
        f.setPixelSize(std::max(8, static_cast<int>(ov.fontSize * scale)));
        p.setFont(f);
        p.setPen(ov.color);
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, ov.content);
    }
    p.setOpacity(1.0);

    // Selection indicator
    if (selected) {
        p.setPen(QPen(QColor(0x4f, 0xc3, 0xd0), 1.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        // Corner handles
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x4f, 0xc3, 0xd0));
        for (const auto &h : handles(ov))
            p.drawRect(h);
    }
}

void OverlayCanvasWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x18, 0x19, 0x1b));

    QRectF fr = frameRect();
    p.fillRect(fr, checkerBrush());
    if (!m_frame.isNull())
        p.drawImage(fr, m_frame);
    else {
        p.setPen(QColor(0x33, 0x36, 0x3b));
        p.drawRect(fr);
        p.setPen(QColor(0x55, 0x55, 0x55));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(fr, Qt::AlignCenter, "No frame — press Play then come here");
    }

    // Draw non-selected overlays first, selected on top
    for (int i = 0; i < m_overlays.size(); ++i)
        if (i != m_selectedIdx)
            drawOverlay(p, m_overlays[i], false);
    if (m_selectedIdx >= 0 && m_selectedIdx < m_overlays.size())
        drawOverlay(p, m_overlays[m_selectedIdx], true);

    // Bottom hint
    p.setPen(QColor(0x44, 0x44, 0x44));
    p.setFont(QFont("Segoe UI", 9));
    p.drawText(QRectF(0, height() - 18, width(), 18), Qt::AlignCenter,
        m_overlays.isEmpty()
            ? "Click  '＋ Text'  or  '＋ Image'  to add an overlay"
            : "Click text/image overlay to select  ·  Drag to move  ·  Drag corners to resize");
}
