#include "ui/canvas/TransformCanvasWidget.h"
#include "ui/canvas/CanvasGeometry.h"
#include "ui/common/Theme.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <cmath>

namespace {
constexpr qreal kMinSize = 0.02;      // minimum clip extent, normalized
constexpr qreal kSnapPx  = 6.0;       // snap threshold in screen pixels
}

TransformCanvasWidget::TransformCanvasWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(480, 300);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
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

void TransformCanvasWidget::clear() {
    m_clips.clear();
    update();
}

int TransformCanvasWidget::selectedIndex() const {
    for (int i = 0; i < m_clips.size(); ++i)
        if (m_clips[i].selected) return i;
    return -1;
}

void TransformCanvasWidget::setSelectedIndex(int index) {
    for (int i = 0; i < m_clips.size(); ++i)
        m_clips[i].selected = (i == index);
    update();
}

void TransformCanvasWidget::setClipRect(int index, const QRectF &rect) {
    if (index < 0 || index >= m_clips.size()) return;
    QRectF r = rect;
    r.setWidth(qBound(kMinSize, r.width(), 1.0));
    r.setHeight(qBound(kMinSize, r.height(), 1.0));
    m_clips[index].rect = clampMove(r);
    update();
}

void TransformCanvasWidget::setClipVisible(int index, bool visible) {
    if (index < 0 || index >= m_clips.size()) return;
    m_clips[index].visible = visible;
    update();
}

// ── Geometry ────────────────────────────────────────────────────────────────

QRectF TransformCanvasWidget::canvasRect() const {
    return CanvasGeometry::letterbox((double)m_canvasW / m_canvasH,
                                     width(), height(), 16.0);
}

QRectF TransformCanvasWidget::itemToScreen(const QRectF &itemRect) const {
    return CanvasGeometry::mapRect(canvasRect(), itemRect);
}

std::array<QRectF, 8> TransformCanvasWidget::handleRects(const QRectF &r, double size) const {
    const double h = size / 2.0;
    const qreal cx = r.center().x(), cy = r.center().y();
    return { QRectF(r.left()  - h, r.top()    - h, size, size),   // NW
             QRectF(cx       - h, r.top()    - h, size, size),   // N
             QRectF(r.right() - h, r.top()    - h, size, size),   // NE
             QRectF(r.right() - h, cy        - h, size, size),   // E
             QRectF(r.right() - h, r.bottom() - h, size, size),   // SE
             QRectF(cx       - h, r.bottom() - h, size, size),   // S
             QRectF(r.left()  - h, r.bottom() - h, size, size),   // SW
             QRectF(r.left()  - h, cy        - h, size, size) }; // W
}

TransformCanvasWidget::Handle TransformCanvasWidget::handleAt(const QPointF &pos, int *itemIndex) const {
    static constexpr Handle kOrder[8] = { Handle::NW, Handle::N, Handle::NE, Handle::E,
                                          Handle::SE, Handle::S, Handle::SW, Handle::W };
    const int sel = selectedIndex();
    if (sel >= 0) {
        const auto rects = handleRects(itemToScreen(m_clips[sel].rect), 14.0);
        for (int i = 0; i < 8; ++i)
            if (rects[i].contains(pos)) { *itemIndex = sel; return kOrder[i]; }
    }
    const int idx = itemAt(pos);
    *itemIndex = idx;
    return idx >= 0 ? Handle::Move : Handle::None;
}

int TransformCanvasWidget::itemAt(const QPointF &pos) const {
    for (int i = m_clips.size() - 1; i >= 0; --i)
        if (itemToScreen(m_clips[i].rect).contains(pos)) return i;
    return -1;
}

QRectF TransformCanvasWidget::clampMove(QRectF r) const {
    r.moveLeft(qBound(0.0, r.left(), qMax(0.0, 1.0 - r.width())));
    r.moveTop(qBound(0.0, r.top(), qMax(0.0, 1.0 - r.height())));
    return r;
}

// ── Snapping ────────────────────────────────────────────────────────────────

void TransformCanvasWidget::collectSnapTargets(QVector<qreal> &xs, QVector<qreal> &ys) const {
    xs = { 0.0, 0.5, 1.0 };
    ys = { 0.0, 0.5, 1.0 };
    for (int i = 0; i < m_clips.size(); ++i) {
        if (i == m_dragIndex || !m_clips[i].visible) continue;
        const QRectF &r = m_clips[i].rect;
        xs << r.left() << r.center().x() << r.right();
        ys << r.top() << r.center().y() << r.bottom();
    }
}

