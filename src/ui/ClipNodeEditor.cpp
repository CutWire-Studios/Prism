#include "ui/ClipNodeEditor.h"
#include "ui/TransformEditorDialog.h"
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QWheelEvent>
#include <QDebug>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMenu>
#include <QCursor>
#include <QGraphicsSceneContextMenuEvent>
#include <functional>
#include <cmath>

static constexpr qreal CARD_W   = 122.0;
static constexpr qreal CARD_H   = 176.0;
static constexpr qreal PORT_R   = 7.0;
static constexpr qreal HEADER_H = 18.0;
static constexpr qreal PROXY_Y    = PORT_R + HEADER_H;
static constexpr qreal IN_PORT_Y  = PORT_R;
static constexpr qreal OUT_PORT_Y = PROXY_Y + CARD_H + PORT_R;
static constexpr qreal PORT_X     = CARD_W / 2.0;
static constexpr qreal NODE_W     = CARD_W;
static constexpr qreal NODE_H     = OUT_PORT_Y + PORT_R;

static constexpr qreal SMALL_NODE_W = 100.0;
static constexpr qreal SMALL_NODE_H = 110.0;

class PortItem;
class NodeItemBase;
class ClipNodeItem;
class TransformNodeItem;
class TransformContextNodeItem;
class ClipNodeScene;

enum class PortKind {
    ChainIn,           // blue, input (on clip nodes)
    ChainOut,          // green, output (on clip nodes)
    ClipTransform,     // purple, clip-facing (on clip nodes)
    TransformClip,     // purple, clip-facing (on transform nodes)
    TransformContext,  // amber, context-facing (on transform nodes)
    ContextHub         // amber, accepts many (on context nodes)
};

QColor portKindColor(PortKind kind) {
    switch (kind) {
    case PortKind::ChainIn:        return QColor(0x50, 0xa8, 0xd8); // blue
    case PortKind::ChainOut:       return QColor(0x50, 0xd0, 0x90); // green
    case PortKind::ClipTransform:  return QColor(0xd0, 0x50, 0xb0); // purple
    case PortKind::TransformClip:  return QColor(0xd0, 0x50, 0xb0); // purple
    case PortKind::TransformContext: return QColor(0xd0, 0xb0, 0x50); // amber
    case PortKind::ContextHub:     return QColor(0xd0, 0xb0, 0x50); // amber
    }
    return QColor(128, 128, 128);
}

bool portsCompatible(PortKind a, PortKind b) {
    if (a == b) return false;
    if ((a == PortKind::ChainOut && b == PortKind::ChainIn) ||
        (a == PortKind::ChainIn && b == PortKind::ChainOut)) return true;
    if ((a == PortKind::ClipTransform && b == PortKind::TransformClip) ||
        (a == PortKind::TransformClip && b == PortKind::ClipTransform)) return true;
    if ((a == PortKind::TransformContext && b == PortKind::ContextHub) ||
        (a == PortKind::ContextHub && b == PortKind::TransformContext)) return true;
    return false;
}

bool isSingleConnection(PortKind kind) {
    return kind != PortKind::ContextHub;
}

class PortItem : public QGraphicsEllipseItem {
public:
    PortItem(PortKind kind, NodeItemBase *parentNode);

    PortKind kind() const { return m_kind; }
    NodeItemBase *nodeItem() const { return m_nodeItem; }
    QPointF sceneCenter() const { return mapToScene(QPointF(0, 0)); }

    void setConnected(bool c) { m_connected = c; refreshAppearance(); }
    bool isConnected() const  { return m_connected; }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *e) override {
        refreshAppearance(true);
        QGraphicsEllipseItem::hoverEnterEvent(e);
    }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *e) override {
        refreshAppearance(false);
        QGraphicsEllipseItem::hoverLeaveEvent(e);
    }
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;

private:
    PortKind m_kind;
    NodeItemBase *m_nodeItem;
    bool m_connected = false;

    void refreshAppearance(bool hovered = false) {
        QColor base = portKindColor(m_kind);
        if (m_connected) base = base.lighter(115);
        if (hovered)     base = base.lighter(145);
        setPen(QPen(base.darker(160), 1.5));
        setBrush(base);
    }
};

class ConnectionItem : public QGraphicsPathItem {
public:
    enum EdgeKind { Chain, ClipToTransform, TransformToContext };

