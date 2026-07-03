#include "ui/canvas/HtmlWorkspaceCanvasWidget.h"
#include "core/sources/HtmlWorkspace.h"
#include <QPainter>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <algorithm>

static constexpr char kPresetMime[] = "application/x-prism-html-preset";

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

HtmlWorkspaceCanvasWidget::HtmlWorkspaceCanvasWidget(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumSize(320, 180);
}

void HtmlWorkspaceCanvasWidget::setWorkspace(const HtmlWorkspace &workspace) {
    m_workspace = workspace;
    if (m_selectedIdx >= m_workspace.components.size())
        m_selectedIdx = -1;
    update();
}

void HtmlWorkspaceCanvasWidget::setSelectedIndex(int idx) {
    m_selectedIdx = idx;
    update();
}

QRectF HtmlWorkspaceCanvasWidget::canvasRect() const {
    const double fw = HtmlWorkspace::kCanvasWidth;
    const double fh = HtmlWorkspace::kCanvasHeight;
    const double scale = std::min(width() / fw, height() / fh);
    const double dw = fw * scale;
    const double dh = fh * scale;
    return QRectF((width() - dw) / 2.0, (height() - dh) / 2.0, dw, dh);
}

QRectF HtmlWorkspaceCanvasWidget::compToWidget(const HtmlWorkspaceComponent &c) const {
    const QRectF cr = canvasRect();
    return QRectF(cr.left() + c.x * cr.width(),  cr.top() + c.y * cr.height(),
                  c.w * cr.width(),              c.h * cr.height());
}

HtmlWorkspaceComponent HtmlWorkspaceCanvasWidget::applyWidgetRect(HtmlWorkspaceComponent c,
                                                                  const QRectF &r) const {
    const QRectF cr = canvasRect();
    if (cr.width() < 1 || cr.height() < 1)
        return c;
    c.x = std::clamp((float)((r.left()   - cr.left()) / cr.width()),  0.f, 0.99f);
    c.y = std::clamp((float)((r.top()    - cr.top())  / cr.height()), 0.f, 0.99f);
    c.w = std::clamp((float)(r.width()   / cr.width()),  0.02f, 1.f);
    c.h = std::clamp((float)(r.height()  / cr.height()), 0.02f, 1.f);
    return c;
}

std::array<QRectF, 4> HtmlWorkspaceCanvasWidget::handles(const HtmlWorkspaceComponent &c) const {
    const QRectF r = compToWidget(c);
    const double hs2 = kHandleSize / 2.0;
    return { QRectF(r.left()  - hs2, r.top()    - hs2, kHandleSize, kHandleSize),
             QRectF(r.right() - hs2, r.top()    - hs2, kHandleSize, kHandleSize),
             QRectF(r.right() - hs2, r.bottom() - hs2, kHandleSize, kHandleSize),
             QRectF(r.left()  - hs2, r.bottom() - hs2, kHandleSize, kHandleSize) };
}

HtmlWorkspaceCanvasWidget::HitResult HtmlWorkspaceCanvasWidget::hitTest(QPointF pt) const {
    if (m_selectedIdx >= 0 && m_selectedIdx < m_workspace.components.size()) {
        const auto h = handles(m_workspace.components[m_selectedIdx]);
        const DragMode hm[] = { DragMode::ResizeTL, DragMode::ResizeTR,
                                DragMode::ResizeBR, DragMode::ResizeBL };
        for (int i = 0; i < 4; ++i) {
            if (h[i].contains(pt))
                return { m_selectedIdx, hm[i] };
        }
    }
    for (int i = m_workspace.components.size() - 1; i >= 0; --i) {
        if (m_workspace.components[i].visible && compToWidget(m_workspace.components[i]).contains(pt))
            return { i, DragMode::Move };
    }
    return { -1, DragMode::None };
}

QString HtmlWorkspaceCanvasWidget::presetLabel(const QString &presetId) const {
    const HtmlPresetInfo *info = HtmlPresetRegistry::find(presetId);
    return info ? info->displayName : presetId;
}

void HtmlWorkspaceCanvasWidget::drawComponent(QPainter &p, const HtmlWorkspaceComponent &c,
                                              bool selected) const {
    if (!c.visible && !selected)
        return;

    const QRectF r = compToWidget(c);
    p.setOpacity(c.visible ? 1.0 : 0.35);
    p.fillRect(r, QColor(0x1a, 0x3a, 0x44, 180));
    p.setPen(QPen(QColor(0x00, 0xe5, 0xff, 120), 1));
    p.drawRect(r);

    p.setPen(QColor(0xe0, 0xe8, 0xee));
    p.setFont(QFont("Segoe UI", 10, QFont::Bold));
    p.drawText(r.adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignLeft,
               presetLabel(c.presetId));

    p.setOpacity(1.0);
    if (selected) {
        p.setPen(QPen(QColor(0x4f, 0xc3, 0xd0), 1.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x4f, 0xc3, 0xd0));
        for (const auto &h : handles(c))
            p.drawRect(h);
    }
}

