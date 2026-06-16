#include "ui/ClipNodeEditor.h"
#include "ui/TransformEditorDialog.h"
#include "ui_TransformNodeDialog.h"
#include "ui_ContextNodeDialog.h"
#include "ui_AudioNodeDialog.h"
#include <QJsonArray>
#include <QJsonObject>
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
#include <QSet>
#include <QCheckBox>
#include <functional>
#include <cmath>
#include "core/VideoPlayer.h"

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
class AudioControllerNodeItem;
class MasterAudioOutputNodeItem;
class ClipNodeScene;

enum class PortKind {
    ChainIn,           // blue, input (on clip nodes)
    ChainOut,          // green, output (on clip nodes)
    ClipTransform,     // purple, clip-facing (on clip nodes)
    TransformClip,     // purple, clip-facing (on transform nodes)
    TransformContext,  // amber, context-facing (on transform nodes)
    ContextHub,        // amber, accepts many (on context nodes)
    AudioOut,          // orange, audio output (on video clip nodes with audio)
    AudioIn,           // orange, audio input (on audio controller nodes)
    AudioControllerOut,// gold, output from audio controller
    MasterAudioIn      // gold, input on master audio output node
};

QColor portKindColor(PortKind kind) {
    switch (kind) {
    case PortKind::ChainIn:        return QColor(0x50, 0xa8, 0xd8); // blue
    case PortKind::ChainOut:       return QColor(0x50, 0xd0, 0x90); // green
    case PortKind::ClipTransform:  return QColor(0xd0, 0x50, 0xb0); // purple
    case PortKind::TransformClip:  return QColor(0xd0, 0x50, 0xb0); // purple
    case PortKind::TransformContext: return QColor(0xd0, 0xb0, 0x50); // amber
    case PortKind::ContextHub:     return QColor(0xd0, 0xb0, 0x50); // amber
    case PortKind::AudioOut:         return QColor(0xe8, 0x80, 0x30); // orange
    case PortKind::AudioIn:          return QColor(0xe8, 0x80, 0x30); // orange
    case PortKind::AudioControllerOut:return QColor(0xe4, 0xb0, 0x42); // gold
    case PortKind::MasterAudioIn:    return QColor(0xe4, 0xb0, 0x42); // gold
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
    if ((a == PortKind::AudioOut && b == PortKind::AudioIn) ||
        (a == PortKind::AudioIn && b == PortKind::AudioOut)) return true;
    if ((a == PortKind::AudioControllerOut && b == PortKind::MasterAudioIn) ||
        (a == PortKind::MasterAudioIn && b == PortKind::AudioControllerOut)) return true;
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
    enum EdgeKind { Chain, ClipToTransform, TransformToContext, AudioToController, ControllerToMaster };

    ConnectionItem(PortItem *from, PortItem *to, EdgeKind kind)
        : QGraphicsPathItem(nullptr), m_from(from), m_to(to), m_kind(kind)
    {
        setZValue(-1);
        setFlag(QGraphicsItem::ItemIsSelectable, false);
        QColor color;
        if (kind == Chain) color = QColor(0x70, 0xb8, 0xff);
        else if (kind == ClipToTransform) color = QColor(0xd0, 0x70, 0xb0);
        else if (kind == TransformToContext) color = QColor(0xd0, 0xb0, 0x70);
        else if (kind == AudioToController) color = QColor(0xe8, 0x80, 0x30);
        else color = QColor(0xf0, 0xc0, 0x50); // ControllerToMaster: gold
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
    ClipNodeItem(ClipNodeModel *model, NodeId id, bool hasAudio = false)
        : m_model(model), m_nodeId(id), m_hasAudio(hasAudio)
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

        if (hasAudio) {
            m_audioPort = new PortItem(PortKind::AudioOut, this);
            m_audioPort->setPos(0, NODE_H / 2);
        }
    }

    NodeId nodeId() const override { return m_nodeId; }
    ClipNodeModel *model() const { return m_model; }
    PortItem *chainInPort()  const { return m_chainInPort; }
    PortItem *chainOutPort() const { return m_chainOutPort; }
    PortItem *transformPort() const { return m_transformPort; }
    PortItem *audioPort() const { return m_audioPort; }
    bool hasAudio() const { return m_hasAudio; }

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
    bool m_hasAudio;
    QGraphicsProxyWidget *m_proxy;
    PortItem *m_chainInPort;
    PortItem *m_chainOutPort;
    PortItem *m_transformPort;
    PortItem *m_audioPort = nullptr;
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

class AudioControllerNodeItem : public NodeItemBase {
public:
    AudioControllerNodeItem(NodeId id, int volume = 100, bool muted = false, AudioPlaybackMode playbackMode = AudioPlaybackMode::Always, int delayMs = 0)
        : m_nodeId(id), m_volume(volume), m_muted(muted), m_playbackMode(playbackMode), m_delayMs(delayMs)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);

        m_audioInPort = new PortItem(PortKind::AudioIn, this);
        m_audioInPort->setPos(SMALL_NODE_W - 10, SMALL_NODE_H / 2);

        m_audioOutPort = new PortItem(PortKind::AudioControllerOut, this);
        m_audioOutPort->setPos(10, SMALL_NODE_H / 2);
    }

    std::function<void(NodeId)> onEditRequested;

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *audioInPort() const { return m_audioInPort; }
    PortItem *audioOutPort() const { return m_audioOutPort; }

    int  volume() const { return m_volume; }
    bool muted()  const { return m_muted; }
    AudioPlaybackMode playbackMode() const { return m_playbackMode; }
    int  delayMs() const { return m_delayMs; }

    void setVolume(int v) { m_volume = v; update(); }
    void setMuted(bool m) { m_muted = m; update(); }
    void setPlaybackMode(AudioPlaybackMode mode) { m_playbackMode = mode; update(); }
    void setDelayMs(int ms) { m_delayMs = ms; update(); }

    QRectF boundingRect() const override { return QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(62, 55, 45), 1));
        p->setBrush(QColor(34, 28, 22));
        p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H), 4, 4);

        p->setPen(QColor(230, 140, 60));
        p->setFont(QFont("Monospace", 8));
        QString modeStr;
        switch (m_playbackMode) {
        case AudioPlaybackMode::DeckAOnly: modeStr = "Deck A"; break;
        case AudioPlaybackMode::DeckBOnly: modeStr = "Deck B"; break;
        case AudioPlaybackMode::Always:    modeStr = "Always"; break;
        }
        QString label = QString("Audio\nVol: %1%\n%2\n[%3]\nDelay: %4ms")
            .arg(m_volume)
            .arg(m_muted ? "Muted" : "Playing")
            .arg(modeStr)
            .arg(m_delayMs);
        p->drawText(QRectF(4, 4, SMALL_NODE_W - 8, 68), Qt::AlignCenter, label);

        QRectF buttonRect(4, 78, SMALL_NODE_W - 8, 24);
        p->setPen(QPen(QColor(230, 140, 60), 1));
        p->setBrush(QColor(90, 45, 10));
        p->drawRoundedRect(buttonRect, 3, 3);
        p->setPen(QColor(255, 185, 120));
        p->setFont(QFont("Monospace", 7));
        p->drawText(buttonRect, Qt::AlignCenter, "Edit");
    }

    QRectF getEditButtonRect() const {
        return QRectF(4, 78, SMALL_NODE_W - 8, 24);
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
    int  m_volume;
    bool m_muted;
    AudioPlaybackMode m_playbackMode;
    int  m_delayMs;
    PortItem *m_audioInPort;
    PortItem *m_audioOutPort;
};