    ConnectionItem(PortItem *from, PortItem *to, EdgeKind kind)
        : QGraphicsPathItem(nullptr), m_from(from), m_to(to), m_kind(kind)
    {
        setZValue(-1);
        setFlag(QGraphicsItem::ItemIsSelectable, false);
        QColor color;
        if (kind == Chain) color = QColor(0x70, 0xb8, 0xff);
        else if (kind == ClipToTransform) color = QColor(0xd0, 0x70, 0xb0);
        else color = QColor(0xd0, 0xb0, 0x70);
        setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        updatePath();
    }

    PortItem *fromPort() const { return m_from; }
    PortItem *toPort()   const { return m_to;   }
    EdgeKind edgeKind()  const { return m_kind; }

    NodeId fromNodeId() const;
    NodeId toNodeId()   const;

    void updatePath() {
        const QPointF p1 = m_from->sceneCenter();
        const QPointF p2 = m_to->sceneCenter();
        const qreal dy   = std::abs(p2.y() - p1.y()) * 0.5;
        QPainterPath path;
        path.moveTo(p1);
        path.cubicTo(QPointF(p1.x(), p1.y() + dy),
                     QPointF(p2.x(), p2.y() - dy),
                     p2);
        setPath(path);
    }

private:
    PortItem *m_from;
    PortItem *m_to;
    EdgeKind m_kind;
};

class NodeItemBase : public QGraphicsItem {
public:
    virtual ~NodeItemBase() = default;
    virtual NodeId nodeId() const = 0;
    virtual void paint(QPainter *p, const QStyleOptionGraphicsItem *o, QWidget *w) override = 0;
};

class ClipNodeItem : public NodeItemBase {
public:
    ClipNodeItem(ClipNodeModel *model, NodeId id)
        : m_model(model), m_nodeId(id)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);

        auto *card = new ClipCard(0);
        model->setCard(card);

        m_proxy = new QGraphicsProxyWidget(this);
        m_proxy->setWidget(card);
        m_proxy->setPos(0, PROXY_Y);

        m_chainInPort  = new PortItem(PortKind::ChainIn,  this);
        m_chainInPort->setPos(PORT_X, IN_PORT_Y);

        m_chainOutPort = new PortItem(PortKind::ChainOut, this);
        m_chainOutPort->setPos(PORT_X, OUT_PORT_Y);

        m_transformPort = new PortItem(PortKind::ClipTransform, this);
        m_transformPort->setPos(NODE_W, NODE_H / 2);
    }

    NodeId nodeId() const override { return m_nodeId; }
    ClipNodeModel *model() const { return m_model; }
    PortItem *chainInPort()  const { return m_chainInPort; }
    PortItem *chainOutPort() const { return m_chainOutPort; }
    PortItem *transformPort() const { return m_transformPort; }

    QRectF boundingRect() const override { return QRectF(0, 0, NODE_W, NODE_H); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(55, 56, 62), 1));
        p->setBrush(QColor(30, 31, 34));
        p->drawRoundedRect(QRectF(0, 0, NODE_W, NODE_H), 6, 6);

        QPainterPath hdr;
        hdr.moveTo(6, 0);
        hdr.arcTo(QRectF(0, 0, 12, 12), 90, 90);
        hdr.lineTo(0, PROXY_Y);
        hdr.lineTo(NODE_W, PROXY_Y);
        hdr.lineTo(NODE_W, 6);
        hdr.arcTo(QRectF(NODE_W - 12, 0, 12, 12), 0, 90);
        hdr.closeSubpath();
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(42, 44, 52));
        p->drawPath(hdr);

        p->setPen(QColor(90, 95, 110));
        const qreal cy = PORT_R + HEADER_H / 2.0;
        for (int i = -1; i <= 1; ++i) {
            p->drawEllipse(QPointF(NODE_W / 2.0 + i * 8, cy), 2, 2);
        }
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    ClipNodeModel *m_model;
    NodeId m_nodeId;
    QGraphicsProxyWidget *m_proxy;
    PortItem *m_chainInPort;
    PortItem *m_chainOutPort;
    PortItem *m_transformPort;
};

