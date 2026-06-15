#include "ui/ClipNodeEditor.h"
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QWheelEvent>
#include <QDebug>
#include <functional>
#include <cmath>

// ── Node layout constants ─────────────────────────────────────────────────────
// ClipCard has a fixed size of 122×176 (from ClipCard.ui).
static constexpr qreal CARD_W   = 122.0;
static constexpr qreal CARD_H   = 176.0;
static constexpr qreal PORT_R   = 7.0;    // port circle radius
static constexpr qreal HEADER_H = 18.0;   // drag-handle strip above the card

// Derived geometry (all relative to the ClipNodeItem's local coordinate origin)
static constexpr qreal PROXY_Y    = PORT_R + HEADER_H;       // top-left of embedded card
static constexpr qreal IN_PORT_Y  = PORT_R;                   // input port centre y
static constexpr qreal OUT_PORT_Y = PROXY_Y + CARD_H + PORT_R; // output port centre y
static constexpr qreal PORT_X     = CARD_W / 2.0;             // port centre x (both ports)
static constexpr qreal NODE_W     = CARD_W;
static constexpr qreal NODE_H     = OUT_PORT_Y + PORT_R;      // total node height

// ── Forward declarations ──────────────────────────────────────────────────────
class PortItem;
class ClipNodeItem;
class ClipNodeScene;

// ─────────────────────────────────────────────────────────────────────────────
// PortItem — a small circle drawn at the top (input) or bottom (output) of a node.
// Handles mouse events to initiate / complete connection drags.
// ─────────────────────────────────────────────────────────────────────────────
class PortItem : public QGraphicsEllipseItem {
public:
    enum PortRole { Input, Output };

    // Constructor body is defined after ClipNodeItem is fully declared (so the
    // compiler knows ClipNodeItem → QGraphicsItem and can accept it as parent).
    PortItem(PortRole role, ClipNodeItem *parentNode);

    PortRole role() const { return m_role; }
    ClipNodeItem *nodeItem() const { return m_nodeItem; }
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
    PortRole m_role;
    ClipNodeItem *m_nodeItem;
    bool m_connected = false;