void HtmlWorkspaceCanvasWidget::emitChanged() {
    emit workspaceChanged(m_workspace);
}

void HtmlWorkspaceCanvasWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x18, 0x19, 0x1b));

    const QRectF cr = canvasRect();
    p.fillRect(cr, checkerBrush());
    p.setPen(QColor(0x33, 0x36, 0x3b));
    p.drawRect(cr);

    QList<int> order;
    for (int i = 0; i < m_workspace.components.size(); ++i)
        order.append(i);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return m_workspace.components[a].zIndex < m_workspace.components[b].zIndex;
    });

    for (int i : order) {
        if (i != m_selectedIdx)
            drawComponent(p, m_workspace.components[i], false);
    }
    if (m_selectedIdx >= 0 && m_selectedIdx < m_workspace.components.size())
        drawComponent(p, m_workspace.components[m_selectedIdx], true);

    p.setPen(QColor(0x44, 0x44, 0x44));
    p.setFont(QFont("Segoe UI", 9));
    p.drawText(QRectF(0, height() - 18, width(), 18), Qt::AlignCenter,
               m_workspace.components.isEmpty()
                   ? "Drag components from the palette onto the canvas"
                   : "Drag to move · Corner handles to resize · Drop palette items to add");
}

void HtmlWorkspaceCanvasWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton)
        return;
    const auto hit = hitTest(e->position());
    if (hit.index != m_selectedIdx) {
        m_selectedIdx = hit.index;
        emit componentSelected(m_selectedIdx);
    }
    if (hit.index >= 0) {
        m_dragMode = hit.mode;
        m_dragOrigin = e->position();
        m_dragRect = compToWidget(m_workspace.components[hit.index]);
        m_dragItem = m_workspace.components[hit.index];
    } else {
        m_dragMode = DragMode::None;
    }
    update();
}

void HtmlWorkspaceCanvasWidget::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragMode == DragMode::None || m_selectedIdx < 0)
        return;

    const QPointF delta = e->position() - m_dragOrigin;
    QRectF r = m_dragRect;

    switch (m_dragMode) {
    case DragMode::Move:
        r.translate(delta);
        break;
    case DragMode::ResizeTL:
        r.setTopLeft(m_dragRect.topLeft() + delta);
        break;
    case DragMode::ResizeTR:
        r.setTopRight(m_dragRect.topRight() + delta);
        break;
    case DragMode::ResizeBR:
        r.setBottomRight(m_dragRect.bottomRight() + delta);
        break;
    case DragMode::ResizeBL:
        r.setBottomLeft(m_dragRect.bottomLeft() + delta);
        break;
    default:
        break;
    }

    const QRectF cr = canvasRect();
    const double minW = cr.width() * 0.02;
    const double minH = cr.height() * 0.02;
    if (r.width() < minW)  r.setWidth(minW);
    if (r.height() < minH) r.setHeight(minH);

    m_workspace.components[m_selectedIdx] = applyWidgetRect(m_dragItem, r);
    emitChanged();
    update();
}

void HtmlWorkspaceCanvasWidget::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton)
        m_dragMode = DragMode::None;
}

void HtmlWorkspaceCanvasWidget::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasFormat(kPresetMime))
        e->acceptProposedAction();
}

void HtmlWorkspaceCanvasWidget::dropEvent(QDropEvent *e) {
    const QString presetId = QString::fromUtf8(e->mimeData()->data(kPresetMime));
    if (presetId.isEmpty())
        return;

    HtmlWorkspaceComponent comp = HtmlPresetRegistry::makeComponent(presetId);
    const QRectF cr = canvasRect();
    const QPointF pos = e->position();

    comp.x = std::clamp((float)((pos.x() - cr.left()) / cr.width() - comp.w / 2.f),
                        0.f, 1.f - comp.w);
    comp.y = std::clamp((float)((pos.y() - cr.top()) / cr.height() - comp.h / 2.f),
                        0.f, 1.f - comp.h);
    comp.zIndex = m_workspace.components.size();

    m_workspace.components.append(comp);
    m_selectedIdx = m_workspace.components.size() - 1;
    emit componentSelected(m_selectedIdx);
    emitChanged();
    update();
    e->acceptProposedAction();
}