class TransformNodeItem : public NodeItemBase {
public:
    TransformNodeItem(NodeId id, float x = 0, float y = 0, float w = 1, float h = 1)
        : m_nodeId(id), m_x(x), m_y(y), m_w(w), m_h(h)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);

        m_clipPort = new PortItem(PortKind::TransformClip, this);
        m_clipPort->setPos(10, SMALL_NODE_H / 2);

        m_contextPort = new PortItem(PortKind::TransformContext, this);
        m_contextPort->setPos(SMALL_NODE_W - 10, SMALL_NODE_H / 2);
    }

    std::function<void(NodeId)> onEditRequested;

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *clipPort() const { return m_clipPort; }
    PortItem *contextPort() const { return m_contextPort; }

    float x() const { return m_x; }
    float y() const { return m_y; }
    float w() const { return m_w; }
    float h() const { return m_h; }

    void setTransform(float x, float y, float w, float h) {
        m_x = x; m_y = y; m_w = w; m_h = h;
        update();
    }

    QRectF boundingRect() const override { return QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(55, 56, 62), 1));
        p->setBrush(QColor(30, 31, 34));
        p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H), 4, 4);

        p->setPen(QColor(180, 160, 100));
        p->setFont(QFont("Monospace", 8));
        QString label = QString("T: %1%,%2%\n%3%×%4%")
            .arg((int)(m_x * 100)).arg((int)(m_y * 100))
            .arg((int)(m_w * 100)).arg((int)(m_h * 100));
        p->drawText(QRectF(4, 4, SMALL_NODE_W - 8, 55),
                   Qt::AlignCenter, label);

        QRectF buttonRect(4, 65, SMALL_NODE_W - 8, 25);
        p->setPen(QPen(QColor(200, 120, 200), 1));
        p->setBrush(QColor(100, 30, 100));
        p->drawRoundedRect(buttonRect, 3, 3);
        p->setPen(QColor(220, 180, 220));
        p->setFont(QFont("Monospace", 7));
        p->drawText(buttonRect, Qt::AlignCenter, "Edit");
    }

    QRectF getEditButtonRect() const {
        return QRectF(4, 65, SMALL_NODE_W - 8, 25);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override {
        if (onEditRequested) onEditRequested(m_nodeId);
        e->accept();
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        if (getEditButtonRect().contains(e->pos())) {
            if (onEditRequested) onEditRequested(m_nodeId);
            e->accept();
            return;
        }
        QGraphicsItem::mousePressEvent(e);
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    NodeId m_nodeId;
    float m_x, m_y, m_w, m_h;
    PortItem *m_clipPort;
    PortItem *m_contextPort;
};

class TransformContextNodeItem : public NodeItemBase {
public:
    TransformContextNodeItem(NodeId id, int canvasW = 1280, int canvasH = 720)
        : m_nodeId(id), m_canvasW(canvasW), m_canvasH(canvasH)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);

        m_hubPort = new PortItem(PortKind::ContextHub, this);
        m_hubPort->setPos(0, SMALL_NODE_H / 2);
    }

    std::function<void(NodeId)> onEditRequested;
    std::function<void(NodeId)> onOpenEditorRequested;

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *hubPort() const { return m_hubPort; }

    int canvasW() const { return m_canvasW; }
    int canvasH() const { return m_canvasH; }

    void setCanvasSize(int w, int h) {
        m_canvasW = w; m_canvasH = h;
        update();
    }

    QRectF boundingRect() const override { return QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(55, 56, 62), 1));
        p->setBrush(QColor(30, 31, 34));
        p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H), 4, 4);

        p->setPen(QColor(180, 160, 100));
        p->setFont(QFont("Monospace", 8));
        QString label = QString("Canvas\n%1×%2")
            .arg(m_canvasW).arg(m_canvasH);
        p->drawText(QRectF(4, 4, SMALL_NODE_W - 8, 35),
                   Qt::AlignCenter, label);

        QRectF buttonRect(4, 40, SMALL_NODE_W - 8, 25);
        p->setPen(QPen(QColor(100, 180, 255), 1));
        p->setBrush(QColor(20, 80, 150));
        p->drawRoundedRect(buttonRect, 3, 3);
        p->setPen(QColor(200, 220, 255));
        p->setFont(QFont("Monospace", 7));
        p->drawText(buttonRect, Qt::AlignCenter, "Edit");
    }

    QRectF getEditButtonRect() const {
        return QRectF(4, 40, SMALL_NODE_W - 8, 25);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override {
        if (onEditRequested) onEditRequested(m_nodeId);
        e->accept();
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        if (getEditButtonRect().contains(e->pos())) {
            if (onOpenEditorRequested) onOpenEditorRequested(m_nodeId);
            e->accept();
            return;
        }
        QGraphicsItem::mousePressEvent(e);
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override {
        QMenu menu;
        menu.addAction("Edit Canvas Size", [this]() {
            if (onEditRequested) onEditRequested(m_nodeId);
        });
        menu.exec(e->screenPos());
        e->accept();
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    NodeId m_nodeId;
    int m_canvasW, m_canvasH;
    PortItem *m_hubPort;
};

class ClipNodeScene : public QGraphicsScene {
public:
    explicit ClipNodeScene(QObject *parent = nullptr)
        : QGraphicsScene(parent)
    {
        setBackgroundBrush(QColor(20, 21, 23));
    }

    std::function<void()> onConnectionChanged;

    void onPortPressed(PortItem *port, const QPointF &scenePos) {
        m_dragPort = port;
        m_tempLine = addLine(QLineF(port->sceneCenter(), scenePos),
                             QPen(QColor(130, 175, 230), 2, Qt::DashLine));
        m_tempLine->setZValue(10);
    }

    void onPortDragged(const QPointF &scenePos) {
        if (m_tempLine && m_dragPort)
            m_tempLine->setLine(QLineF(m_dragPort->sceneCenter(), scenePos));
    }

    void onPortReleased(const QPointF &scenePos) {
        if (m_tempLine) { removeItem(m_tempLine); delete m_tempLine; m_tempLine = nullptr; }
        PortItem *src = m_dragPort;
        m_dragPort = nullptr;
        if (!src) return;

        PortItem *dst = nullptr;
        for (auto *item : items(scenePos)) {
            if (auto *p = dynamic_cast<PortItem *>(item)) { dst = p; break; }
        }
        if (!dst || dst == src) return;
        if (dst->nodeItem() == src->nodeItem()) return;
        if (!portsCompatible(src->kind(), dst->kind())) return;

        PortItem *outPort, *inPort;
        ConnectionItem::EdgeKind kind;
        if (src->kind() == PortKind::ChainOut || src->kind() == PortKind::ChainIn) {
            outPort = (src->kind() == PortKind::ChainOut) ? src : dst;
            inPort  = (src->kind() == PortKind::ChainIn)  ? src : dst;
            kind = ConnectionItem::Chain;
        } else if (src->kind() == PortKind::ClipTransform || src->kind() == PortKind::TransformClip) {
            outPort = (src->kind() == PortKind::ClipTransform) ? src : dst;
            inPort  = (src->kind() == PortKind::TransformClip)  ? src : dst;
            kind = ConnectionItem::ClipToTransform;
        } else {
            outPort = (src->kind() == PortKind::TransformContext) ? src : dst;
            inPort  = (src->kind() == PortKind::ContextHub)       ? src : dst;
            kind = ConnectionItem::TransformToContext;
        }

        if (isSingleConnection(outPort->kind()))
            disconnectPort(outPort);
        if (isSingleConnection(inPort->kind()))
            disconnectPort(inPort);

        createConnection(outPort, inPort, kind);
    }

    void updateConnectionsForNode(NodeItemBase *nodeItem) {
        for (auto &e : m_edges) {
            if (e.fromNodeId == nodeItem->nodeId() || e.toNodeId == nodeItem->nodeId())
                e.item->updatePath();
        }
    }

    void removeConnectionsForNode(NodeId nodeId) {
        for (int i = m_edges.size() - 1; i >= 0; --i) {
            auto &e = m_edges[i];
            if (e.fromNodeId == nodeId || e.toNodeId == nodeId) {
                e.item->fromPort()->setConnected(false);
                e.item->toPort()->setConnected(false);
                removeItem(e.item);
                delete e.item;
                m_edges.removeAt(i);
                if (onConnectionChanged) onConnectionChanged();
            }
        }
    }

    NodeId upstreamOf(NodeId nodeId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::Chain && e.toNodeId == nodeId)
                return e.fromNodeId;
        return 0;
    }

    NodeId transformNodeForClip(NodeId clipId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::ClipToTransform && e.fromNodeId == clipId)
                return e.toNodeId;
        return 0;
    }

    QVector<NodeId> clipsForContext(NodeId contextId) const {
        QVector<NodeId> result;
        for (const auto &e : m_edges) {
            if (e.edgeKind == ConnectionItem::TransformToContext && e.toNodeId == contextId) {
                for (const auto &ce : m_edges) {
                    if (ce.edgeKind == ConnectionItem::ClipToTransform && ce.toNodeId == e.fromNodeId)
                        result.append(ce.fromNodeId);
                }
            }
        }
        return result;
    }

    void createConnectionManually(PortItem *outPort, PortItem *inPort, ConnectionItem::EdgeKind kind) {
        createConnection(outPort, inPort, kind);
    }

private:
    struct Edge {
        NodeId fromNodeId;
        NodeId toNodeId;
        ConnectionItem::EdgeKind edgeKind;
        ConnectionItem *item;
    };

    void createConnection(PortItem *outPort, PortItem *inPort, ConnectionItem::EdgeKind kind) {
        auto *item = new ConnectionItem(outPort, inPort, kind);
        addItem(item);
        m_edges.append({ outPort->nodeItem()->nodeId(),
                         inPort->nodeItem()->nodeId(),
                         kind,
                         item });
        outPort->setConnected(true);
        inPort->setConnected(true);
        if (onConnectionChanged) onConnectionChanged();
    }

    void disconnectPort(PortItem *port) {
        for (int i = m_edges.size() - 1; i >= 0; --i) {
            auto &e = m_edges[i];
            if (e.item->fromPort() == port || e.item->toPort() == port) {
                e.item->fromPort()->setConnected(false);
                e.item->toPort()->setConnected(false);
                removeItem(e.item);
                delete e.item;
                m_edges.removeAt(i);
                if (onConnectionChanged) onConnectionChanged();
            }
        }
    }

    QList<Edge> m_edges;
    PortItem *m_dragPort = nullptr;
    QGraphicsLineItem *m_tempLine = nullptr;
};

