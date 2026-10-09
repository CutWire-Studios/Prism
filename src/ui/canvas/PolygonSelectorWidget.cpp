#include "ui/canvas/PolygonSelectorWidget.h"

#include "ui/common/Theme.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace {
constexpr qreal kHandleR = 6.0;
}

PolygonSelectorWidget::PolygonSelectorWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(320, 180);
    setMouseTracking(true);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void PolygonSelectorWidget::setFrame(const QImage &frame) {
    m_frame = frame;
    update();
}

void PolygonSelectorWidget::setPoints(const QVector<QPointF> &points) {
    m_points = points;
    update();
}

void PolygonSelectorWidget::clearPoints() {
    m_points.clear();
    update();
}

QRectF PolygonSelectorWidget::frameRect() const {
    const QRectF area = rect().adjusted(kHandleR, kHandleR, -kHandleR, -kHandleR);
    if (m_frame.isNull()) return area;
    const QSizeF fs = m_frame.size().scaled(area.size().toSize(), Qt::KeepAspectRatio);
    QRectF r(0, 0, fs.width(), fs.height());
    r.moveCenter(area.center());
    return r;
}

QPointF PolygonSelectorWidget::normToWidget(const QPointF &n) const {
    const QRectF r = frameRect();
    return {r.x() + n.x() * r.width(), r.y() + n.y() * r.height()};
}

QPointF PolygonSelectorWidget::widgetToNorm(const QPointF &p) const {
    const QRectF r = frameRect();
    if (r.width() <= 0 || r.height() <= 0) return {0, 0};
    return {std::clamp((p.x() - r.x()) / r.width(), 0.0, 1.0),
            std::clamp((p.y() - r.y()) / r.height(), 0.0, 1.0)};
}

int PolygonSelectorWidget::hitVertex(const QPointF &p) const {
    for (int i = 0; i < m_points.size(); ++i)
        if (QLineF(p, normToWidget(m_points[i])).length() < kHandleR * 2.0)
            return i;
    return -1;
}

void PolygonSelectorWidget::mousePressEvent(QMouseEvent *e) {
    const int hit = hitVertex(e->position());
    if (e->button() == Qt::RightButton) {
        if (hit >= 0) { m_points.remove(hit); update(); }
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    if (hit >= 0) {
        m_dragIndex = hit;
    } else {
        m_points.append(widgetToNorm(e->position()));
        m_dragIndex = m_points.size() - 1;
    }
    update();
}

void PolygonSelectorWidget::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragIndex < 0) return;
    m_points[m_dragIndex] = widgetToNorm(e->position());
    update();
}

void PolygonSelectorWidget::mouseReleaseEvent(QMouseEvent *) {
    m_dragIndex = -1;
}

void PolygonSelectorWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const auto &t = Theme::instance().tokens();
    p.fillRect(rect(), t.bgBase);

    const QRectF fr = frameRect();
    if (!m_frame.isNull())
        p.drawImage(fr, m_frame);
    else {
        p.setPen(t.textDisabled);
        p.drawText(fr, Qt::AlignCenter, "No preview");
    }

    p.setRenderHint(QPainter::Antialiasing, true);

    QPolygonF poly;
    for (const QPointF &pt : m_points)
        poly << normToWidget(pt);

    QColor fill = t.accent;
    fill.setAlpha(40);
    if (poly.size() >= 2) {
        p.setPen(QPen(t.accent, 2));
        p.setBrush(poly.size() >= 3 ? fill : Qt::NoBrush);
        if (poly.size() >= 3) p.drawPolygon(poly);
        else                  p.drawPolyline(poly);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(t.accent);
    for (const QPointF &pt : poly)
        p.drawEllipse(pt, kHandleR, kHandleR);

    if (m_points.isEmpty()) {
        p.setPen(t.textSecondary);
        p.drawText(fr.adjusted(6, 6, -6, -6), Qt::AlignTop | Qt::AlignHCenter,
                   "Click to add points · right-click a point to remove");
    }
}