    void refreshAppearance(bool hovered = false) {
        QColor base = (m_role == Input) ? QColor(0x50, 0xa8, 0xd8)
                                        : QColor(0x50, 0xd0, 0x90);
        if (m_connected) base = base.lighter(115);
        if (hovered)     base = base.lighter(145);
        setPen(QPen(base.darker(160), 1.5));
        setBrush(base);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ConnectionItem — bezier curve drawn between an output port and an input port.
// ─────────────────────────────────────────────────────────────────────────────
class ConnectionItem : public QGraphicsPathItem {
public:
    ConnectionItem(PortItem *from, PortItem *to)
        : QGraphicsPathItem(nullptr)
        , m_from(from)
        , m_to(to)
    {
        setZValue(-1);
        setFlag(QGraphicsItem::ItemIsSelectable, false);
        setPen(QPen(QColor(0x70, 0xb8, 0xff), 2.5,
                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        updatePath();
    }

    PortItem *fromPort() const { return m_from; } // output port
    PortItem *toPort()   const { return m_to;   } // input port

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
};

// ─────────────────────────────────────────────────────────────────────────────
// ClipNodeItem — the visual node. Contains:
//   • a header strip (drag handle)
//   • an input PortItem at the top centre
//   • a QGraphicsProxyWidget embedding the ClipCard
//   • an output PortItem at the bottom centre
// ─────────────────────────────────────────────────────────────────────────────
class ClipNodeItem : public QGraphicsItem {
public:
    ClipNodeItem(ClipNodeModel *model, NodeId id)
        : m_model(model)
        , m_nodeId(id)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);

        auto *card = new ClipCard(0);
        model->setCard(card);

        m_proxy = new QGraphicsProxyWidget(this);
        m_proxy->setWidget(card);
        m_proxy->setPos(0, PROXY_Y);

        m_inputPort  = new PortItem(PortItem::Input,  this);
        m_inputPort->setPos(PORT_X, IN_PORT_Y);

        m_outputPort = new PortItem(PortItem::Output, this);
        m_outputPort->setPos(PORT_X, OUT_PORT_Y);
    }

    NodeId         nodeId()     const { return m_nodeId; }
    ClipNodeModel *model()      const { return m_model; }
    PortItem      *inputPort()  const { return m_inputPort; }
    PortItem      *outputPort() const { return m_outputPort; }

    QRectF boundingRect() const override { return QRectF(0, 0, NODE_W, NODE_H); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);

        // Node background
        p->setPen(QPen(QColor(55, 56, 62), 1));
        p->setBrush(QColor(30, 31, 34));
        p->drawRoundedRect(QRectF(0, 0, NODE_W, NODE_H), 6, 6);

        // Header strip (rounded top, square bottom)
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

        // Drag indicator dots in header
        p->setPen(QColor(90, 95, 110));
        const qreal cy = PORT_R + HEADER_H / 2.0;
        for (int i = -1; i <= 1; ++i) {
            p->drawEllipse(QPointF(NODE_W / 2.0 + i * 8, cy), 2, 2);
        }
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    ClipNodeModel       *m_model;
    NodeId               m_nodeId;
    QGraphicsProxyWidget *m_proxy;
    PortItem            *m_inputPort;
    PortItem            *m_outputPort;
};

// ─────────────────────────────────────────────────────────────────────────────
// ClipNodeScene — manages connection state and port-drag interactions.
// ─────────────────────────────────────────────────────────────────────────────
class ClipNodeScene : public QGraphicsScene {
public:
    explicit ClipNodeScene(QObject *parent = nullptr)
        : QGraphicsScene(parent)
    {
        setBackgroundBrush(QColor(20, 21, 23));
    }

    std::function<void()> onConnectionChanged;

    // Called by PortItem mouse events
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
        m_dragPort    = nullptr;
        if (!src) return;

        // Find a PortItem under the cursor
        PortItem *dst = nullptr;
        for (auto *item : items(scenePos)) {
            if (auto *p = dynamic_cast<PortItem *>(item)) { dst = p; break; }
        }
        if (!dst || dst == src) return;
        if (dst->nodeItem() == src->nodeItem()) return; // same node
        if (dst->role()     == src->role())     return; // same port type

        PortItem *outPort = (src->role() == PortItem::Output) ? src : dst;
        PortItem *inPort  = (src->role() == PortItem::Input)  ? src : dst;

        // Enforce one-to-one: disconnect any existing connection on either port
        disconnectPort(outPort);
        disconnectPort(inPort);

        createConnection(outPort, inPort);
    }

    // Called by ClipNodeItem::itemChange when a node moves
    void updateConnectionsForNode(ClipNodeItem *nodeItem) {
        for (auto &e : m_edges) {
            if (e.upstreamId == nodeItem->nodeId() || e.downstreamId == nodeItem->nodeId())
                e.item->updatePath();
        }
    }

    void removeConnectionsForNode(NodeId nodeId) {
        for (int i = m_edges.size() - 1; i >= 0; --i) {
            auto &e = m_edges[i];
            if (e.upstreamId == nodeId || e.downstreamId == nodeId) {
                e.item->fromPort()->setConnected(false);
                e.item->toPort()->setConnected(false);
                removeItem(e.item);
                delete e.item;
                m_edges.removeAt(i);
                if (onConnectionChanged) onConnectionChanged();
            }
        }
    }

    // Returns the NodeId of the node connected to nodeId's input port, or 0 if none.
    NodeId upstreamOf(NodeId nodeId) const {
        for (const auto &e : m_edges)
            if (e.downstreamId == nodeId) return e.upstreamId;
        return 0;
    }

private:
    struct Edge {
        NodeId upstreamId;
        NodeId downstreamId;
        ConnectionItem *item;
    };

    void createConnection(PortItem *outPort, PortItem *inPort) {
        auto *item = new ConnectionItem(outPort, inPort);
        addItem(item);
        m_edges.append({ outPort->nodeItem()->nodeId(),
                         inPort->nodeItem()->nodeId(),
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

    QList<Edge>        m_edges;
    PortItem          *m_dragPort = nullptr;
    QGraphicsLineItem *m_tempLine = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// PortItem method bodies (need ClipNodeItem to be fully defined first)
// ─────────────────────────────────────────────────────────────────────────────
PortItem::PortItem(PortRole role, ClipNodeItem *parentNode)
    : QGraphicsEllipseItem(-PORT_R, -PORT_R, PORT_R * 2, PORT_R * 2, parentNode)
    , m_role(role)
    , m_nodeItem(parentNode)
{
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

// ─────────────────────────────────────────────────────────────────────────────
// ClipNodeItem::itemChange (needs ClipNodeScene)
// ─────────────────────────────────────────────────────────────────────────────
QVariant ClipNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

// ─────────────────────────────────────────────────────────────────────────────
// ConnectionItem helpers (need ClipNodeItem)
// ─────────────────────────────────────────────────────────────────────────────
NodeId ConnectionItem::fromNodeId() const { return m_from->nodeItem()->nodeId(); }
NodeId ConnectionItem::toNodeId()   const { return m_to->nodeItem()->nodeId();   }

// ─────────────────────────────────────────────────────────────────────────────
// ClipNodeView — adds middle-click panning and scroll-wheel zoom to QGraphicsView.
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// ClipNodeEditor
// ─────────────────────────────────────────────────────────────────────────────

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
}

ClipNodeEditor::~ClipNodeEditor() = default;

ClipNodeModel *ClipNodeEditor::addClipNode(const QString &path, const QPixmap &thumbnail) {
    const NodeId id = m_nextId++;
    auto *model     = new ClipNodeModel(this);

    auto *nodeItem = new ClipNodeItem(model, id);

    const int idx  = m_nodeMap.size();
    nodeItem->setPos(idx * 160.0 + 20.0, idx * 60.0 + 20.0);

    m_scene->addItem(nodeItem);
    m_nodeMap[id] = model;

    model->loadClip(path, thumbnail);
    connectNodeSignals(model, id);

    return model;
}

ClipNodeModel *ClipNodeEditor::addSourceNode(const SourceDescriptor &desc, const QPixmap &thumbnail) {
    const NodeId id = m_nextId++;
    auto *model     = new ClipNodeModel(this);

    auto *nodeItem = new ClipNodeItem(model, id);

    const int idx = m_nodeMap.size();
    nodeItem->setPos(idx * 160.0 + 20.0, idx * 60.0 + 20.0);

    m_scene->addItem(nodeItem);
    m_nodeMap[id] = model;

    model->loadSource(desc, thumbnail);
    connectNodeSignals(model, id);

    return model;
}

void ClipNodeEditor::removeNode(NodeId nodeId) {
    auto it = m_nodeMap.find(nodeId);
    if (it == m_nodeMap.end()) return;

    ClipNodeModel *model = *it;
    m_nodeMap.erase(it);

    // Remove all connections involving this node
    m_scene->removeConnectionsForNode(nodeId);

    // Find and delete the scene item (which also deletes the embedded ClipCard)
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