PortItem::PortItem(PortKind kind, NodeItemBase *parentNode)
    : QGraphicsEllipseItem(-PORT_R, -PORT_R, PORT_R * 2, PORT_R * 2)
    , m_kind(kind), m_nodeItem(parentNode)
{
    setParentItem(parentNode);
    setAcceptHoverEvents(true);
    setZValue(2);
    refreshAppearance();
}

void PortItem::mousePressEvent(QGraphicsSceneMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        static_cast<ClipNodeScene *>(scene())->onPortPressed(this, e->scenePos());
        e->accept();
    } else {
        QGraphicsEllipseItem::mousePressEvent(e);
    }
}

void PortItem::mouseMoveEvent(QGraphicsSceneMouseEvent *e) {
    static_cast<ClipNodeScene *>(scene())->onPortDragged(e->scenePos());
    e->accept();
}

void PortItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        static_cast<ClipNodeScene *>(scene())->onPortReleased(e->scenePos());
        e->accept();
    } else {
        QGraphicsEllipseItem::mouseReleaseEvent(e);
    }
}

NodeId ConnectionItem::fromNodeId() const { return m_from->nodeItem()->nodeId(); }
NodeId ConnectionItem::toNodeId()   const { return m_to->nodeItem()->nodeId();   }

QVariant ClipNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