void TransformCanvasWidget::snapMove(QRectF &r) {
    const QRectF cr = canvasRect();
    const qreal thX = kSnapPx / cr.width(), thY = kSnapPx / cr.height();
    QVector<qreal> xs, ys;
    collectSnapTargets(xs, ys);

    qreal bestDx = 0, bestAx = thX, gx = 0; bool hasX = false;
    const qreal candX[3] = { r.left(), r.center().x(), r.right() };
    for (qreal t : xs) for (qreal c : candX) {
        if (qAbs(t - c) < bestAx) { bestAx = qAbs(t - c); bestDx = t - c; gx = t; hasX = true; }
    }
    qreal bestDy = 0, bestAy = thY, gy = 0; bool hasY = false;
    const qreal candY[3] = { r.top(), r.center().y(), r.bottom() };
    for (qreal t : ys) for (qreal c : candY) {
        if (qAbs(t - c) < bestAy) { bestAy = qAbs(t - c); bestDy = t - c; gy = t; hasY = true; }
    }
    if (hasX) { r.translate(bestDx, 0); m_guidesX.push_back(gx); }
    if (hasY) { r.translate(0, bestDy); m_guidesY.push_back(gy); }
}

void TransformCanvasWidget::snapResize(QRectF &r, Handle h) {
    const QRectF cr = canvasRect();
    const qreal thX = kSnapPx / cr.width(), thY = kSnapPx / cr.height();
    QVector<qreal> xs, ys;
    collectSnapTargets(xs, ys);

    auto snapVal = [](qreal v, const QVector<qreal> &targets, qreal th, bool *ok) {
        qreal best = v, bestA = th;
        *ok = false;
        for (qreal t : targets)
            if (qAbs(t - v) < bestA) { bestA = qAbs(t - v); best = t; *ok = true; }
        return best;
    };
    bool ok;
    const bool left   = (h == Handle::W || h == Handle::NW || h == Handle::SW);
    const bool right  = (h == Handle::E || h == Handle::NE || h == Handle::SE);
    const bool top    = (h == Handle::N || h == Handle::NW || h == Handle::NE);
    const bool bottom = (h == Handle::S || h == Handle::SW || h == Handle::SE);
    if (left)   { const qreal v = snapVal(r.left(), xs, thX, &ok);   if (ok) { r.setLeft(v);   m_guidesX.push_back(v); } }
    if (right)  { const qreal v = snapVal(r.right(), xs, thX, &ok);  if (ok) { r.setRight(v);  m_guidesX.push_back(v); } }
    if (top)    { const qreal v = snapVal(r.top(), ys, thY, &ok);    if (ok) { r.setTop(v);    m_guidesY.push_back(v); } }
    if (bottom) { const qreal v = snapVal(r.bottom(), ys, thY, &ok); if (ok) { r.setBottom(v); m_guidesY.push_back(v); } }
}

// ── Painting ────────────────────────────────────────────────────────────────

void TransformCanvasWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const auto &t = Theme::instance().tokens();
    const bool dark = Theme::instance().isDark();

    p.fillRect(rect(), t.bgBase);

    const QRectF cr = canvasRect();

    // Checkerboard (transparent region) clipped to the canvas.
    const QColor checkA = dark ? t.bgBase.lighter(130) : t.bgBase.darker(105);
    const QColor checkB = dark ? t.bgBase.lighter(150) : t.bgBase.darker(110);
    p.save();
    p.setClipRect(cr);
    const int sq = 16;
    const int x0 = (int)std::floor(cr.left()), y0 = (int)std::floor(cr.top());
    for (int y = y0; y < cr.bottom(); y += sq)
        for (int x = x0; x < cr.right(); x += sq) {
            const bool even = (((x - x0) / sq + (y - y0) / sq) % 2) == 0;
            p.fillRect(QRect(x, y, sq, sq), even ? checkA : checkB);
        }
    p.restore();

    for (int i = 0; i < m_clips.size(); ++i) {
        const auto &clip = m_clips[i];
        const QRectF sr = itemToScreen(clip.rect);

        if (!clip.visible) p.setOpacity(0.25);
        if (!clip.thumbnail.isNull())
            p.drawPixmap(sr.toRect(), clip.thumbnail);
        else
            p.fillRect(sr, t.glassControl);
        p.setOpacity(1.0);

        p.setBrush(Qt::NoBrush);
        if (clip.selected) {
            p.setPen(QPen(t.accent, 2));
        } else {
            QPen pen(clip.visible ? t.textSecondary : t.textDisabled, 1);
            if (!clip.visible) pen.setStyle(Qt::DashLine);
            p.setPen(pen);
        }
        p.drawRect(sr);

        // Name chip.
        if (sr.width() > 70 && sr.height() > 30) {
            QString label = clip.name;
            if (!clip.visible) label += QStringLiteral("  ·  hidden");
            QFont f = font();
            f.setPointSizeF(8);
            p.setFont(f);
            const qreal tw = qMin(QFontMetricsF(f).horizontalAdvance(label) + 12.0, sr.width() - 8.0);
            const QRectF chip(sr.left() + 4, sr.top() + 4, tw, 17);
            p.setPen(Qt::NoPen);
            QColor chipBg = t.surfacePopup;
            chipBg.setAlpha(220);
            p.setBrush(chipBg);
            p.drawRoundedRect(chip, 3, 3);
            p.setPen(clip.selected ? t.accent : t.text);
            p.drawText(chip.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetricsF(f).elidedText(label, Qt::ElideRight, chip.width() - 10));
        }
    }

    // Canvas frame above the clips so edges stay crisp.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(t.stroke, 1));
    p.drawRect(cr);

    // Snap guides.
    if (!m_guidesX.isEmpty() || !m_guidesY.isEmpty()) {
        QPen gp(t.warning, 1, Qt::DashLine);
        p.setPen(gp);
        for (qreal gx : m_guidesX) {
            const qreal x = cr.left() + gx * cr.width();
            p.drawLine(QPointF(x, cr.top()), QPointF(x, cr.bottom()));
        }
        for (qreal gy : m_guidesY) {
            const qreal y = cr.top() + gy * cr.height();
            p.drawLine(QPointF(cr.left(), y), QPointF(cr.right(), y));
        }
    }

    // Handles for the selection.
    const int sel = selectedIndex();
    if (sel >= 0) {
        const QRectF sr = itemToScreen(m_clips[sel].rect);
        p.setPen(QPen(t.bgBase, 1));
        p.setBrush(t.accent);
        for (const QRectF &h : handleRects(sr, 8.0))
            p.drawRect(h);
    }

    // Position/size readout while dragging.
    if (m_dragging && m_dragIndex >= 0) {
        const QRectF &r = m_clips[m_dragIndex].rect;
        const QString txt = QStringLiteral("%1, %2    %3 × %4")
            .arg(qRound(r.x() * m_canvasW)).arg(qRound(r.y() * m_canvasH))
            .arg(qRound(r.width() * m_canvasW)).arg(qRound(r.height() * m_canvasH));
        QFont f = font();
        f.setPointSizeF(8.5);
        p.setFont(f);
        const qreal tw = QFontMetricsF(f).horizontalAdvance(txt) + 20;
        const QRectF chip(cr.center().x() - tw / 2, cr.bottom() - 26, tw, 20);
        p.setPen(Qt::NoPen);
        QColor readoutBg = t.surfacePopup;
        readoutBg.setAlpha(220);
        p.setBrush(readoutBg);
        p.drawRoundedRect(chip, 4, 4);
        p.setPen(t.text);
        p.drawText(chip, Qt::AlignCenter, txt);
    }
}

// ── Interaction ─────────────────────────────────────────────────────────────

void TransformCanvasWidget::updateCursor(const QPointF &pos) {
    int idx;
    switch (handleAt(pos, &idx)) {
    case Handle::NW: case Handle::SE: setCursor(Qt::SizeFDiagCursor); break;
    case Handle::NE: case Handle::SW: setCursor(Qt::SizeBDiagCursor); break;
    case Handle::N:  case Handle::S:  setCursor(Qt::SizeVerCursor);   break;
    case Handle::E:  case Handle::W:  setCursor(Qt::SizeHorCursor);   break;
    case Handle::Move:                setCursor(Qt::SizeAllCursor);   break;
    default:                          setCursor(Qt::ArrowCursor);     break;
    }
}

void TransformCanvasWidget::selectOnly(int index) {
    if (selectedIndex() == index) return;
    setSelectedIndex(index);
    emit selectionChanged(index);
}

void TransformCanvasWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    const QPointF pos = event->pos();

    int idx;
    Handle h = handleAt(pos, &idx);
    if (h == Handle::Move) selectOnly(idx);
    else if (h == Handle::None) { selectOnly(-1); return; }

    m_dragIndex = idx;
    m_dragHandle = h;
    m_pressPos = pos;
    m_origRect = m_clips[idx].rect;
    m_dragging = true;
    update();
}

void TransformCanvasWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging || m_dragIndex < 0) {
        updateCursor(event->pos());
        return;
    }

    const QRectF cr = canvasRect();
    QPointF d((event->pos().x() - m_pressPos.x()) / cr.width(),
              (event->pos().y() - m_pressPos.y()) / cr.height());
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool noSnap = event->modifiers() & Qt::AltModifier;

    m_guidesX.clear();
    m_guidesY.clear();

    QRectF r = m_origRect;
    if (m_dragHandle == Handle::Move) {
        if (shift) {   // axis lock along the dominant direction
            if (qAbs(d.x() * cr.width()) >= qAbs(d.y() * cr.height())) d.setY(0);
            else d.setX(0);
        }
        r.translate(d);
        if (!noSnap) snapMove(r);
        r = clampMove(r);
    } else {
        const bool left   = (m_dragHandle == Handle::W || m_dragHandle == Handle::NW || m_dragHandle == Handle::SW);
        const bool right  = (m_dragHandle == Handle::E || m_dragHandle == Handle::NE || m_dragHandle == Handle::SE);
        const bool top    = (m_dragHandle == Handle::N || m_dragHandle == Handle::NW || m_dragHandle == Handle::NE);
        const bool bottom = (m_dragHandle == Handle::S || m_dragHandle == Handle::SW || m_dragHandle == Handle::SE);
        if (left)   r.setLeft(r.left() + d.x());
        if (right)  r.setRight(r.right() + d.x());
        if (top)    r.setTop(r.top() + d.y());
        if (bottom) r.setBottom(r.bottom() + d.y());

        if (!noSnap) snapResize(r, m_dragHandle);

        // Shift on a corner keeps the original aspect, anchored opposite.
        const bool corner = (left || right) && (top || bottom);
        if (shift && corner && m_origRect.height() > 1e-6) {
            const qreal ar = m_origRect.width() / m_origRect.height();
            qreal w = qAbs(r.width()), hh = qAbs(r.height());
            if (w / ar > hh) w = hh * ar; else hh = w / ar;
            const qreal x = left ? m_origRect.right() - w : m_origRect.left();
            const qreal y = top  ? m_origRect.bottom() - hh : m_origRect.top();
            r = QRectF(x, y, w, hh);
            m_guidesX.clear();
            m_guidesY.clear();
        }

        r = r.normalized().intersected(QRectF(0, 0, 1, 1));
        if (r.width() < kMinSize)  { if (left) r.setLeft(r.right() - kMinSize);  else r.setRight(r.left() + kMinSize); }
        if (r.height() < kMinSize) { if (top)  r.setTop(r.bottom() - kMinSize);  else r.setBottom(r.top() + kMinSize); }
        r = r.intersected(QRectF(0, 0, 1, 1));
    }

    if (r != m_clips[m_dragIndex].rect) {
        m_clips[m_dragIndex].rect = r;
        emit clipRectChanged(m_dragIndex);
    }
    update();
}

void TransformCanvasWidget::mouseReleaseEvent(QMouseEvent *) {
    m_dragging = false;
    m_dragIndex = -1;
    m_guidesX.clear();
    m_guidesY.clear();
    update();
}

void TransformCanvasWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    const int idx = itemAt(event->pos());
    if (idx < 0) return;
    selectOnly(idx);
    m_clips[idx].rect = QRectF(0, 0, 1, 1);
    emit clipRectChanged(idx);
    update();
}

void TransformCanvasWidget::keyPressEvent(QKeyEvent *event) {
    const int sel = selectedIndex();
    if (event->key() == Qt::Key_Escape && sel >= 0) {
        selectOnly(-1);
        return;
    }
    if (sel < 0) {
        QWidget::keyPressEvent(event);
        return;
    }
    const qreal step = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
    qreal dx = 0, dy = 0;
    switch (event->key()) {
    case Qt::Key_Left:  dx = -step / m_canvasW; break;
    case Qt::Key_Right: dx =  step / m_canvasW; break;
    case Qt::Key_Up:    dy = -step / m_canvasH; break;
    case Qt::Key_Down:  dy =  step / m_canvasH; break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    m_clips[sel].rect = clampMove(m_clips[sel].rect.translated(dx, dy));
    emit clipRectChanged(sel);
    update();
}

void TransformCanvasWidget::leaveEvent(QEvent *) {
    if (!m_dragging) setCursor(Qt::ArrowCursor);
}