class MasterAudioOutputNodeItem : public NodeItemBase {
public:
    explicit MasterAudioOutputNodeItem(NodeId id)
        : m_nodeId(id)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);

        m_audioInPort = new PortItem(PortKind::MasterAudioIn, this);
        m_audioInPort->setPos(SMALL_NODE_W - 10, SMALL_NODE_H / 2);
    }

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *audioInPort() const { return m_audioInPort; }

    QRectF boundingRect() const override { return QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(86, 65, 18), 1));
        p->setBrush(QColor(45, 34, 12));
        p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H), 4, 4);

        p->setPen(QColor(245, 206, 88));
        p->setFont(QFont("Monospace", 8, QFont::Bold));
        p->drawText(QRectF(6, 10, SMALL_NODE_W - 12, SMALL_NODE_H - 20),
                    Qt::AlignCenter, "Master Audio\nOutput");
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    NodeId m_nodeId;
    PortItem *m_audioInPort = nullptr;
};

class ClipNodeScene : public QGraphicsScene {
public:
    explicit ClipNodeScene(QObject *parent = nullptr)
        : QGraphicsScene(parent)
    {
        setBackgroundBrush(QColor(20, 21, 23));
    }

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override {
        // Fill base background
        painter->fillRect(rect, QColor(20, 21, 23));

        // Use cosmetic pens so line widths are constant regardless of zoom level
        QPen minorPen(QColor(26, 27, 30), 0);
        minorPen.setCosmetic(true);

        QPen majorPen(QColor(33, 35, 38), 0);
        majorPen.setCosmetic(true);

        const int gridSize = 20;
        const int majorGridSize = 100;

        qreal left = std::floor(rect.left() / gridSize) * gridSize;
        qreal top = std::floor(rect.top() / gridSize) * gridSize;

        QVector<QLineF> minorLines;
        QVector<QLineF> majorLines;

        for (qreal x = left; x < rect.right(); x += gridSize) {
            long long ix = std::llround(x);
            if (ix % majorGridSize == 0) {
                majorLines.append(QLineF(x, rect.top(), x, rect.bottom()));
            } else {
                minorLines.append(QLineF(x, rect.top(), x, rect.bottom()));
            }
        }

        for (qreal y = top; y < rect.bottom(); y += gridSize) {
            long long iy = std::llround(y);
            if (iy % majorGridSize == 0) {
                majorLines.append(QLineF(rect.left(), y, rect.right(), y));
            } else {
                minorLines.append(QLineF(rect.left(), y, rect.right(), y));
            }
        }

        painter->setPen(minorPen);
        painter->drawLines(minorLines);

        painter->setPen(majorPen);
        painter->drawLines(majorLines);
    }

public:
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
        } else if (src->kind() == PortKind::AudioOut || src->kind() == PortKind::AudioIn) {
            outPort = (src->kind() == PortKind::AudioOut) ? src : dst;
            inPort  = (src->kind() == PortKind::AudioIn)  ? src : dst;
            kind = ConnectionItem::AudioToController;
        } else if (src->kind() == PortKind::AudioControllerOut || src->kind() == PortKind::MasterAudioIn) {
            outPort = (src->kind() == PortKind::AudioControllerOut) ? src : dst;
            inPort  = (src->kind() == PortKind::MasterAudioIn)      ? src : dst;
            kind = ConnectionItem::ControllerToMaster;
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

    NodeId downstreamOf(NodeId nodeId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::Chain && e.fromNodeId == nodeId)
                return e.toNodeId;
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

    NodeId audioNodeForClip(NodeId clipId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::AudioToController && e.fromNodeId == clipId)
                return e.toNodeId;
        return 0;
    }

    NodeId clipForAudioNode(NodeId audioNodeId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::AudioToController && e.toNodeId == audioNodeId)
                return e.fromNodeId;
        return 0;
    }

    NodeId masterNodeForAudioController(NodeId audioNodeId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::ControllerToMaster && e.fromNodeId == audioNodeId)
                return e.toNodeId;
        return 0;
    }

    QJsonArray edgesToJson() const {
        QJsonArray arr;
        for (const auto &e : m_edges) {
            QJsonObject obj;
            obj["from"] = (qint64)e.fromNodeId;
            obj["to"]   = (qint64)e.toNodeId;
            obj["kind"] = (int)e.edgeKind;
            arr.append(obj);
        }
        return arr;
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

QVariant AudioControllerNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

QVariant MasterAudioOutputNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
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
    m_scene->onConnectionChanged = [this]() {
        emit clipChainChanged();
        emit audioGraphChanged();
    };

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
    const bool hasAudio = VideoPlayer::fileHasAudio(path);

    const NodeId id = m_nextId++;
    auto *model = new ClipNodeModel(this);
    auto *nodeItem = new ClipNodeItem(model, id, hasAudio);

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

    if (hasAudio) {
        auto *audioNode = new AudioControllerNodeItem(m_nextId++);
        const NodeId audioId = audioNode->nodeId();
        audioNode->setPos(nodeItem->pos().x() - 150.0,
                          nodeItem->pos().y() + NODE_H / 2.0 - SMALL_NODE_H / 2.0);
        audioNode->onEditRequested = [this](NodeId aid) { onEditAudioNode(aid); };
        m_scene->addItem(audioNode);
        m_audioNodes[audioId] = static_cast<void *>(audioNode);

        m_scene->createConnectionManually(nodeItem->audioPort(), audioNode->audioInPort(),
                                          ConnectionItem::AudioToController);
    }

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

    for (auto it = m_audioNodes.begin(); it != m_audioNodes.end(); ++it) {
        auto *an = static_cast<AudioControllerNodeItem *>(*it);
        m_scene->removeConnectionsForNode(an->nodeId());
        m_scene->removeItem(an);
        delete an;
    }
    m_audioNodes.clear();

    for (auto it = m_masterAudioNodes.begin(); it != m_masterAudioNodes.end(); ++it) {
        auto *mn = static_cast<MasterAudioOutputNodeItem *>(*it);
        m_scene->removeConnectionsForNode(mn->nodeId());
        m_scene->removeItem(mn);
        delete mn;
    }
    m_masterAudioNodes.clear();
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
    menu.addAction("Add Master Audio Output", this, &ClipNodeEditor::onAddMasterAudioOutput);
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

void ClipNodeEditor::onAddMasterAudioOutput() {
    auto *masterNode = new MasterAudioOutputNodeItem(m_nextId++);
    masterNode->setPos(m_view->mapToScene(m_view->mapFromGlobal(QCursor::pos())));
    m_scene->addItem(masterNode);
    m_masterAudioNodes[masterNode->nodeId()] = static_cast<void *>(masterNode);
    emit clipChainChanged();
    emit audioGraphChanged();
}

void ClipNodeEditor::onEditTransformNode(NodeId nodeId) {
    auto it = m_transformNodes.find(nodeId);
    if (it == m_transformNodes.end()) return;

    TransformNodeItem *transform = static_cast<TransformNodeItem *>(*it);

    QDialog dialog(this);
    Ui::TransformNodeDialog ui;
    ui.setupUi(&dialog);

    ui.xSpin->setValue(transform->x() * 100.0);
    ui.ySpin->setValue(transform->y() * 100.0);
    ui.wSpin->setValue(transform->w() * 100.0);
    ui.hSpin->setValue(transform->h() * 100.0);

    if (dialog.exec() == QDialog::Accepted) {
        transform->setTransform(
            (float)(ui.xSpin->value() / 100.0),
            (float)(ui.ySpin->value() / 100.0),
            (float)(ui.wSpin->value() / 100.0),
            (float)(ui.hSpin->value() / 100.0));
        emit clipChainChanged();
    }
}

void ClipNodeEditor::onEditContextNode(NodeId nodeId) {
    auto it = m_contextNodes.find(nodeId);
    if (it == m_contextNodes.end()) return;

    TransformContextNodeItem *context = static_cast<TransformContextNodeItem *>(*it);

    QDialog dialog(this);
    Ui::ContextNodeDialog ui;
    ui.setupUi(&dialog);

    ui.wSpin->setValue(context->canvasW());
    ui.hSpin->setValue(context->canvasH());

    connect(ui.presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, [&ui](int idx) {
        if      (idx == 1) { ui.wSpin->setValue(1280); ui.hSpin->setValue(720);  }
        else if (idx == 2) { ui.wSpin->setValue(1920); ui.hSpin->setValue(1080); }
        else if (idx == 3) { ui.wSpin->setValue(1024); ui.hSpin->setValue(768);  }
    });

    if (dialog.exec() == QDialog::Accepted) {
        context->setCanvasSize(ui.wSpin->value(), ui.hSpin->value());
        emit clipChainChanged();
    }
}

void ClipNodeEditor::onOpenTransformEditor(NodeId contextId) {
    TransformEditorDialog dialog(contextId, this, this);
    dialog.exec();
}

void ClipNodeEditor::onEditAudioNode(NodeId nodeId) {
    auto it = m_audioNodes.find(nodeId);
    if (it == m_audioNodes.end()) return;

    AudioControllerNodeItem *audio = static_cast<AudioControllerNodeItem *>(*it);

    QDialog dialog(this);
    Ui::AudioNodeDialog ui;
    ui.setupUi(&dialog);

    ui.volumeSpin->setValue(audio->volume());
    ui.mutedCheck->setChecked(audio->muted());
    // modeCombo order matches AudioPlaybackMode enum: DeckAOnly=0, DeckBOnly=1, Always=2
    ui.modeCombo->setCurrentIndex((int)audio->playbackMode());
    ui.delaySpin->setValue(audio->delayMs());

    if (dialog.exec() == QDialog::Accepted) {
        audio->setVolume(ui.volumeSpin->value());
        audio->setMuted(ui.mutedCheck->isChecked());
        audio->setPlaybackMode((AudioPlaybackMode)ui.modeCombo->currentIndex());
        audio->setDelayMs(ui.delaySpin->value());
        const NodeId clipId = m_scene->clipForAudioNode(nodeId);
        if (clipId != 0) emit audioControllerChanged(clipId);
    }
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

QVector<NodeId> ClipNodeEditor::clipsForContextOrdered(NodeId contextId) const {
    const QVector<NodeId> unordered = m_scene->clipsForContext(contextId);
    if (unordered.size() <= 1) return unordered;

    QSet<NodeId> clipSet(unordered.begin(), unordered.end());
    QVector<NodeId> ordered;
    ordered.reserve(unordered.size());

    // Follow each chain head (clip whose upstream is absent from the context set)
    // downstream to collect clips in chain order.
    for (NodeId clipId : unordered) {
        NodeId up = m_scene->upstreamOf(clipId);
        if (up == 0 || !clipSet.contains(up)) {
            NodeId current = clipId;
            while (current != 0 && clipSet.contains(current) && !ordered.contains(current)) {
                ordered.append(current);
                current = m_scene->downstreamOf(current);
            }
        }
    }

    // Append any clips not reachable from any chain head (isolated / unchained)
    for (NodeId clipId : unordered) {
        if (!ordered.contains(clipId))
            ordered.append(clipId);
    }

    return ordered;
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

bool ClipNodeEditor::audioSettingsForClip(NodeId clipId, int &volume, bool &muted, bool &routedToMaster, AudioPlaybackMode &playbackMode, int &delayMs) const {
    const NodeId audioNodeId = m_scene->audioNodeForClip(clipId);
    if (audioNodeId == 0) return false;

    auto it = m_audioNodes.find(audioNodeId);
    if (it == m_audioNodes.end()) return false;

    auto *audioNode = static_cast<AudioControllerNodeItem *>(*it);
    volume = audioNode->volume();
    muted = audioNode->muted();
    routedToMaster = (m_scene->masterNodeForAudioController(audioNodeId) != 0);
    playbackMode = audioNode->playbackMode();
    delayMs = audioNode->delayMs();
    return true;
}

// ── Session persistence ───────────────────────────────────────────────────────

static QJsonObject descriptorToJson(const SourceDescriptor &d) {
    QJsonObject o;
    o["kind"]               = (int)d.kind;
    o["path"]               = d.path;
    o["displayName"]        = d.displayName;
    o["color"]              = d.color.name(QColor::HexArgb);
    o["cameraIndex"]        = d.cameraIndex;
    o["screenIndex"]        = d.screenIndex;
    o["windowIndex"]        = d.windowIndex;
    o["slideshowIntervalMs"]= d.slideshowIntervalMs;
    o["canvasWidth"]        = d.canvasWidth;
    o["canvasHeight"]       = d.canvasHeight;
    o["canvasFill"]         = (int)d.canvasFill;
    o["shaderCode"]         = d.shaderCode;
    o["htmlContent"]        = d.htmlContent;
    return o;
}

static SourceDescriptor descriptorFromJson(const QJsonObject &o) {
    SourceDescriptor d;
    d.kind               = (SourceDescriptor::Kind)o["kind"].toInt();
    d.path               = o["path"].toString();
    d.displayName        = o["displayName"].toString();
    d.color              = QColor(o["color"].toString("#ffffffff"));
    d.cameraIndex        = o["cameraIndex"].toInt();
    d.screenIndex        = o["screenIndex"].toInt();
    d.windowIndex        = o["windowIndex"].toInt();
    d.slideshowIntervalMs= o["slideshowIntervalMs"].toInt(3000);
    d.canvasWidth        = o["canvasWidth"].toInt(1280);
    d.canvasHeight       = o["canvasHeight"].toInt(720);
    d.canvasFill         = (SourceDescriptor::CanvasFill)o["canvasFill"].toInt();
    d.shaderCode         = o["shaderCode"].toString();
    d.htmlContent        = o["htmlContent"].toString();
    return d;
}

QJsonObject ClipNodeEditor::saveState() const {
    QJsonObject root;
    root["nextId"] = (qint64)m_nextId;

    // ── Clip nodes ────────────────────────────────────────────────────────────
    QJsonArray clipNodes;
    for (auto it = m_nodeMap.cbegin(); it != m_nodeMap.cend(); ++it) {
        const NodeId       id    = it.key();
        const ClipNodeModel *model = it.value();
        QJsonObject nodeObj;
        nodeObj["id"] = (qint64)id;

        // Scene position
        for (auto *item : m_scene->items()) {
            if (auto *ci = dynamic_cast<ClipNodeItem *>(item)) {
                if (ci->nodeId() == id) {
                    nodeObj["posX"]     = ci->pos().x();
                    nodeObj["posY"]     = ci->pos().y();
                    nodeObj["hasAudio"] = ci->hasAudio();
                    break;
                }
            }
        }

        // Paired transform node
        const NodeId transId = m_scene->transformNodeForClip(id);
        nodeObj["transformId"] = (qint64)transId;
        auto tit = m_transformNodes.find(transId);
        if (tit != m_transformNodes.end()) {
            auto *tn = static_cast<TransformNodeItem *>(*tit);
            nodeObj["transformPosX"] = tn->pos().x();
            nodeObj["transformPosY"] = tn->pos().y();
            nodeObj["transformX"]    = (double)tn->x();
            nodeObj["transformY"]    = (double)tn->y();
            nodeObj["transformW"]    = (double)tn->w();
            nodeObj["transformH"]    = (double)tn->h();
        }

        nodeObj["source"]   = descriptorToJson(model->sourceDescriptor());
        nodeObj["settings"] = model->settings().toJson();
        nodeObj["repeat"]   = model->isRepeat();

        clipNodes.append(nodeObj);
    }
    root["clipNodes"] = clipNodes;

    // ── Context nodes ─────────────────────────────────────────────────────────
    QJsonArray contextNodes;
    for (auto it = m_contextNodes.cbegin(); it != m_contextNodes.cend(); ++it) {
        auto *cn = static_cast<TransformContextNodeItem *>(*it);
        QJsonObject obj;
        obj["id"]      = (qint64)cn->nodeId();
        obj["posX"]    = cn->pos().x();
        obj["posY"]    = cn->pos().y();
        obj["canvasW"] = cn->canvasW();
        obj["canvasH"] = cn->canvasH();
        contextNodes.append(obj);
    }
    root["contextNodes"] = contextNodes;

    // ── Audio controller nodes ────────────────────────────────────────────────
    QJsonArray audioNodes;
    for (auto it = m_audioNodes.cbegin(); it != m_audioNodes.cend(); ++it) {
        auto *an = static_cast<AudioControllerNodeItem *>(*it);
        QJsonObject obj;
        obj["id"]           = (qint64)an->nodeId();
        obj["posX"]         = an->pos().x();
        obj["posY"]         = an->pos().y();
        obj["volume"]       = an->volume();
        obj["muted"]        = an->muted();
        obj["playbackMode"] = (int)an->playbackMode();
        obj["delayMs"]      = an->delayMs();
        audioNodes.append(obj);
    }
    root["audioNodes"] = audioNodes;

    QJsonArray masterAudioNodes;
    for (auto it = m_masterAudioNodes.cbegin(); it != m_masterAudioNodes.cend(); ++it) {
        auto *mn = static_cast<MasterAudioOutputNodeItem *>(*it);
        QJsonObject obj;
        obj["id"]   = (qint64)mn->nodeId();
        obj["posX"] = mn->pos().x();
        obj["posY"] = mn->pos().y();
        masterAudioNodes.append(obj);
    }
    root["masterAudioNodes"] = masterAudioNodes;

    // ── All edges (chain + transform + context wires) ─────────────────────────
    root["connections"] = m_scene->edgesToJson();

    return root;
}

PortItem *ClipNodeEditor::findPort(NodeId nodeId, int portKindInt) const {
    const PortKind kind = (PortKind)portKindInt;
    for (auto *item : m_scene->items()) {
        if (auto *ci = dynamic_cast<ClipNodeItem *>(item)) {
            if (ci->nodeId() != nodeId) continue;
            if (kind == PortKind::ChainIn)       return ci->chainInPort();
            if (kind == PortKind::ChainOut)      return ci->chainOutPort();
            if (kind == PortKind::ClipTransform) return ci->transformPort();
            if (kind == PortKind::AudioOut)      return ci->audioPort();
        }
        if (auto *ti = dynamic_cast<TransformNodeItem *>(item)) {
            if (ti->nodeId() != nodeId) continue;
            if (kind == PortKind::TransformClip)    return ti->clipPort();
            if (kind == PortKind::TransformContext)  return ti->contextPort();
        }
        if (auto *xi = dynamic_cast<TransformContextNodeItem *>(item)) {
            if (xi->nodeId() != nodeId) continue;
            if (kind == PortKind::ContextHub) return xi->hubPort();
        }
        if (auto *ai = dynamic_cast<AudioControllerNodeItem *>(item)) {
            if (ai->nodeId() != nodeId) continue;
            if (kind == PortKind::AudioIn) return ai->audioInPort();
            if (kind == PortKind::AudioControllerOut) return ai->audioOutPort();
        }
        if (auto *mi = dynamic_cast<MasterAudioOutputNodeItem *>(item)) {
            if (mi->nodeId() != nodeId) continue;
            if (kind == PortKind::MasterAudioIn) return mi->audioInPort();
        }
    }
    return nullptr;
}

void ClipNodeEditor::restoreState(const QJsonObject &state) {
    clearAllNodes();

    // ── Clip nodes ────────────────────────────────────────────────────────────
    const QJsonArray clipNodes = state["clipNodes"].toArray();
    for (const auto &val : clipNodes) {
        const QJsonObject obj = val.toObject();
        const NodeId clipId  = (NodeId)obj["id"].toInteger();
        const NodeId transId = (NodeId)obj["transformId"].toInteger();

        // Create clip node item with the saved ID.
        m_nextId = clipId;
        const NodeId id = m_nextId++;

        const bool hasAudio = obj["hasAudio"].toBool(false);
        auto *model    = new ClipNodeModel(this);
        auto *nodeItem = new ClipNodeItem(model, id, hasAudio);
        nodeItem->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        m_scene->addItem(nodeItem);
        m_nodeMap[id] = model;

        // Restore source
        const SourceDescriptor desc = descriptorFromJson(obj["source"].toObject());
        model->setNodeId(id);
        using Kind = SourceDescriptor::Kind;
        if (desc.kind == Kind::VideoFile || desc.kind == Kind::Image)
            model->loadClip(desc.path, QPixmap{});
        else
            model->loadSource(desc, QPixmap{});

        // Restore clip settings, repeat, mute
        if (obj.contains("settings"))
            model->applySettings(ClipSettings::fromJson(obj["settings"].toObject()));
        if (obj["repeat"].toBool()) model->setRepeat(true);

        connectNodeSignals(model, id);

        // Create paired transform node with saved ID.
        m_nextId = transId;
        auto *tn = new TransformNodeItem(m_nextId++,
            (float)obj["transformX"].toDouble(0.0),
            (float)obj["transformY"].toDouble(0.0),
            (float)obj["transformW"].toDouble(1.0),
            (float)obj["transformH"].toDouble(1.0));
        tn->setPos(obj["transformPosX"].toDouble(), obj["transformPosY"].toDouble());
        tn->onEditRequested = [this](NodeId tid) { onEditTransformNode(tid); };
        m_scene->addItem(tn);
        m_transformNodes[transId] = static_cast<void *>(tn);

        // Wire clip ↔ transform (ClipToTransform edge).
        m_scene->createConnectionManually(nodeItem->transformPort(), tn->clipPort(),
                                          ConnectionItem::ClipToTransform);

        emit nodeAdded(id);
    }

    // ── Context nodes ─────────────────────────────────────────────────────────
    const QJsonArray contextNodes = state["contextNodes"].toArray();
    for (const auto &val : contextNodes) {
        const QJsonObject obj = val.toObject();
        const NodeId ctxId = (NodeId)obj["id"].toInteger();
        m_nextId = ctxId;
        auto *cn = new TransformContextNodeItem(m_nextId++,
            obj["canvasW"].toInt(1280), obj["canvasH"].toInt(720));
        cn->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        cn->onEditRequested        = [this](NodeId cid) { onEditContextNode(cid); };
        cn->onOpenEditorRequested  = [this](NodeId cid) { onOpenTransformEditor(cid); };
        m_scene->addItem(cn);
        m_contextNodes[ctxId] = static_cast<void *>(cn);
    }

    // ── Audio controller nodes ────────────────────────────────────────────────
    const QJsonArray audioNodes = state["audioNodes"].toArray();
    for (const auto &val : audioNodes) {
        const QJsonObject obj = val.toObject();
        const NodeId audioId = (NodeId)obj["id"].toInteger();
        m_nextId = audioId;
        const AudioPlaybackMode mode = (AudioPlaybackMode)obj["playbackMode"].toInt((int)AudioPlaybackMode::Always);
        auto *an = new AudioControllerNodeItem(m_nextId++,
            obj["volume"].toInt(100), obj["muted"].toBool(false), mode, obj["delayMs"].toInt(0));
        an->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        an->onEditRequested = [this](NodeId aid) { onEditAudioNode(aid); };
        m_scene->addItem(an);
        m_audioNodes[audioId] = static_cast<void *>(an);
    }

    const QJsonArray masterAudioNodes = state["masterAudioNodes"].toArray();
    for (const auto &val : masterAudioNodes) {
        const QJsonObject obj = val.toObject();
        const NodeId masterId = (NodeId)obj["id"].toInteger();
        m_nextId = masterId;
        auto *mn = new MasterAudioOutputNodeItem(m_nextId++);
        mn->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        m_scene->addItem(mn);
        m_masterAudioNodes[masterId] = static_cast<void *>(mn);
    }

    // ── Extra edges (chain + transform-to-context; skip ClipToTransform already wired) ──
    const QJsonArray conns = state["connections"].toArray();
    for (const auto &val : conns) {
        const QJsonObject obj = val.toObject();
        const NodeId from = (NodeId)obj["from"].toInteger();
        const NodeId to   = (NodeId)obj["to"].toInteger();
        const int    kind = obj["kind"].toInt();

        if (kind == ConnectionItem::ClipToTransform) continue;

        PortItem *fromPort = nullptr, *toPort = nullptr;
        if (kind == ConnectionItem::Chain) {
            fromPort = findPort(from, (int)PortKind::ChainOut);
            toPort   = findPort(to,   (int)PortKind::ChainIn);
        } else if (kind == ConnectionItem::AudioToController) {
            fromPort = findPort(from, (int)PortKind::AudioOut);
            toPort   = findPort(to,   (int)PortKind::AudioIn);
        } else if (kind == ConnectionItem::ControllerToMaster) {
            fromPort = findPort(from, (int)PortKind::AudioControllerOut);
            toPort   = findPort(to,   (int)PortKind::MasterAudioIn);
        } else {
            fromPort = findPort(from, (int)PortKind::TransformContext);
            toPort   = findPort(to,   (int)PortKind::ContextHub);
        }
        if (fromPort && toPort)
            m_scene->createConnectionManually(fromPort, toPort,
                                              (ConnectionItem::EdgeKind)kind);
    }

    // Advance nextId past all IDs used in this session.
    const qint64 savedNext = state["nextId"].toInteger(0);
    if (savedNext > (qint64)m_nextId)
        m_nextId = (NodeId)savedNext;
}