QVariant TransformNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

QVariant TransformContextNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

class ClipNodeView : public QGraphicsView {
public:
    explicit ClipNodeView(QGraphicsScene *scene, QWidget *parent = nullptr)
        : QGraphicsView(scene, parent)
    {
        setRenderHint(QPainter::Antialiasing);
        setViewportUpdateMode(FullViewportUpdate);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setTransformationAnchor(AnchorUnderMouse);
        setResizeAnchor(AnchorViewCenter);
        setSceneRect(-5000, -5000, 10000, 10000);
        setDragMode(NoDrag);
        scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    }

protected:
    void wheelEvent(QWheelEvent *e) override {
        const qreal factor = e->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12;
        scale(factor, factor);
        e->accept();
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::MiddleButton) {
            m_panStart = e->pos();
            m_panning  = true;
            setCursor(Qt::ClosedHandCursor);
            e->accept();
            return;
        }
        QGraphicsView::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_panning) {
            const QPoint delta = e->pos() - m_panStart;
            m_panStart = e->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            e->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::MiddleButton) {
            m_panning = false;
            setCursor(Qt::ArrowCursor);
            e->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(e);
    }

private:
    QPoint m_panStart;
    bool   m_panning = false;
};

ClipNodeEditor::ClipNodeEditor(QWidget *parent)
    : QWidget(parent)
{
    m_scene = new ClipNodeScene(this);
    m_scene->onConnectionChanged = [this]() { emit clipChainChanged(); };

    m_view = new ClipNodeView(m_scene, this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);
    setLayout(layout);

    connect(m_view, &QGraphicsView::customContextMenuRequested, this, &ClipNodeEditor::onCanvasContextMenu);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
}

