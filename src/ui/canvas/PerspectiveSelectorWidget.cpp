#include "ui/canvas/PerspectiveSelectorWidget.h"

#include "ui/common/Theme.h"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>

namespace {
constexpr qreal kHandleR = 7.0;
// Margin around the video preview that handles are allowed to be dragged into,
// so corners can be pulled outside the original frame's boundaries.
constexpr qreal kOverscan = 60.0;
}

PerspectiveSelectorWidget::PerspectiveSelectorWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(320, 180);
    setMouseTracking(true);
    resetCorners();
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void PerspectiveSelectorWidget::setFrame(const QImage &frame) {
    m_frame = frame;
    update();
}

void PerspectiveSelectorWidget::setCorners(const QVector<QPointF> &corners) {
    if (corners.size() == 4) {
        m_corners = corners;
        update();
    }
}

void PerspectiveSelectorWidget::resetCorners() {
    m_corners = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    update();
}

QRectF PerspectiveSelectorWidget::frameRect() const {
    const QRectF area = rect().adjusted(kOverscan, kOverscan, -kOverscan, -kOverscan);
    if (m_frame.isNull()) {
        QRectF r(0, 0, area.width() * m_zoom, area.height() * m_zoom);
        r.moveCenter(area.center());
        return r;
    }
    const QSizeF fs = m_frame.size().scaled(area.size().toSize(), Qt::KeepAspectRatio);
    QRectF r(0, 0, fs.width() * m_zoom, fs.height() * m_zoom);
    r.moveCenter(area.center());
    return r;
}

QPointF PerspectiveSelectorWidget::normToWidget(const QPointF &n) const {
    const QRectF r = frameRect();
    return {r.x() + n.x() * r.width(), r.y() + n.y() * r.height()};
}

QPointF PerspectiveSelectorWidget::widgetToNorm(const QPointF &p) const {
    const QRectF r = frameRect();
    if (r.width() <= 0 || r.height() <= 0) return {0, 0};
    // Clamp to the widget's own bounds (not the frame), so a corner can be
    // dragged outside the original video but still stays reachable on screen.
    const QRectF bounds = rect().adjusted(kHandleR, kHandleR, -kHandleR, -kHandleR);
    const qreal x = std::clamp(p.x(), bounds.left(), bounds.right());
    const qreal y = std::clamp(p.y(), bounds.top(), bounds.bottom());
    return {(x - r.x()) / r.width(), (y - r.y()) / r.height()};
}

void PerspectiveSelectorWidget::mousePressEvent(QMouseEvent *e) {
    m_dragIndex = -1;
    qreal best = kHandleR * 2.5;
    for (int i = 0; i < m_corners.size(); ++i) {
        const qreal d = QLineF(e->position(), normToWidget(m_corners[i])).length();
        if (d < best) { best = d; m_dragIndex = i; }
    }
    if (m_dragIndex >= 0) {
        m_corners[m_dragIndex] = widgetToNorm(e->position());
        update();
    }
}

void PerspectiveSelectorWidget::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragIndex < 0) return;
    m_corners[m_dragIndex] = widgetToNorm(e->position());
    update();
}

void PerspectiveSelectorWidget::mouseReleaseEvent(QMouseEvent *) {
    m_dragIndex = -1;
}

void PerspectiveSelectorWidget::wheelEvent(QWheelEvent *e) {
    const qreal factor = e->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
    m_zoom = std::clamp(m_zoom * factor, 0.2, 3.0);
    update();
    e->accept();
}

void PerspectiveSelectorWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const auto &t = Theme::instance().tokens();
    p.fillRect(rect(), t.bgBase);

    const QRectF fr = frameRect();

    // Reference outline marking the original, undistorted video bounds.
    p.setPen(QPen(t.textDisabled, 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(fr);

    QPolygonF poly;
    for (const QPointF &c : m_corners)
        poly << normToWidget(c);

    if (!m_frame.isNull()) {
        // Warp the preview the same way KeystoneSource::process does, so the
        // handles show exactly what the corners will do to the real output.
        const qreal w = m_frame.width();
        const qreal h = m_frame.height();
        const QPolygonF srcQuad{{0, 0}, {w, 0}, {w, h}, {0, h}};
        QTransform t;
        if (QTransform::quadToQuad(srcQuad, poly, t)) {
            p.save();
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.setTransform(t, true);
            p.drawImage(0, 0, m_frame);
            p.restore();
        }
    } else {
        p.setPen(t.textDisabled);
        p.drawText(fr, Qt::AlignCenter, "No preview");
    }

    QColor fill = t.accent;
    fill.setAlpha(40);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(t.accent, 2));
    p.setBrush(fill);
    p.drawPolygon(poly);

    p.setBrush(t.accent);
    for (const QPointF &pt : poly)
        p.drawEllipse(pt, kHandleR, kHandleR);
}