ClipNodeEditor::~ClipNodeEditor() = default;

ClipNodeModel *ClipNodeEditor::addClipNode(const QString &path, const QPixmap &thumbnail) {
    const NodeId id = m_nextId++;
    auto *model = new ClipNodeModel(this);
    auto *nodeItem = new ClipNodeItem(model, id);

    const int idx = m_nodeMap.size();
    nodeItem->setPos(idx * 160.0 + 20.0, idx * 60.0 + 20.0);

    m_scene->addItem(nodeItem);
    m_nodeMap[id] = model;

    auto *transformNodeItem = new TransformNodeItem(m_nextId++);
    NodeId transformId = transformNodeItem->nodeId();
    transformNodeItem->setPos(idx * 160.0 + 150.0, idx * 60.0 + 20.0);
    transformNodeItem->onEditRequested = [this](NodeId tid) { onEditTransformNode(tid); };
    m_scene->addItem(transformNodeItem);
    m_transformNodes[transformId] = static_cast<void *>(transformNodeItem);

    model->setNodeId(id);
    model->loadClip(path, thumbnail);
    connectNodeSignals(model, id);

    m_scene->createConnectionManually(nodeItem->transformPort(), transformNodeItem->clipPort(),
                                      ConnectionItem::ClipToTransform);

    emit nodeAdded(id);
    return model;
}

ClipNodeModel *ClipNodeEditor::addSourceNode(const SourceDescriptor &desc, const QPixmap &thumbnail) {
    const NodeId id = m_nextId++;
    auto *model = new ClipNodeModel(this);
    auto *nodeItem = new ClipNodeItem(model, id);

    const int idx = m_nodeMap.size();
    nodeItem->setPos(idx * 160.0 + 20.0, idx * 60.0 + 20.0);

    m_scene->addItem(nodeItem);
    m_nodeMap[id] = model;

    auto *transformNodeItem = new TransformNodeItem(m_nextId++);
    NodeId transformId = transformNodeItem->nodeId();
    transformNodeItem->setPos(idx * 160.0 + 150.0, idx * 60.0 + 20.0);
    transformNodeItem->onEditRequested = [this](NodeId tid) { onEditTransformNode(tid); };
    m_scene->addItem(transformNodeItem);
    m_transformNodes[transformId] = static_cast<void *>(transformNodeItem);

    model->setNodeId(id);
    model->loadSource(desc, thumbnail);
    connectNodeSignals(model, id);

    m_scene->createConnectionManually(nodeItem->transformPort(), transformNodeItem->clipPort(),
                                      ConnectionItem::ClipToTransform);

    emit nodeAdded(id);
    return model;
}

void ClipNodeEditor::removeNode(NodeId nodeId) {
    auto it = m_nodeMap.find(nodeId);
    if (it == m_nodeMap.end()) return;

    ClipNodeModel *model = *it;
    m_nodeMap.erase(it);
    m_scene->removeConnectionsForNode(nodeId);

    for (auto *item : m_scene->items()) {
        if (auto *nodeItem = dynamic_cast<ClipNodeItem *>(item)) {
            if (nodeItem->nodeId() == nodeId) {
                m_scene->removeItem(nodeItem);
                delete nodeItem;
                break;
            }
        }
    }

    disconnectNodeSignals(model);
    delete model;

    emit nodeRemoved(nodeId);
}

void ClipNodeEditor::clearAllNodes() {
    const QVector<NodeId> ids = m_nodeMap.keys().toVector();
    for (NodeId id : ids)
        removeNode(id);
}

QVector<ClipNodeModel *> ClipNodeEditor::allNodes() const {
    return m_nodeMap.values().toVector();
}

ClipNodeModel *ClipNodeEditor::nodeAt(NodeId id) const {
    auto it = m_nodeMap.find(id);
    return (it != m_nodeMap.end()) ? *it : nullptr;
}

void ClipNodeEditor::setActiveDeckClip(NodeId clipId, bool deckA) {
    if (deckA) {
        m_activeClipA = clipId;
        emit deckAClipChanged(clipId);
    } else {
        m_activeClipB = clipId;
        emit deckBClipChanged(clipId);
    }
    emit clipChainChanged();
}

QVector<ClipNodeModel *> ClipNodeEditor::getClipChain(NodeId fromClip) const {
    return traverseUpstream(fromClip);
}

void ClipNodeEditor::onNodeAButtonClicked(NodeId nodeId) {
    auto *node = nodeAt(nodeId);
    if (node && node->hasSource())
        setActiveDeckClip(nodeId, true);
}

void ClipNodeEditor::onNodeBButtonClicked(NodeId nodeId) {
    auto *node = nodeAt(nodeId);
    if (node && node->hasSource())
        setActiveDeckClip(nodeId, false);
}

void ClipNodeEditor::onNodeRemoveRequested(NodeId nodeId) {
    removeNode(nodeId);
}

void ClipNodeEditor::connectNodeSignals(ClipNodeModel *model, NodeId id) {
    connect(model, &ClipNodeModel::aButtonClicked,  this, [this, id]() { onNodeAButtonClicked(id); });
    connect(model, &ClipNodeModel::bButtonClicked,  this, [this, id]() { onNodeBButtonClicked(id); });
    connect(model, &ClipNodeModel::removeRequested, this, [this, id]() { onNodeRemoveRequested(id); });
}

void ClipNodeEditor::onCanvasContextMenu() {
    QMenu menu;
    menu.addAction("Add Transform Context Node", this, &ClipNodeEditor::onAddTransformContext);
    menu.exec(QCursor::pos());
}

void ClipNodeEditor::onAddTransformContext() {
    auto *contextNode = new TransformContextNodeItem(m_nextId++);
    contextNode->setPos(m_view->mapToScene(m_view->mapFromGlobal(QCursor::pos())));
    contextNode->onEditRequested = [this](NodeId cid) { onEditContextNode(cid); };
    contextNode->onOpenEditorRequested = [this](NodeId cid) { onOpenTransformEditor(cid); };
    m_scene->addItem(contextNode);
    m_contextNodes[contextNode->nodeId()] = static_cast<void *>(contextNode);
    emit clipChainChanged();
}

void ClipNodeEditor::onEditTransformNode(NodeId nodeId) {
    auto it = m_transformNodes.find(nodeId);
    if (it == m_transformNodes.end()) return;

    TransformNodeItem *transform = static_cast<TransformNodeItem *>(*it);

    QDialog dialog(this);
    dialog.setWindowTitle("Edit Transform");
    dialog.setModal(true);

    auto *layout = new QFormLayout(&dialog);

    auto *xSpin = new QDoubleSpinBox();
    xSpin->setRange(0, 100);
    xSpin->setDecimals(1);
    xSpin->setSuffix("%");
    xSpin->setValue(transform->x() * 100);
    layout->addRow("X Position:", xSpin);

    auto *ySpin = new QDoubleSpinBox();
    ySpin->setRange(0, 100);
    ySpin->setDecimals(1);
    ySpin->setSuffix("%");
    ySpin->setValue(transform->y() * 100);
    layout->addRow("Y Position:", ySpin);

    auto *wSpin = new QDoubleSpinBox();
    wSpin->setRange(1, 100);
    wSpin->setDecimals(1);
    wSpin->setSuffix("%");
    wSpin->setValue(transform->w() * 100);
    layout->addRow("Width:", wSpin);

    auto *hSpin = new QDoubleSpinBox();
    hSpin->setRange(1, 100);
    hSpin->setDecimals(1);
    hSpin->setSuffix("%");
    hSpin->setValue(transform->h() * 100);
    layout->addRow("Height:", hSpin);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        transform->setTransform(xSpin->value() / 100.f, ySpin->value() / 100.f,
                               wSpin->value() / 100.f, hSpin->value() / 100.f);
        emit clipChainChanged();
    }
}

void ClipNodeEditor::onEditContextNode(NodeId nodeId) {
    auto it = m_contextNodes.find(nodeId);
    if (it == m_contextNodes.end()) return;

    TransformContextNodeItem *context = static_cast<TransformContextNodeItem *>(*it);

    QDialog dialog(this);
    dialog.setWindowTitle("Canvas Size");
    dialog.setModal(true);

    auto *layout = new QFormLayout(&dialog);

    auto *wSpin = new QSpinBox();
    wSpin->setRange(1, 7680);
    wSpin->setValue(context->canvasW());
    layout->addRow("Width:", wSpin);

    auto *hSpin = new QSpinBox();
    hSpin->setRange(1, 4320);
    hSpin->setValue(context->canvasH());
    layout->addRow("Height:", hSpin);

    auto *preset = new QComboBox();
    preset->addItem("Custom");
    preset->addItem("16:9 (1280×720)");
    preset->addItem("16:9 (1920×1080)");
    preset->addItem("4:3 (1024×768)");
    layout->addRow("Preset:", preset);

    connect(preset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [wSpin, hSpin, preset]() {
        if (preset->currentIndex() == 1) { wSpin->setValue(1280); hSpin->setValue(720); }
        else if (preset->currentIndex() == 2) { wSpin->setValue(1920); hSpin->setValue(1080); }
        else if (preset->currentIndex() == 3) { wSpin->setValue(1024); hSpin->setValue(768); }
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        context->setCanvasSize(wSpin->value(), hSpin->value());
        emit clipChainChanged();
    }
}

void ClipNodeEditor::onOpenTransformEditor(NodeId contextId) {
    TransformEditorDialog dialog(contextId, this, this);
    dialog.exec();
}

void ClipNodeEditor::disconnectNodeSignals(ClipNodeModel *model) {
    disconnect(model, nullptr, this, nullptr);
}

QVector<ClipNodeModel *> ClipNodeEditor::traverseUpstream(NodeId clipId) const {
    QVector<ClipNodeModel *> chain;
    auto *node = nodeAt(clipId);
    if (!node) return chain;

    chain.push_back(node);

    const NodeId upId = m_scene->upstreamOf(clipId);
    if (upId != 0) {
        const auto upstream = traverseUpstream(upId);
        for (auto *n : upstream)
            if (!chain.contains(n)) chain.push_back(n);
    }
    return chain;
}

NodeId ClipNodeEditor::transformNodeForClip(NodeId clipId) const {
    return m_scene->transformNodeForClip(clipId);
}

bool ClipNodeEditor::clipTransform(NodeId clipId, float &x, float &y, float &w, float &h) const {
    NodeId transformId = transformNodeForClip(clipId);
    if (transformId == 0) return false;

    auto it = m_transformNodes.find(transformId);
    if (it == m_transformNodes.end()) return false;

    TransformNodeItem *transform = static_cast<TransformNodeItem *>(*it);
    x = transform->x();
    y = transform->y();
    w = transform->w();
    h = transform->h();
    return true;
}

void ClipNodeEditor::setClipTransform(NodeId clipId, float x, float y, float w, float h) {
    NodeId transformId = transformNodeForClip(clipId);
    if (transformId == 0) return;

    auto it = m_transformNodes.find(transformId);
    if (it == m_transformNodes.end()) return;

    TransformNodeItem *transform = static_cast<TransformNodeItem *>(*it);
    transform->setTransform(x, y, w, h);
    emit clipChainChanged();
}

QVector<NodeId> ClipNodeEditor::clipsForContext(NodeId contextId) const {
    return m_scene->clipsForContext(contextId);
}

bool ClipNodeEditor::contextCanvasSize(NodeId clipId, int &w, int &h) const {
    NodeId transformId = transformNodeForClip(clipId);
    if (transformId == 0) return false;

    for (const auto &[contextId, contextPtr] : m_contextNodes.asKeyValueRange()) {
        auto *contextNode = static_cast<TransformContextNodeItem *>(contextPtr);
        const auto clips = m_scene->clipsForContext(contextId);

        for (const auto &cid : clips) {
            NodeId clipTransformId = transformNodeForClip(cid);
            if (clipTransformId == transformId) {
                w = contextNode->canvasW();
                h = contextNode->canvasH();
                return true;
            }
        }
    }
    return false;
}
