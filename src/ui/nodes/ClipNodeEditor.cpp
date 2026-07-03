#include "ui/nodes/ClipNodeEditor.h"
#include "ui/common/MaterialSymbols.h"
#include "core/project/AssetPathResolver.h"
#include "core/scripting/ScriptRuntime.h"
#include "ui/editors/ScriptEditDialog.h"
#include "ui/canvas/TransformEditorDialog.h"
#include "ui/canvas/CropSelectorWidget.h"
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
#include <QMediaDevices>
#include <QAudioDevice>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMenu>
#include <QCursor>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QGraphicsSceneContextMenuEvent>
#include <QSet>
#include <QCheckBox>
#include <functional>
#include <cmath>
#include <QObject>
#include "core/media/VideoPlayer.h"
#include "core/media/ThumbnailExtractor.h"
#include "core/project/ClipManager.h"
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QToolButton>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QtMath>
#include <algorithm>

static constexpr qreal CARD_W        = 122.0;
static constexpr qreal CARD_PROXY_H  = 175.0;
static constexpr qreal PORT_R        = 4.5;
static constexpr qreal PORT_HIT_R    = 8.0;
static constexpr qreal HEADER_H      = 16.0;
static constexpr qreal PROXY_Y       = PORT_R + HEADER_H;
static constexpr qreal IN_PORT_Y     = PORT_R;
static constexpr qreal PORT_X        = CARD_W / 2.0;
static constexpr qreal NODE_W        = CARD_W;

static constexpr qreal SMALL_NODE_W = 100.0;
static constexpr qreal SMALL_NODE_H = 110.0;
static constexpr qreal AUDIO_STRIP_H = 54.0;

static const QColor kSelectionAccent(0x4a, 0x9e, 0xff);

class PortItem;
class NodeItemBase;
class ClipNodeItem;
class ProcessNodeItem;
class LayerNodeItem;
class AbSelectNodeItem;
class OutputNodeItem;
class ScriptNodeItem;
class MasterAudioOutputNodeItem;
class MasterAudioInputNodeItem;
class ClipNodeScene;
class ClipNodeView;

enum class PortKind {
    ChainIn,
    ChainOut,
    AbOut,
    AbIn,
    AudioOut,
    ShaderAudioIn,
    AudioIn,
    AudioControllerOut,
    MasterAudioIn,
    MasterAudioInputOut,
    ScriptOut,
    DataIn
};

QColor portKindColor(PortKind kind) {
    switch (kind) {
    case PortKind::ChainIn:            return QColor(0x50, 0xa8, 0xd8);
    case PortKind::ChainOut:           return QColor(0x50, 0xa8, 0xd8);
    case PortKind::AbOut:              return QColor(0xe0, 0x50, 0x50);
    case PortKind::AbIn:               return QColor(0xe0, 0x50, 0x50);
    case PortKind::AudioOut:           return QColor(0xe8, 0x80, 0x30);
    case PortKind::ShaderAudioIn:      return QColor(0xe8, 0x80, 0x30);
    case PortKind::AudioIn:            return QColor(0xe8, 0x80, 0x30);
    case PortKind::AudioControllerOut: return QColor(0xe4, 0xb0, 0x42);
    case PortKind::MasterAudioIn:      return QColor(0xe4, 0xb0, 0x42);
    case PortKind::MasterAudioInputOut: return QColor(0x70, 0xc8, 0xa8);
    case PortKind::ScriptOut:          return QColor(0x70, 0xc0, 0xa8);
    case PortKind::DataIn:             return QColor(0x70, 0xc0, 0xa8);
    }
    return QColor(128, 128, 128);
}

bool portsCompatible(PortKind a, PortKind b) {
    if (a == b) return false;
    if ((a == PortKind::ChainOut && b == PortKind::ChainIn) ||
        (a == PortKind::ChainIn && b == PortKind::ChainOut)) return true;
    if ((a == PortKind::AbOut && b == PortKind::AbIn) ||
        (a == PortKind::AbIn && b == PortKind::AbOut)) return true;
    if ((a == PortKind::AudioControllerOut && b == PortKind::ShaderAudioIn) ||
        (a == PortKind::ShaderAudioIn && b == PortKind::AudioControllerOut)) return true;
    if ((a == PortKind::AudioControllerOut && b == PortKind::MasterAudioIn) ||
        (a == PortKind::MasterAudioIn && b == PortKind::AudioControllerOut)) return true;
    if ((a == PortKind::MasterAudioInputOut && b == PortKind::MasterAudioIn) ||
        (a == PortKind::MasterAudioIn && b == PortKind::MasterAudioInputOut)) return true;
    if ((a == PortKind::ScriptOut && b == PortKind::DataIn) ||
        (a == PortKind::DataIn && b == PortKind::ScriptOut)) return true;
    return false;
}

bool isSingleConnection(PortKind kind) {
    if (kind == PortKind::AudioControllerOut) return false;
    if (kind == PortKind::MasterAudioIn) return false;
    if (kind == PortKind::AbIn) return false;   // all A/B-select nodes feed the one Output
    return true;
}

class PortItem : public QGraphicsEllipseItem {
public:
    PortItem(PortKind kind, NodeItemBase *parentNode);

    PortKind kind() const { return m_kind; }
    NodeItemBase *nodeItem() const { return m_nodeItem; }
    QPointF sceneCenter() const { return mapToScene(QPointF(0, 0)); }

    void setConnected(bool c) { m_connected = c; update(); }
    bool isConnected() const  { return m_connected; }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *e) override {
        m_hovered = true;
        update();
        QGraphicsEllipseItem::hoverEnterEvent(e);
    }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *e) override {
        m_hovered = false;
        update();
        QGraphicsEllipseItem::hoverLeaveEvent(e);
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w) override;
    QPainterPath shape() const override;
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;

private:
    PortKind m_kind;
    NodeItemBase *m_nodeItem;
    bool m_connected = false;
    bool m_hovered = false;
};

class ConnectionItem : public QGraphicsPathItem {
public:
    enum EdgeKind { Chain = 0, TransformToContext = 1, AudioToController = 2,
                    ControllerToMaster = 3, LegacyClipToTransform = 4,
                    ClipToShaderAudio = 5, ScriptToData = 6, InputToMaster = 7,
                    AbToOutput = 8 };

    ConnectionItem(PortItem *from, PortItem *to, EdgeKind kind)
        : QGraphicsPathItem(nullptr), m_from(from), m_to(to), m_kind(kind)
    {
        setZValue(-1);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        QColor color;
        if (kind == Chain) color = QColor(0x70, 0xb8, 0xff);
        else if (kind == AbToOutput) color = QColor(0xe0, 0x50, 0x50);
        else if (kind == AudioToController) color = QColor(0xe8, 0x80, 0x30);
        else if (kind == ClipToShaderAudio) color = QColor(0xe8, 0x90, 0x40);
        else if (kind == ScriptToData) color = QColor(0x70, 0xc8, 0xa0);
        else if (kind == InputToMaster) color = QColor(0x70, 0xd0, 0x90);
        else color = QColor(0xf0, 0xc0, 0x50);
        m_basePen = QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        setPen(m_basePen);
        updatePath();
    }

    PortItem *fromPort() const { return m_from; }
    PortItem *toPort()   const { return m_to;   }
    EdgeKind edgeKind()  const { return m_kind; }

    NodeId fromNodeId() const;
    NodeId toNodeId()   const;

    void updatePath();

    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10.0);
        return stroker.createStroke(path());
    }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w) override {
        if (isSelected()) {
            QPen pen = m_basePen;
            pen.setWidthF(4.0);
            pen.setColor(kSelectionAccent);
            p->setPen(pen);
            p->drawPath(path());
        }
        QGraphicsPathItem::paint(p, opt, w);
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override;

private:
    PortItem *m_from;
    PortItem *m_to;
    EdgeKind m_kind;
    QPen m_basePen;
};

class NodeItemBase : public QGraphicsItem {
public:
    virtual ~NodeItemBase() = default;
    virtual NodeId nodeId() const = 0;
    virtual void paint(QPainter *p, const QStyleOptionGraphicsItem *o, QWidget *w) override = 0;
};

void ConnectionItem::updatePath() {
    const QPointF p1 = m_from->sceneCenter();
    const QPointF p2 = m_to->sceneCenter();
    // Ports sit on the vertical left/right edges of their nodes, so leave and arrive
    // horizontally (perpendicular to the edge) in each port's outward direction
    // rather than tilting the wire up/down.
    auto outwardX = [](PortItem *port) -> qreal {
        NodeItemBase *n = port->nodeItem();
        const qreal nodeCenterX = n->mapToScene(n->boundingRect().center()).x();
        return port->sceneCenter().x() < nodeCenterX ? -1.0 : 1.0;
    };
    const qreal dx = std::max(std::abs(p2.x() - p1.x()) * 0.4, 40.0);
    QPainterPath path;
    path.moveTo(p1);
    path.cubicTo(QPointF(p1.x() + outwardX(m_from) * dx, p1.y()),
                 QPointF(p2.x() + outwardX(m_to) * dx, p2.y()),
                 p2);
    setPath(path);
}

class ClipNodeItem : public NodeItemBase {
public:
    ClipNodeItem(ClipNodeModel *model, NodeId id, bool hasAudio = false,
                 bool hasShaderAudioIn = false, bool hasDataIn = false,
                 int volume = 100, bool muted = false,
                 AudioPlaybackMode playbackMode = AudioPlaybackMode::Always, int delayMs = 0)
        : m_model(model), m_nodeId(id), m_hasAudio(hasAudio)
        , m_volume(volume), m_muted(muted), m_playbackMode(playbackMode), m_delayMs(delayMs)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);

        auto *card = new ClipCard(0);
        model->setCard(card);
        QObject::connect(card, &ClipCard::preferredHeightChanged, card, [this](int) { updateLayout(); });

        card->setCardMode(ClipCard::CardMode::InputNode);

        m_proxy = new QGraphicsProxyWidget(this);
        m_proxy->setWidget(card);
        m_proxy->setPos(0, PROXY_Y);

        m_chainOutPort = new PortItem(PortKind::ChainOut, this);

        if (hasAudio) {
            m_audioPort = new PortItem(PortKind::AudioControllerOut, this);
        }
        if (hasShaderAudioIn) {
            m_shaderAudioInPort = new PortItem(PortKind::ShaderAudioIn, this);
        }
        if (hasDataIn) {
            m_dataInPort = new PortItem(PortKind::DataIn, this);
        }
        updateLayout();
    }

    std::function<void(NodeId)> onDeleteRequested;
    std::function<void(NodeId)> onRenameRequested;
    std::function<void(NodeId)> onEditAudioRequested;

    NodeId nodeId() const override { return m_nodeId; }
    ClipNodeModel *model() const { return m_model; }
    PortItem *chainOutPort() const { return m_chainOutPort; }
    PortItem *audioPort() const { return m_audioPort; }
    PortItem *shaderAudioInPort() const { return m_shaderAudioInPort; }
    PortItem *dataInPort() const { return m_dataInPort; }
    bool hasAudio() const { return m_hasAudio; }

    int  volume() const { return m_volume; }
    bool muted()  const { return m_muted; }
    AudioPlaybackMode playbackMode() const { return m_playbackMode; }
    int  delayMs() const { return m_delayMs; }

    void setVolume(int v) { m_volume = v; update(); }
    void setMuted(bool m) { m_muted = m; update(); }
    void setPlaybackMode(AudioPlaybackMode mode) { m_playbackMode = mode; update(); }
    void setDelayMs(int ms) { m_delayMs = ms; update(); }

    qreal bodyHeight() const {
        return m_proxy->widget() ? m_proxy->widget()->height() : CARD_PROXY_H;
    }

    qreal audioStripHeight() const { return m_hasAudio ? AUDIO_STRIP_H : 0.0; }

    qreal nodeHeight() const {
        return PROXY_Y + bodyHeight() + audioStripHeight() + PORT_R + PORT_R;
    }

    QRectF boundingRect() const override { return QRectF(0, 0, NODE_W, nodeHeight()); }

    void updateLayout();

    QRectF getAudioEditButtonRect() const {
        const qreal stripTop = PROXY_Y + bodyHeight();
        return QRectF(4, stripTop + AUDIO_STRIP_H - 22, NODE_W - 8, 18);
    }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF bounds(0, 0, NODE_W, nodeHeight());
        p->setPen(QPen(QColor(55, 56, 62), 1));
        p->setBrush(QColor(30, 31, 34));
        p->drawRoundedRect(bounds, 6, 6);

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

        p->setPen(QColor(120, 170, 210));
        p->setFont(QFont("Monospace", 7));
        p->drawText(QRectF(4, 2, NODE_W - 8, HEADER_H),
                    Qt::AlignCenter, QStringLiteral("INPUT"));

        if (m_hasAudio) {
            const qreal stripTop = PROXY_Y + bodyHeight();
            const QRectF stripRect(2, stripTop, NODE_W - 4, AUDIO_STRIP_H - 2);
            p->setPen(QPen(QColor(62, 55, 45), 1));
            p->setBrush(QColor(34, 28, 22));
            p->drawRoundedRect(stripRect, 3, 3);

            p->setPen(QColor(230, 140, 60));
            p->setFont(QFont("Monospace", 7));
            QString modeStr;
            switch (m_playbackMode) {
            case AudioPlaybackMode::DeckAOnly: modeStr = "Deck A"; break;
            case AudioPlaybackMode::DeckBOnly: modeStr = "Deck B"; break;
            case AudioPlaybackMode::Always:    modeStr = "Always"; break;
            }
            const QString label = QString("Audio  Vol:%1%  %2\n%3  Delay:%4ms")
                .arg(m_volume)
                .arg(m_muted ? "Muted" : "Playing")
                .arg(modeStr)
                .arg(m_delayMs);
            p->drawText(QRectF(6, stripTop + 3, NODE_W - 12, AUDIO_STRIP_H - 26),
                        Qt::AlignCenter, label);

            const QRectF buttonRect = getAudioEditButtonRect();
            p->setPen(QPen(QColor(230, 140, 60), 1));
            p->setBrush(QColor(90, 45, 10));
            p->drawRoundedRect(buttonRect, 3, 3);
            p->setPen(QColor(255, 185, 120));
            p->drawText(buttonRect, Qt::AlignCenter, "Edit Audio");
        }

        if (isSelected()) {
            p->setPen(QPen(kSelectionAccent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 6, 6);
        }
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override {
        QMenu menu;
        if (m_hasAudio) {
            menu.addAction("Edit Audio", [this]() {
                if (onEditAudioRequested) onEditAudioRequested(m_nodeId);
            });
        }
        menu.addAction("Delete", [this]() {
            if (onDeleteRequested) onDeleteRequested(m_nodeId);
        });
        menu.exec(e->screenPos());
        e->accept();
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override {
        if (m_hasAudio && getAudioEditButtonRect().contains(e->pos())) {
            if (onEditAudioRequested) onEditAudioRequested(m_nodeId);
            e->accept();
            return;
        }
        QGraphicsItem::mouseDoubleClickEvent(e);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        if (m_hasAudio && getAudioEditButtonRect().contains(e->pos())) {
            if (onEditAudioRequested) onEditAudioRequested(m_nodeId);
            e->accept();
            return;
        }
        QGraphicsItem::mousePressEvent(e);
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    ClipNodeModel *m_model;
    NodeId m_nodeId;
    bool m_hasAudio;
    int  m_volume;
    bool m_muted;
    AudioPlaybackMode m_playbackMode;
    int  m_delayMs;
    QGraphicsProxyWidget *m_proxy;
    PortItem *m_chainOutPort;
    PortItem *m_audioPort = nullptr;
    PortItem *m_shaderAudioInPort = nullptr;
    PortItem *m_dataInPort = nullptr;
};

// ── Process node (Crop / FlipH / FlipV) ─────────────────────────────────────
class ProcessNodeItem : public NodeItemBase {
public:
    enum class Effect { Crop = 0, FlipH = 1, FlipV = 2 };

    ProcessNodeItem(NodeId id, Effect effect)
        : m_nodeId(id), m_effect(effect)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);
        m_inPort  = new PortItem(PortKind::ChainIn, this);
        m_inPort->setPos(0, PROC_H / 2);
        m_outPort = new PortItem(PortKind::ChainOut, this);
        m_outPort->setPos(PROC_W, PROC_H / 2);
    }

    std::function<void(NodeId)> onEditRequested;
    std::function<void(NodeId)> onDeleteRequested;

    static constexpr qreal PROC_W = 116.0;
    static constexpr qreal PROC_H = 66.0;

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *inPort()  const { return m_inPort; }
    PortItem *outPort() const { return m_outPort; }

    Effect effect() const { return m_effect; }
    void cropRect(float &x, float &y, float &w, float &h) const {
        x = m_cropX; y = m_cropY; w = m_cropW; h = m_cropH;
    }
    void setCropRect(float x, float y, float w, float h) {
        m_cropX = x; m_cropY = y; m_cropW = w; m_cropH = h; update();
    }

    QRectF boundingRect() const override { return QRectF(0, 0, PROC_W, PROC_H); }
    QRectF getEditButtonRect() const { return QRectF(8, PROC_H - 24, PROC_W - 16, 18); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(55, 56, 62), 1));
        p->setBrush(QColor(28, 30, 36));
        p->drawRoundedRect(QRectF(0, 0, PROC_W, PROC_H), 4, 4);

        p->setPen(QColor(120, 170, 210));
        p->setFont(QFont("Monospace", 8, QFont::Bold));
        QString name = m_effect == Effect::Crop ? "Crop"
                     : m_effect == Effect::FlipH ? "Flip H" : "Flip V";
        p->drawText(QRectF(4, 6, PROC_W - 8, 28), Qt::AlignCenter,
                    QStringLiteral("PROCESS\n%1").arg(name));

        if (m_effect == Effect::Crop) {
            const QRectF r = getEditButtonRect();
            p->setPen(QPen(QColor(100, 180, 255), 1));
            p->setBrush(QColor(20, 80, 150));
            p->drawRoundedRect(r, 3, 3);
            p->setPen(QColor(200, 220, 255));
            p->setFont(QFont("Monospace", 7));
            p->drawText(r, Qt::AlignCenter, "Edit Crop");
        }

        if (isSelected()) {
            p->setPen(QPen(kSelectionAccent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(QRectF(0, 0, PROC_W, PROC_H).adjusted(1, 1, -1, -1), 4, 4);
        }
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        if (m_effect == Effect::Crop && getEditButtonRect().contains(e->pos())) {
            if (onEditRequested) onEditRequested(m_nodeId);
            e->accept();
            return;
        }
        QGraphicsItem::mousePressEvent(e);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override {
        if (m_effect == Effect::Crop && onEditRequested) onEditRequested(m_nodeId);
        e->accept();
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override {
        QMenu menu;
        if (m_effect == Effect::Crop)
            menu.addAction("Edit Crop", [this]() { if (onEditRequested) onEditRequested(m_nodeId); });
        menu.addAction("Delete", [this]() { if (onDeleteRequested) onDeleteRequested(m_nodeId); });
        menu.exec(e->screenPos());
        e->accept();
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    NodeId m_nodeId;
    Effect m_effect;
    float m_cropX = 0.f, m_cropY = 0.f, m_cropW = 1.f, m_cropH = 1.f;
    PortItem *m_inPort = nullptr;
    PortItem *m_outPort = nullptr;
};

// ── Layer node (multi-input compositor with per-slot placement) ─────────────
struct LayerSlot {
    float baseX = 0.f, baseY = 0.f, baseW = 1.f, baseH = 1.f;
    bool  visible = true;
    QString name;
};

class LayerNodeItem : public NodeItemBase {
public:
    static constexpr qreal SW_W    = 158.0;
    static constexpr qreal SW_HDR  = 20.0;
    static constexpr qreal SW_ROW  = 22.0;
    static constexpr qreal SW_FOOT = 26.0;

    explicit LayerNodeItem(NodeId id, int initialSlots = 1)
        : m_nodeId(id)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);
        setToolTip("Double-click a layer name to rename · eye toggles visibility · ▲▼ reorders");
        for (int i = 0; i < initialSlots; ++i)
            m_slots.push_back(LayerSlot{});
        rebuildPorts();
    }

    std::function<void(NodeId)> onEditRequested;      // transform editor
    std::function<void(NodeId)> onEditCanvasRequested;
    std::function<void(NodeId)> onDeleteRequested;
    std::function<void()>       onChanged;

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *outPort() const { return m_outPort; }
    int slotCount() const { return m_slots.size(); }
    PortItem *inPort(int i) const { return (i >= 0 && i < m_inPorts.size()) ? m_inPorts[i] : nullptr; }
    const QVector<PortItem *> &inPorts() const { return m_inPorts; }
    const LayerSlot &slot(int i) const { return m_slots[i]; }
    QVector<LayerSlot> &slotsRef() { return m_slots; }
    void setSlots(const QVector<LayerSlot> &s) { m_slots = s; rebuildPorts(); }

    int canvasW() const { return m_canvasW; }
    int canvasH() const { return m_canvasH; }
    void setCanvasSize(int w, int h) { m_canvasW = w; m_canvasH = h; update(); }

    qreal nodeHeight() const { return SW_HDR + m_slots.size() * SW_ROW + SW_FOOT; }
    QRectF boundingRect() const override { return QRectF(0, 0, SW_W, nodeHeight()); }

    QRectF rowRect(int i) const { return QRectF(0, SW_HDR + i * SW_ROW, SW_W, SW_ROW); }
    QRectF eyeRect(int i) const { return QRectF(6, SW_HDR + i * SW_ROW + 3, 16, 16); }
    QRectF nameRect(int i) const { return QRectF(26, SW_HDR + i * SW_ROW, SW_W - 70, SW_ROW); }
    QRectF upRect(int i)  const { return QRectF(SW_W - 40, SW_HDR + i * SW_ROW + 3, 16, 16); }
    QRectF downRect(int i) const { return QRectF(SW_W - 22, SW_HDR + i * SW_ROW + 3, 16, 16); }
    QRectF editButtonRect() const { return QRectF(6, nodeHeight() - SW_FOOT + 3, SW_W - 12, 20); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF bounds(0, 0, SW_W, nodeHeight());
        p->setPen(QPen(QColor(55, 56, 62), 1));
        p->setBrush(QColor(28, 32, 40));
        p->drawRoundedRect(bounds, 5, 5);

        p->setPen(QColor(120, 170, 210));
        p->setFont(QFont("Monospace", 8, QFont::Bold));
        p->drawText(QRectF(4, 3, SW_W - 8, SW_HDR - 4), Qt::AlignCenter,
                    QStringLiteral("LAYER  %1×%2").arg(m_canvasW).arg(m_canvasH));

        for (int i = 0; i < m_slots.size(); ++i) {
            const LayerSlot &s = m_slots[i];
            const bool connected = i < m_inPorts.size() && m_inPorts[i]->isConnected();
            p->setPen(QColor(60, 62, 70));
            p->drawLine(QPointF(4, SW_HDR + i * SW_ROW), QPointF(SW_W - 4, SW_HDR + i * SW_ROW));

            if (!connected) {
                // Trailing empty input slot — placeholder, no controls.
                p->setPen(QColor(110, 115, 125));
                p->setFont(QFont("Monospace", 7));
                p->drawText(QRectF(26, SW_HDR + i * SW_ROW, SW_W - 32, SW_ROW),
                            Qt::AlignVCenter | Qt::AlignLeft, "+ connect input");
                continue;
            }

            // eye toggle (show/hide layer)
            MaterialSymbols::drawCentered(*p, eyeRect(i),
                                          s.visible ? "visibility" : "visibility_off", 14,
                                          s.visible ? QColor(120, 200, 160) : QColor(90, 90, 96));

            // editable name (double-click to rename)
            const QRectF nr = nameRect(i);
            p->setPen(QColor(190, 195, 205));
            p->setFont(QFont("Monospace", 7));
            const QString nm = s.name.isEmpty() ? QStringLiteral("in %1").arg(i + 1) : s.name;
            p->drawText(nr, Qt::AlignVCenter | Qt::AlignLeft,
                        p->fontMetrics().elidedText(nm, Qt::ElideRight, nr.width()));
            p->setPen(QColor(70, 74, 82));
            p->drawLine(QPointF(nr.left(), nr.bottom() - 2), QPointF(nr.right(), nr.bottom() - 2));

            // up / down
            p->setPen(QColor(150, 160, 175));
            p->setFont(QFont("Monospace", 9));
            if (i > 0)                     p->drawText(upRect(i),   Qt::AlignCenter, "▲");
            if (i < connectedCount() - 1)  p->drawText(downRect(i), Qt::AlignCenter, "▼");
        }

        const QRectF er = editButtonRect();
        p->setPen(QPen(QColor(100, 180, 255), 1));
        p->setBrush(QColor(20, 80, 150));
        p->drawRoundedRect(er, 3, 3);
        p->setPen(QColor(200, 220, 255));
        p->setFont(QFont("Monospace", 7));
        p->drawText(er, Qt::AlignCenter, "Edit Layout");

        if (isSelected()) {
            p->setPen(QPen(kSelectionAccent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 5, 5);
        }
    }

    // Number of leading connected input slots (the trailing slot is always empty).
    int connectedCount() const {
        int n = 0;
        for (int i = 0; i < m_inPorts.size(); ++i)
            if (m_inPorts[i]->isConnected()) ++n;
        return n;
    }

    void swapInputs(int a, int b) {
        std::swap(m_slots[a], m_slots[b]);
        std::swap(m_inPorts[a], m_inPorts[b]);
        positionPorts();
        update();
        if (onChanged) onChanged();
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        for (int i = 0; i < m_slots.size(); ++i) {
            const bool connected = i < m_inPorts.size() && m_inPorts[i]->isConnected();
            if (!connected) continue;
            if (eyeRect(i).contains(e->pos())) {
                m_slots[i].visible = !m_slots[i].visible;
                update();
                if (onChanged) onChanged();
                e->accept();
                return;
            }
            if (upRect(i).contains(e->pos()) && i > 0 && m_inPorts[i - 1]->isConnected()) {
                swapInputs(i, i - 1);
                e->accept();
                return;
            }
            if (downRect(i).contains(e->pos()) && i < connectedCount() - 1) {
                swapInputs(i, i + 1);
                e->accept();
                return;
            }
        }
        if (editButtonRect().contains(e->pos())) {
            if (onEditRequested) onEditRequested(m_nodeId);
            e->accept();
            return;
        }
        QGraphicsItem::mousePressEvent(e);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override {
        for (int i = 0; i < m_slots.size(); ++i) {
            const bool connected = i < m_inPorts.size() && m_inPorts[i]->isConnected();
            if (connected && nameRect(i).contains(e->pos())) {
                bool ok = false;
                const QString name = QInputDialog::getText(nullptr, "Rename Layer", "Name:",
                    QLineEdit::Normal, m_slots[i].name, &ok);
                if (ok) { m_slots[i].name = name; update(); if (onChanged) onChanged(); }
                e->accept();
                return;
            }
        }
        QGraphicsItem::mouseDoubleClickEvent(e);
    }

    // Keep exactly one trailing empty input port; preserve connected ports/data + order.
    void normalizeInputs();

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override {
        QMenu menu;
        menu.addAction("Edit Canvas Size", [this]() { if (onEditCanvasRequested) onEditCanvasRequested(m_nodeId); });
        menu.addAction("Edit Layout", [this]() { if (onEditRequested) onEditRequested(m_nodeId); });
        menu.addAction("Delete", [this]() { if (onDeleteRequested) onDeleteRequested(m_nodeId); });
        menu.exec(e->screenPos());
        e->accept();
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    void rebuildPorts();
    void positionPorts();

    NodeId m_nodeId;
    int m_canvasW = 1280, m_canvasH = 720;
    QVector<LayerSlot> m_slots;
    QVector<PortItem *> m_inPorts;
    PortItem *m_outPort = nullptr;
};

// ── A/B deck-select node ────────────────────────────────────────────────────
struct AbSlot { QString name; };

class AbSelectNodeItem : public NodeItemBase {
public:
    static constexpr qreal SW_W    = 168.0;
    static constexpr qreal SW_HDR  = 20.0;
    static constexpr qreal SW_ROW  = 24.0;
    static constexpr qreal SW_FOOT = 6.0;

    explicit AbSelectNodeItem(NodeId id, int initialSlots = 1)
        : m_nodeId(id)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);
        setToolTip("Double-click an input name to rename · A/B assigns the deck");
        for (int i = 0; i < initialSlots; ++i)
            m_slots.push_back(AbSlot{});
        rebuildPorts();
    }

    std::function<void(NodeId, int, bool)> onAssignDeck;  // (node, slot, deckA)
    std::function<void(NodeId)> onDeleteRequested;
    std::function<void()>       onChanged;

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *abOutPort() const { return m_abOutPort; }
    int slotCount() const { return m_slots.size(); }
    PortItem *inPort(int i) const { return (i >= 0 && i < m_inPorts.size()) ? m_inPorts[i] : nullptr; }
    const QVector<PortItem *> &inPorts() const { return m_inPorts; }
    const AbSlot &slot(int i) const { return m_slots[i]; }
    QVector<AbSlot> &slotsRef() { return m_slots; }
    void setSlots(const QVector<AbSlot> &s) { m_slots = s; rebuildPorts(); }

    // Which slot indices currently drive deck A / B (for highlight); set by editor.
    void setDeckSlots(int aSlot, int bSlot) { m_aSlot = aSlot; m_bSlot = bSlot; update(); }

    // Whether this node's red output is wired to the Output node; gates the A/B buttons.
    void setOutputConnected(bool c) { if (m_outputConnected != c) { m_outputConnected = c; update(); } }
    bool outputConnected() const { return m_outputConnected; }

    qreal nodeHeight() const { return SW_HDR + m_slots.size() * SW_ROW + SW_FOOT; }
    QRectF boundingRect() const override { return QRectF(0, 0, SW_W, nodeHeight()); }

    QRectF aBtnRect(int i) const { return QRectF(SW_W - 52, SW_HDR + i * SW_ROW + 3, 22, 18); }
    QRectF bBtnRect(int i) const { return QRectF(SW_W - 28, SW_HDR + i * SW_ROW + 3, 22, 18); }
    QRectF nameRect(int i) const { return QRectF(6, SW_HDR + i * SW_ROW, SW_W - 62, SW_ROW); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF bounds(0, 0, SW_W, nodeHeight());
        p->setPen(QPen(QColor(72, 46, 46), 1));
        p->setBrush(QColor(40, 28, 30));
        p->drawRoundedRect(bounds, 5, 5);

        p->setPen(QColor(230, 120, 120));
        p->setFont(QFont("Monospace", 8, QFont::Bold));
        p->drawText(QRectF(4, 3, SW_W - 8, SW_HDR - 4), Qt::AlignCenter, "A/B SELECT");

        for (int i = 0; i < m_slots.size(); ++i) {
            const bool connected = i < m_inPorts.size() && m_inPorts[i]->isConnected();
            p->setPen(QColor(70, 50, 52));
            p->drawLine(QPointF(4, SW_HDR + i * SW_ROW), QPointF(SW_W - 4, SW_HDR + i * SW_ROW));

            if (!connected) {
                p->setPen(QColor(120, 100, 102));
                p->setFont(QFont("Monospace", 7));
                p->drawText(QRectF(8, SW_HDR + i * SW_ROW, SW_W - 12, SW_ROW),
                            Qt::AlignVCenter | Qt::AlignLeft, "+ connect input");
                continue;
            }

            const QRectF nr = nameRect(i);
            p->setPen(QColor(210, 195, 195));
            p->setFont(QFont("Monospace", 7));
            const QString nm = m_slots[i].name.isEmpty() ? QStringLiteral("in %1").arg(i + 1) : m_slots[i].name;
            p->drawText(nr, Qt::AlignVCenter | Qt::AlignLeft,
                        p->fontMetrics().elidedText(nm, Qt::ElideRight, nr.width()));
            p->setPen(QColor(86, 62, 64));
            p->drawLine(QPointF(nr.left(), nr.bottom() - 2), QPointF(nr.right(), nr.bottom() - 2));

            auto drawBtn = [&](const QRectF &r, const QString &t, bool active) {
                if (!m_outputConnected) {
                    p->setPen(QPen(QColor(70, 58, 60), 1));
                    p->setBrush(QColor(36, 30, 32));
                    p->drawRoundedRect(r, 3, 3);
                    p->setPen(QColor(96, 84, 86));
                    p->setFont(QFont("Monospace", 8, QFont::Bold));
                    p->drawText(r, Qt::AlignCenter, t);
                    return;
                }
                p->setPen(QPen(active ? QColor(0xe0, 0x50, 0x50) : QColor(90, 70, 72), 1));
                p->setBrush(active ? QColor(0x80, 0x2a, 0x2a) : QColor(46, 34, 36));
                p->drawRoundedRect(r, 3, 3);
                p->setPen(active ? Qt::white : QColor(180, 160, 162));
                p->setFont(QFont("Monospace", 8, QFont::Bold));
                p->drawText(r, Qt::AlignCenter, t);
            };
            drawBtn(aBtnRect(i), "A", i == m_aSlot);
            drawBtn(bBtnRect(i), "B", i == m_bSlot);
        }

        if (isSelected()) {
            p->setPen(QPen(kSelectionAccent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 5, 5);
        }
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        if (m_outputConnected) {
            for (int i = 0; i < m_slots.size(); ++i) {
                if (i >= m_inPorts.size() || !m_inPorts[i]->isConnected()) continue;
                if (aBtnRect(i).contains(e->pos())) {
                    if (onAssignDeck) onAssignDeck(m_nodeId, i, true);
                    e->accept();
                    return;
                }
                if (bBtnRect(i).contains(e->pos())) {
                    if (onAssignDeck) onAssignDeck(m_nodeId, i, false);
                    e->accept();
                    return;
                }
            }
        }
        QGraphicsItem::mousePressEvent(e);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override {
        for (int i = 0; i < m_slots.size(); ++i) {
            if (i >= m_inPorts.size() || !m_inPorts[i]->isConnected()) continue;
            if (nameRect(i).contains(e->pos())) {
                bool ok = false;
                const QString name = QInputDialog::getText(nullptr, "Rename Input", "Name:",
                    QLineEdit::Normal, m_slots[i].name, &ok);
                if (ok) { m_slots[i].name = name; update(); if (onChanged) onChanged(); }
                e->accept();
                return;
            }
        }
        QGraphicsItem::mouseDoubleClickEvent(e);
    }

    // Keep exactly one trailing empty input port; preserve connected ports/data + order.
    void normalizeInputs();

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override {
        QMenu menu;
        menu.addAction("Delete", [this]() { if (onDeleteRequested) onDeleteRequested(m_nodeId); });
        menu.exec(e->screenPos());
        e->accept();
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    void rebuildPorts();
    void positionPorts();

    NodeId m_nodeId;
    QVector<AbSlot> m_slots;
    QVector<PortItem *> m_inPorts;
    PortItem *m_abOutPort = nullptr;
    int m_aSlot = -1, m_bSlot = -1;
    bool m_outputConnected = false;
};

// ── Output node (single sink) ───────────────────────────────────────────────
class OutputNodeItem : public NodeItemBase {
public:
    static constexpr qreal OUT_W = 116.0;
    static constexpr qreal OUT_H = 60.0;

    explicit OutputNodeItem(NodeId id)
        : m_nodeId(id)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);
        m_chainInPort = new PortItem(PortKind::ChainIn, this);
        m_chainInPort->setPos(0, OUT_H * 0.33);
        m_abInPort = new PortItem(PortKind::AbIn, this);
        m_abInPort->setPos(0, OUT_H * 0.67);
    }

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *chainInPort() const { return m_chainInPort; }
    PortItem *abInPort()    const { return m_abInPort; }

    QRectF boundingRect() const override { return QRectF(0, 0, OUT_W, OUT_H); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(70, 90, 110), 1));
        p->setBrush(QColor(26, 34, 42));
        p->drawRoundedRect(QRectF(0, 0, OUT_W, OUT_H), 5, 5);

        p->setPen(QColor(150, 200, 240));
        p->setFont(QFont("Monospace", 10, QFont::Bold));
        p->drawText(QRectF(0, 0, OUT_W, OUT_H), Qt::AlignCenter, "OUTPUT");

        p->setPen(QColor(90, 150, 200));
        p->setFont(QFont("Monospace", 6));
        p->drawText(QRectF(14, OUT_H * 0.33 - 6, 60, 12), Qt::AlignVCenter | Qt::AlignLeft, "video");
        p->setPen(QColor(220, 110, 110));
        p->drawText(QRectF(14, OUT_H * 0.67 - 6, 60, 12), Qt::AlignVCenter | Qt::AlignLeft, "A/B");

        if (isSelected()) {
            p->setPen(QPen(kSelectionAccent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(QRectF(0, 0, OUT_W, OUT_H).adjusted(1, 1, -1, -1), 5, 5);
        }
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    NodeId m_nodeId;
    PortItem *m_chainInPort = nullptr;
    PortItem *m_abInPort = nullptr;
};

class MasterAudioOutputNodeItem : public NodeItemBase {
public:
    explicit MasterAudioOutputNodeItem(NodeId id)
        : m_nodeId(id)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);

        m_audioInPort = new PortItem(PortKind::MasterAudioIn, this);
        m_audioInPort->setPos(SMALL_NODE_W, SMALL_NODE_H / 2);
    }

    std::function<void(NodeId)> onDeleteRequested;
    std::function<void(NodeId)> onDeviceChanged;

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *audioInPort() const { return m_audioInPort; }

    // Empty deviceId = system default output.
    QString deviceId() const { return m_deviceId; }
    QString deviceLabel() const { return m_deviceLabel; }
    void setDevice(const QString &id, const QString &label) {
        m_deviceId = id;
        m_deviceLabel = label;
        update();
    }

    QRectF boundingRect() const override { return QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H); }

    QRectF getDeviceButtonRect() const { return QRectF(4, SMALL_NODE_H - 32, SMALL_NODE_W - 8, 24); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(86, 65, 18), 1));
        p->setBrush(QColor(45, 34, 12));
        p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H), 4, 4);

        p->setPen(QColor(245, 206, 88));
        p->setFont(QFont("Monospace", 8, QFont::Bold));
        p->drawText(QRectF(6, 8, SMALL_NODE_W - 12, 40),
                    Qt::AlignCenter, "Master Audio\nOutput");

        // Output-device selector (click to choose).
        const QRectF devRect = getDeviceButtonRect();
        p->setPen(QPen(QColor(245, 206, 88), 1));
        p->setBrush(QColor(70, 52, 16));
        p->drawRoundedRect(devRect, 3, 3);
        p->setPen(QColor(255, 224, 140));
        p->setFont(QFont("Monospace", 7));
        const QString devText = m_deviceId.isEmpty() ? QStringLiteral("Default")
                                                      : m_deviceLabel;
        const QString elided = p->fontMetrics().elidedText(
            devText + QStringLiteral(" ▾"), Qt::ElideRight, devRect.width() - 6);
        p->drawText(devRect, Qt::AlignCenter, elided);

        if (isSelected()) {
            p->setPen(QPen(kSelectionAccent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H).adjusted(1, 1, -1, -1), 4, 4);
        }
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        if (getDeviceButtonRect().contains(e->pos())) {
            showDeviceMenu(e->screenPos());
            e->accept();
            return;
        }
        QGraphicsItem::mousePressEvent(e);
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override {
        QMenu menu;
        menu.addAction("Delete", [this]() {
            if (onDeleteRequested) onDeleteRequested(m_nodeId);
        });
        menu.exec(e->screenPos());
        e->accept();
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    void showDeviceMenu(const QPoint &screenPos) {
        QMenu menu;
        QAction *defAct = menu.addAction(QStringLiteral("System Default"));
        defAct->setCheckable(true);
        defAct->setChecked(m_deviceId.isEmpty());
        menu.addSeparator();
        const auto outputs = QMediaDevices::audioOutputs();
        for (const QAudioDevice &dev : outputs) {
            QAction *a = menu.addAction(dev.description());
            a->setCheckable(true);
            const QString id = QString::fromUtf8(dev.id());
            a->setChecked(id == m_deviceId);
            a->setData(id);
        }
        QAction *chosen = menu.exec(screenPos);
        if (!chosen) return;
        if (chosen == defAct) {
            m_deviceId.clear();
            m_deviceLabel.clear();
        } else {
            m_deviceId = chosen->data().toString();
            m_deviceLabel = chosen->text();
        }
        update();
        if (onDeviceChanged) onDeviceChanged(m_nodeId);
    }

    QString m_deviceId;       // empty = system default
    QString m_deviceLabel;

    NodeId m_nodeId;
    PortItem *m_audioInPort = nullptr;
};

class MasterAudioInputNodeItem : public NodeItemBase {
public:
    explicit MasterAudioInputNodeItem(NodeId id, int volume = 100, bool muted = false)
        : m_nodeId(id), m_volume(volume), m_muted(muted)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);

        m_audioOutPort = new PortItem(PortKind::MasterAudioInputOut, this);
        m_audioOutPort->setPos(SMALL_NODE_W, SMALL_NODE_H / 2);
    }

    std::function<void(NodeId)> onDeleteRequested;
    std::function<void(NodeId)> onSettingsChanged;

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *audioOutPort() const { return m_audioOutPort; }

    QString deviceId() const { return m_deviceId; }
    QString deviceLabel() const { return m_deviceLabel; }
    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }

    void setDevice(const QString &id, const QString &label) {
        m_deviceId = id;
        m_deviceLabel = label;
        update();
    }
    void setVolume(int volume) { m_volume = volume; update(); }
    void setMuted(bool muted) { m_muted = muted; update(); }

    QRectF boundingRect() const override { return QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H); }

    QRectF getDeviceButtonRect() const { return QRectF(4, SMALL_NODE_H - 32, SMALL_NODE_W - 8, 24); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(24, 72, 58), 1));
        p->setBrush(QColor(18, 40, 34));
        p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H), 4, 4);

        p->setPen(QColor(120, 220, 170));
        p->setFont(QFont("Monospace", 8, QFont::Bold));
        p->drawText(QRectF(6, 8, SMALL_NODE_W - 12, 40),
                    Qt::AlignCenter, "Master Audio\nInput");

        p->setPen(QColor(180, 230, 200));
        p->setFont(QFont("Monospace", 7));
        p->drawText(QRectF(6, 42, SMALL_NODE_W - 12, 24), Qt::AlignCenter,
                    m_muted ? QStringLiteral("Muted") : QStringLiteral("Vol: %1%").arg(m_volume));

        const QRectF devRect = getDeviceButtonRect();
        p->setPen(QPen(QColor(120, 220, 170), 1));
        p->setBrush(QColor(28, 64, 52));
        p->drawRoundedRect(devRect, 3, 3);
        p->setPen(QColor(210, 245, 225));
        const QString devText = m_deviceId.isEmpty() ? QStringLiteral("Default Mic")
                                                      : m_deviceLabel;
        const QString elided = p->fontMetrics().elidedText(
            devText + QStringLiteral(" ▾"), Qt::ElideRight, devRect.width() - 6);
        p->drawText(devRect, Qt::AlignCenter, elided);

        if (isSelected()) {
            p->setPen(QPen(kSelectionAccent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H).adjusted(1, 1, -1, -1), 4, 4);
        }
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        if (getDeviceButtonRect().contains(e->pos())) {
            showDeviceMenu(e->screenPos());
            e->accept();
            return;
        }
        QGraphicsItem::mousePressEvent(e);
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override {
        QMenu menu;
        menu.addAction("Set Volume…", [this]() {
            bool ok = false;
            const int volume = QInputDialog::getInt(
                nullptr, QStringLiteral("Mic Volume"), QStringLiteral("Volume %:"),
                m_volume, 0, 100, 1, &ok);
            if (!ok) return;
            setVolume(volume);
            if (onSettingsChanged) onSettingsChanged(m_nodeId);
        });
        menu.addAction(m_muted ? "Unmute" : "Mute", [this]() {
            setMuted(!m_muted);
            if (onSettingsChanged) onSettingsChanged(m_nodeId);
        });
        menu.addAction("Delete", [this]() {
            if (onDeleteRequested) onDeleteRequested(m_nodeId);
        });
        menu.exec(e->screenPos());
        e->accept();
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    void showDeviceMenu(const QPoint &screenPos) {
        QMenu menu;
        QAction *defAct = menu.addAction(QStringLiteral("System Default Mic"));
        defAct->setCheckable(true);
        defAct->setChecked(m_deviceId.isEmpty());
        menu.addSeparator();
        const auto inputs = QMediaDevices::audioInputs();
        for (const QAudioDevice &dev : inputs) {
            QAction *a = menu.addAction(dev.description());
            a->setCheckable(true);
            const QString id = QString::fromUtf8(dev.id());
            a->setChecked(id == m_deviceId);
            a->setData(id);
        }
        QAction *chosen = menu.exec(screenPos);
        if (!chosen) return;
        if (chosen == defAct) {
            m_deviceId.clear();
            m_deviceLabel.clear();
        } else {
            m_deviceId = chosen->data().toString();
            m_deviceLabel = chosen->text();
        }
        update();
        if (onSettingsChanged) onSettingsChanged(m_nodeId);
    }

    NodeId m_nodeId;
    PortItem *m_audioOutPort = nullptr;
    QString m_deviceId;
    QString m_deviceLabel;
    int m_volume;
    bool m_muted;
};

class ScriptNodeItem : public NodeItemBase {
public:
    ScriptNodeItem(NodeId id, const QString &code = QString(),
                   ScriptTriggerMode trigger = ScriptTriggerMode::Periodic,
                   int intervalMs = 1000)
        : m_nodeId(id), m_code(code), m_triggerMode(trigger), m_intervalMs(intervalMs)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);

        m_scriptOutPort = new PortItem(PortKind::ScriptOut, this);
        m_scriptOutPort->setPos(0, SMALL_NODE_H / 2);

        m_output = std::make_shared<ScriptOutput>();
        m_thread = new QThread();
        m_runtime = new ScriptRuntime(m_output);
        m_runtime->moveToThread(m_thread);
        m_thread->start();

        if (!m_code.isEmpty()) {
            QMetaObject::invokeMethod(m_runtime, "applySettings", Qt::QueuedConnection,
                                      Q_ARG(QString, m_code),
                                      Q_ARG(int, static_cast<int>(m_triggerMode)),
                                      Q_ARG(int, m_intervalMs));
        }
    }

    ~ScriptNodeItem() override {
        if (m_runtime && m_thread) {
            if (m_thread->isRunning()) {
                QMetaObject::invokeMethod(m_runtime, "shutdown", Qt::BlockingQueuedConnection);
                QMetaObject::invokeMethod(m_runtime, "deleteLater", Qt::BlockingQueuedConnection);
                m_thread->quit();
                m_thread->wait();
            } else {
                delete m_runtime;
            }
            m_runtime = nullptr;
        }
        delete m_thread;
        m_thread = nullptr;
    }

    std::function<void(NodeId)> onEditRequested;
    std::function<void(NodeId)> onDeleteRequested;

    NodeId nodeId() const override { return m_nodeId; }
    PortItem *scriptOutPort() const { return m_scriptOutPort; }
    std::shared_ptr<ScriptOutput> output() const { return m_output; }

    QString code() const { return m_code; }
    ScriptTriggerMode triggerMode() const { return m_triggerMode; }
    int intervalMs() const { return m_intervalMs; }

    void applySettings(const QString &code, ScriptTriggerMode trigger, int intervalMs) {
        m_code = code;
        m_triggerMode = trigger;
        m_intervalMs = intervalMs;
        update();
        if (m_runtime) {
            QMetaObject::invokeMethod(m_runtime, "applySettings", Qt::QueuedConnection,
                                      Q_ARG(QString, m_code),
                                      Q_ARG(int, static_cast<int>(m_triggerMode)),
                                      Q_ARG(int, m_intervalMs));
        }
    }

    QRectF boundingRect() const override { return QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(45, 62, 55), 1));
        p->setBrush(QColor(22, 34, 30));
        p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H), 4, 4);

        p->setPen(QColor(120, 220, 170));
        p->setFont(QFont("Monospace", 8));
        QString triggerStr;
        switch (m_triggerMode) {
        case ScriptTriggerMode::Periodic: triggerStr = QString("Every %1ms").arg(m_intervalMs); break;
        case ScriptTriggerMode::OnStart:  triggerStr = QStringLiteral("On start"); break;
        case ScriptTriggerMode::Manual:   triggerStr = QStringLiteral("Manual"); break;
        }
        p->drawText(QRectF(4, 4, SMALL_NODE_W - 8, 50), Qt::AlignCenter,
                    QString("Lua Script\n%1").arg(triggerStr));

        QRectF buttonRect(4, 78, SMALL_NODE_W - 8, 24);
        p->setPen(QPen(QColor(120, 220, 170), 1));
        p->setBrush(QColor(20, 70, 50));
        p->drawRoundedRect(buttonRect, 3, 3);
        p->setPen(QColor(180, 255, 210));
        p->setFont(QFont("Monospace", 7));
        p->drawText(buttonRect, Qt::AlignCenter, "Edit");

        if (isSelected()) {
            p->setPen(QPen(kSelectionAccent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H).adjusted(1, 1, -1, -1), 4, 4);
        }
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

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override {
        QMenu menu;
        menu.addAction("Run now", [this]() {
            if (m_runtime)
                QMetaObject::invokeMethod(m_runtime, "runNow", Qt::QueuedConnection);
        });
        menu.addAction("Delete", [this]() {
            if (onDeleteRequested) onDeleteRequested(m_nodeId);
        });
        menu.exec(e->screenPos());
        e->accept();
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    NodeId m_nodeId;
    QString m_code;
    ScriptTriggerMode m_triggerMode;
    int m_intervalMs;
    PortItem *m_scriptOutPort;
    std::shared_ptr<ScriptOutput> m_output;
    QThread *m_thread = nullptr;
    ScriptRuntime *m_runtime = nullptr;
};


// Ordered input ports for a node whose destination slot must survive save/restore.
static QVector<PortItem *> orderedInputPorts(NodeItemBase *node) {
    QVector<PortItem *> ports;
    if (auto *l = dynamic_cast<LayerNodeItem *>(node)) {
        ports = l->inPorts();
    } else if (auto *a = dynamic_cast<AbSelectNodeItem *>(node)) {
        ports = a->inPorts();
    } else if (auto *o = dynamic_cast<OutputNodeItem *>(node)) {
        ports << o->chainInPort() << o->abInPort();
    } else if (auto *p = dynamic_cast<ProcessNodeItem *>(node)) {
        ports << p->inPort();
    }
    return ports;
}

static int destPortIndex(NodeItemBase *node, PortItem *port) {
    const QVector<PortItem *> ports = orderedInputPorts(node);
    for (int i = 0; i < ports.size(); ++i)
        if (ports[i] == port) return i;
    return -1;
}

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
        } else if (src->kind() == PortKind::AbOut || src->kind() == PortKind::AbIn) {
            outPort = (src->kind() == PortKind::AbOut) ? src : dst;
            inPort  = (src->kind() == PortKind::AbIn)  ? src : dst;
            kind = ConnectionItem::AbToOutput;
        } else if ((src->kind() == PortKind::AudioControllerOut && dst->kind() == PortKind::ShaderAudioIn) ||
                   (src->kind() == PortKind::ShaderAudioIn && dst->kind() == PortKind::AudioControllerOut)) {
            outPort = (src->kind() == PortKind::AudioControllerOut) ? src : dst;
            inPort  = (src->kind() == PortKind::ShaderAudioIn) ? src : dst;
            kind = ConnectionItem::ClipToShaderAudio;
        } else if ((src->kind() == PortKind::MasterAudioInputOut && dst->kind() == PortKind::MasterAudioIn) ||
                   (src->kind() == PortKind::MasterAudioIn && dst->kind() == PortKind::MasterAudioInputOut)) {
            outPort = (src->kind() == PortKind::MasterAudioInputOut) ? src : dst;
            inPort  = (src->kind() == PortKind::MasterAudioIn) ? src : dst;
            kind = ConnectionItem::InputToMaster;
        } else if (src->kind() == PortKind::AudioControllerOut || src->kind() == PortKind::MasterAudioIn) {
            outPort = (src->kind() == PortKind::AudioControllerOut) ? src : dst;
            inPort  = (src->kind() == PortKind::MasterAudioIn)      ? src : dst;
            kind = ConnectionItem::ControllerToMaster;
        } else {
            outPort = (src->kind() == PortKind::ScriptOut) ? src : dst;
            inPort  = (src->kind() == PortKind::DataIn)    ? src : dst;
            kind = ConnectionItem::ScriptToData;
        }

        // Rule 1: a Process video-IN accepts only Input or Process producers.
        if (kind == ConnectionItem::Chain
            && dynamic_cast<ProcessNodeItem *>(inPort->nodeItem())) {
            NodeItemBase *up = outPort->nodeItem();
            if (!dynamic_cast<ClipNodeItem *>(up) && !dynamic_cast<ProcessNodeItem *>(up))
                return;
        }
        // Rule 2: Output's input is blue XOR red (many red A/B nodes allowed, but not mixed with blue).
        if (auto *out = dynamic_cast<OutputNodeItem *>(inPort->nodeItem())) {
            PortItem *other = (inPort == out->chainInPort()) ? out->abInPort() : out->chainInPort();
            if (other && portHasEdge(other))
                return;
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

    void removeConnection(ConnectionItem *item) {
        for (int i = m_edges.size() - 1; i >= 0; --i) {
            auto &e = m_edges[i];
            if (e.item == item) {
                e.item->fromPort()->setConnected(false);
                e.item->toPort()->setConnected(false);
                removeItem(e.item);
                delete e.item;
                m_edges.removeAt(i);
                if (onConnectionChanged) onConnectionChanged();
                return;
            }
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

    // Producer node feeding a specific blue input PORT (via a Chain edge).
    NodeId producerForInputPort(PortItem *inPort) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::Chain && e.toPort == inPort)
                return e.fromNodeId;
        return 0;
    }

    // True if any edge terminates on this exact port (robust for multi-connect ports).
    bool portHasEdge(PortItem *port) const {
        for (const auto &e : m_edges)
            if (e.fromPort == port || e.toPort == port)
                return true;
        return false;
    }

    // Producer feeding an Output's red A/B input (via an AbToOutput edge).
    NodeId abProducerForPort(PortItem *inPort) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::AbToOutput && e.toPort == inPort)
                return e.fromNodeId;
        return 0;
    }

    void createConnectionManually(PortItem *outPort, PortItem *inPort, ConnectionItem::EdgeKind kind) {
        createConnection(outPort, inPort, kind);
    }

    NodeId masterNodeForClip(NodeId clipId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::ControllerToMaster && e.fromNodeId == clipId)
                return e.toNodeId;
        return 0;
    }

    NodeId masterOutputForInput(NodeId inputNodeId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::InputToMaster && e.fromNodeId == inputNodeId)
                return e.toNodeId;
        return 0;
    }

    NodeId clipForShaderAudio(NodeId shaderNodeId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::ClipToShaderAudio && e.toNodeId == shaderNodeId)
                return e.fromNodeId;
        return 0;
    }

    NodeId scriptNodeForData(NodeId dataNodeId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::ScriptToData && e.toNodeId == dataNodeId)
                return e.fromNodeId;
        return 0;
    }

    QJsonArray edgesToJson() const {
        QJsonArray arr;
        for (const auto &e : m_edges) {
            QJsonObject obj;
            obj["from"] = (qint64)e.fromNodeId;
            obj["to"]   = (qint64)e.toNodeId;
            obj["kind"] = (int)e.edgeKind;
            obj["toPortIndex"] = destPortIndex(e.toPort->nodeItem(), e.toPort);
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
        PortItem *fromPort;
        PortItem *toPort;
    };

    void createConnection(PortItem *outPort, PortItem *inPort, ConnectionItem::EdgeKind kind) {
        auto *item = new ConnectionItem(outPort, inPort, kind);
        addItem(item);
        m_edges.append({ outPort->nodeItem()->nodeId(),
                         inPort->nodeItem()->nodeId(),
                         kind,
                         item,
                         outPort,
                         inPort });
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

void ConnectionItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *e) {
    QMenu menu;
    menu.addAction("Delete connection", [this]() {
        if (auto *s = static_cast<ClipNodeScene *>(scene()))
            s->removeConnection(this);
    });
    menu.exec(e->screenPos());
    e->accept();
}

void ClipNodeItem::updateLayout() {
    prepareGeometryChange();
    const qreal midY = nodeHeight() / 2.0;
    m_chainOutPort->setPos(NODE_W, midY);
    if (m_audioPort) m_audioPort->setPos(0, midY);
    if (m_shaderAudioInPort)
        m_shaderAudioInPort->setPos(0, IN_PORT_Y + PORT_R * 4);
    if (m_dataInPort)
        m_dataInPort->setPos(NODE_W, IN_PORT_Y + PORT_R * 4);
    if (scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    update();
}

PortItem::PortItem(PortKind kind, NodeItemBase *parentNode)
    : QGraphicsEllipseItem(-PORT_R, -PORT_R, PORT_R * 2, PORT_R * 2)
    , m_kind(kind), m_nodeItem(parentNode)
{
    setParentItem(parentNode);
    setAcceptHoverEvents(true);
    setZValue(2);
    setPen(Qt::NoPen);
    setBrush(Qt::NoBrush);
}

void PortItem::paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w) {
    Q_UNUSED(opt);
    Q_UNUSED(w);
    p->setRenderHint(QPainter::Antialiasing);

    QColor base = portKindColor(m_kind);
    if (m_connected) base = base.lighter(108);
    if (m_hovered)   base = base.lighter(125);

    if (m_hovered || m_connected) {
        QColor glow = base;
        glow.setAlpha(m_hovered ? 70 : 45);
        p->setPen(Qt::NoPen);
        p->setBrush(glow);
        p->drawEllipse(QPointF(0, 0), PORT_R + 2.5, PORT_R + 2.5);
    }

    p->setPen(QPen(QColor(12, 13, 15), 1.0));
    p->setBrush(base);
    p->drawEllipse(QPointF(0, 0), PORT_R, PORT_R);

    QColor highlight = base.lighter(155);
    highlight.setAlpha(160);
    p->setPen(Qt::NoPen);
    p->setBrush(highlight);
    p->drawEllipse(QPointF(-PORT_R * 0.2, -PORT_R * 0.25), PORT_R * 0.35, PORT_R * 0.3);
}

QPainterPath PortItem::shape() const {
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), PORT_HIT_R, PORT_HIT_R);
    return path;
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

QVariant ProcessNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

QVariant LayerNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

void LayerNodeItem::rebuildPorts() {
    prepareGeometryChange();
    for (auto *p : m_inPorts) { if (p->scene()) p->scene()->removeItem(p); delete p; }
    m_inPorts.clear();
    for (int i = 0; i < m_slots.size(); ++i)
        m_inPorts.push_back(new PortItem(PortKind::ChainIn, this));
    if (!m_outPort) m_outPort = new PortItem(PortKind::ChainOut, this);
    positionPorts();
}

void LayerNodeItem::positionPorts() {
    prepareGeometryChange();
    for (int i = 0; i < m_inPorts.size(); ++i)
        m_inPorts[i]->setPos(0, SW_HDR + i * SW_ROW + SW_ROW / 2);
    if (m_outPort) m_outPort->setPos(SW_W, nodeHeight() / 2.0);
    if (scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    update();
}

void LayerNodeItem::normalizeInputs() {
    QVector<PortItem *> newPorts;
    QVector<LayerSlot> newSlots;
    PortItem *trailingEmpty = nullptr;
    for (int i = 0; i < m_inPorts.size(); ++i) {
        if (m_inPorts[i]->isConnected()) {
            newPorts.push_back(m_inPorts[i]);
            newSlots.push_back(m_slots[i]);
        } else if (!trailingEmpty) {
            trailingEmpty = m_inPorts[i];       // reuse the first empty as the trailing slot
        } else {
            if (m_inPorts[i]->scene()) m_inPorts[i]->scene()->removeItem(m_inPorts[i]);
            delete m_inPorts[i];
        }
    }
    if (!trailingEmpty) trailingEmpty = new PortItem(PortKind::ChainIn, this);
    newPorts.push_back(trailingEmpty);
    newSlots.push_back(LayerSlot{});
    m_inPorts = newPorts;
    m_slots = newSlots;
    positionPorts();
}

QVariant AbSelectNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

void AbSelectNodeItem::rebuildPorts() {
    prepareGeometryChange();
    for (auto *p : m_inPorts) { if (p->scene()) p->scene()->removeItem(p); delete p; }
    m_inPorts.clear();
    for (int i = 0; i < m_slots.size(); ++i)
        m_inPorts.push_back(new PortItem(PortKind::ChainIn, this));
    if (!m_abOutPort) m_abOutPort = new PortItem(PortKind::AbOut, this);
    positionPorts();
}

void AbSelectNodeItem::positionPorts() {
    prepareGeometryChange();
    for (int i = 0; i < m_inPorts.size(); ++i)
        m_inPorts[i]->setPos(0, SW_HDR + i * SW_ROW + SW_ROW / 2);
    if (m_abOutPort) m_abOutPort->setPos(SW_W, nodeHeight() / 2.0);
    if (scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    update();
}

void AbSelectNodeItem::normalizeInputs() {
    QVector<PortItem *> newPorts;
    QVector<AbSlot> newSlots;
    PortItem *trailingEmpty = nullptr;
    for (int i = 0; i < m_inPorts.size(); ++i) {
        if (m_inPorts[i]->isConnected()) {
            newPorts.push_back(m_inPorts[i]);
            newSlots.push_back(m_slots[i]);
        } else if (!trailingEmpty) {
            trailingEmpty = m_inPorts[i];
        } else {
            if (m_inPorts[i]->scene()) m_inPorts[i]->scene()->removeItem(m_inPorts[i]);
            delete m_inPorts[i];
        }
    }
    if (!trailingEmpty) trailingEmpty = new PortItem(PortKind::ChainIn, this);
    newPorts.push_back(trailingEmpty);
    newSlots.push_back(AbSlot{});
    m_inPorts = newPorts;
    m_slots = newSlots;
    positionPorts();
}

QVariant OutputNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

QVariant MasterAudioOutputNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

QVariant MasterAudioInputNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

QVariant ScriptNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && scene())
        static_cast<ClipNodeScene *>(scene())->updateConnectionsForNode(this);
    return QGraphicsItem::itemChange(change, value);
}

class ClipNodeView : public QGraphicsView {
public:
    static constexpr qreal kMinZoom     = 0.2;
    static constexpr qreal kMaxZoom     = 3.0;
    static constexpr qreal kDefaultZoom = 1.0;
    static constexpr qreal kZoomStep    = 1.12;

    std::function<void()> onDeleteSelection;
    std::function<void(const QStringList &, const QPoint &)> onFileDrop;
    std::function<void(qreal)> onZoomChanged;

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
        setDragMode(RubberBandDrag);
        setFocusPolicy(Qt::StrongFocus);
        setAcceptDrops(true);
        scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    }

    qreal zoomScale() const { return transform().m11(); }

    void setZoomScale(qreal targetScale) {
        targetScale = std::clamp(targetScale, kMinZoom, kMaxZoom);
        const qreal current = zoomScale();
        if (qFuzzyCompare(current, targetScale))
            return;

        setTransformationAnchor(AnchorViewCenter);
        setResizeAnchor(AnchorViewCenter);
        const qreal factor = targetScale / current;
        QGraphicsView::scale(factor, factor);
        notifyZoomChanged();
    }

    void zoomBy(qreal factor) {
        setZoomScale(zoomScale() * factor);
    }

    void zoomIn() { zoomBy(kZoomStep); }

    void zoomOut() { zoomBy(1.0 / kZoomStep); }

    void resetZoom() {
        const QPointF center = mapToScene(viewport()->rect().center());
        resetTransform();
        centerOn(center);
        notifyZoomChanged();
    }

protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent *e) override {
        if (e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }

    void dropEvent(QDropEvent *e) override {
        if (!e->mimeData()->hasUrls()) {
            e->ignore();
            return;
        }
        e->acceptProposedAction();
        if (!onFileDrop)
            return;

        QStringList paths;
        for (const QUrl &url : e->mimeData()->urls()) {
            const QString path = url.toLocalFile();
            if (!path.isEmpty() && QFileInfo(path).isFile() && ClipManager::isMediaPath(path))
                paths << path;
        }
        if (!paths.isEmpty())
            onFileDrop(paths, e->position().toPoint());
    }

    void wheelEvent(QWheelEvent *e) override {
        const qreal current = zoomScale();
        const qreal factor  = e->angleDelta().y() > 0 ? kZoomStep : 1.0 / kZoomStep;
        const qreal target  = std::clamp(current * factor, kMinZoom, kMaxZoom);
        if (qFuzzyCompare(current, target)) {
            e->accept();
            return;
        }

        setTransformationAnchor(AnchorUnderMouse);
        setResizeAnchor(AnchorUnderMouse);
        QGraphicsView::scale(target / current, target / current);
        notifyZoomChanged();
        e->accept();
    }

    void keyPressEvent(QKeyEvent *e) override {
        if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && onDeleteSelection) {
            onDeleteSelection();
            e->accept();
            return;
        }
        if (e->modifiers() & Qt::ControlModifier) {
            if (e->key() == Qt::Key_Equal || e->key() == Qt::Key_Plus) {
                zoomIn();
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_Minus) {
                zoomOut();
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_0) {
                resetZoom();
                e->accept();
                return;
            }
        }
        QGraphicsView::keyPressEvent(e);
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
    void notifyZoomChanged() {
        if (onZoomChanged)
            onZoomChanged(zoomScale());
    }

    QPoint m_panStart;
    bool   m_panning = false;
};

class ClipNodeMinimap : public QWidget {
public:
    explicit ClipNodeMinimap(ClipNodeView *view, QWidget *parent = nullptr)
        : QWidget(parent), m_view(view)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setMouseTracking(true);
        setToolTip(tr("Canvas minimap — click or drag to pan"));

        if (m_view && m_view->scene()) {
            connect(m_view->scene(), &QGraphicsScene::changed, this, qOverload<>(&QWidget::update));
        }
        if (m_view) {
            connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
            connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
        }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        p.fillRect(rect(), QColor(20, 22, 26, 230));
        p.setPen(QColor(48, 52, 58));
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        if (!m_view || !m_view->scene()) {
            p.setPen(QColor(120, 120, 120));
            p.drawText(rect(), Qt::AlignCenter, tr("—"));
            return;
        }

        const QRectF bounds = contentBounds();
        if (!bounds.isValid() || bounds.isEmpty()) {
            p.setPen(QColor(120, 120, 120));
            p.drawText(rect(), Qt::AlignCenter, tr("Empty"));
            return;
        }

        const QRectF mini = innerRect();

        p.setBrush(QColor(74, 158, 255, 100));
        p.setPen(Qt::NoPen);
        for (QGraphicsItem *item : m_view->scene()->items()) {
            if (!item->isVisible() || !dynamic_cast<NodeItemBase *>(item))
                continue;
            const QPointF c = mapSceneToMini(item->sceneBoundingRect().center(), bounds, mini);
            p.drawEllipse(c, 2.5, 2.5);
        }

        const QPointF tl = mapSceneToMini(m_view->mapToScene(m_view->viewport()->rect().topLeft()),
                                          bounds, mini);
        const QPointF br = mapSceneToMini(m_view->mapToScene(m_view->viewport()->rect().bottomRight()),
                                          bounds, mini);
        const QRectF vpMini(QPointF(std::min(tl.x(), br.x()), std::min(tl.y(), br.y())),
                            QPointF(std::max(tl.x(), br.x()), std::max(tl.y(), br.y())));
        p.setPen(QPen(QColor(74, 158, 255), 1.5));
        p.setBrush(QColor(74, 158, 255, 35));
        p.drawRect(vpMini);
    }

    void mousePressEvent(QMouseEvent *e) override {
        m_dragging = true;
        panToMiniPos(e->pos());
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragging)
            panToMiniPos(e->pos());
        e->accept();
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton)
            m_dragging = false;
        QWidget::mouseReleaseEvent(e);
    }

private:
    QRectF innerRect() const { return rect().adjusted(4, 4, -4, -4); }

    QRectF contentBounds() const {
        QRectF bounds = m_view->scene()->itemsBoundingRect();
        if (!bounds.isValid() || bounds.isEmpty())
            return bounds;
        const qreal margin = 80.0;
        return bounds.adjusted(-margin, -margin, margin, margin);
    }

    static QPointF mapSceneToMini(const QPointF &scenePos, const QRectF &bounds, const QRectF &mini) {
        const qreal u = std::clamp((scenePos.x() - bounds.left()) / bounds.width(), 0.0, 1.0);
        const qreal v = std::clamp((scenePos.y() - bounds.top()) / bounds.height(), 0.0, 1.0);
        return QPointF(mini.left() + u * mini.width(), mini.top() + v * mini.height());
    }

    QPointF mapMiniToScene(const QPointF &miniPos, const QRectF &bounds, const QRectF &mini) const {
        const qreal u = (miniPos.x() - mini.left()) / mini.width();
        const qreal v = (miniPos.y() - mini.top()) / mini.height();
        return QPointF(bounds.left() + u * bounds.width(), bounds.top() + v * bounds.height());
    }

    void panToMiniPos(const QPoint &pos) {
        const QRectF bounds = contentBounds();
        if (!bounds.isValid() || bounds.isEmpty())
            return;
        const QRectF mini = innerRect();
        const QPointF clamped(
            std::clamp(static_cast<qreal>(pos.x()), mini.left(), mini.right()),
            std::clamp(static_cast<qreal>(pos.y()), mini.top(), mini.bottom()));
        const QPointF scenePos = mapMiniToScene(clamped, bounds, mini);
        m_view->centerOn(scenePos);
        update();
    }

    ClipNodeView *m_view = nullptr;
    bool          m_dragging = false;
};

class ClipNodeCanvasWidget : public QWidget {
public:
    explicit ClipNodeCanvasWidget(ClipNodeView *view, QWidget *parent = nullptr)
        : QWidget(parent), m_view(view)
    {
        m_view->setParent(this);

        m_minimap = new ClipNodeMinimap(m_view, this);
        m_minimap->setStyleSheet(QStringLiteral("border-radius: 6px;"));

        m_zoomBar = new QWidget(this);
        m_zoomBar->setStyleSheet(QStringLiteral(
            "background: rgba(20, 22, 26, 0.9); border: 1px solid #2a2d32; border-radius: 6px;"));
        auto *zLayout = new QHBoxLayout(m_zoomBar);
        zLayout->setContentsMargins(4, 4, 4, 4);
        zLayout->setSpacing(2);

        const QString btnStyle = QStringLiteral(
            "QToolButton { background: transparent; border: none; border-radius: 4px; color: #ccc; }"
            "QToolButton:hover { background: #2a5c66; }"
            "QToolButton:disabled { color: #555; }");

        auto *zoomOutBtn = new QToolButton(m_zoomBar);
        MaterialSymbols::setIconText(zoomOutBtn, MaterialSymbols::Names::ZoomOut, 18);
        zoomOutBtn->setToolTip(tr("Zoom out (Ctrl+-)"));
        zoomOutBtn->setFixedSize(28, 28);
        zoomOutBtn->setStyleSheet(btnStyle);

        m_zoomLabel = new QLabel(m_zoomBar);
        m_zoomLabel->setAlignment(Qt::AlignCenter);
        m_zoomLabel->setFixedWidth(44);
        m_zoomLabel->setStyleSheet(QStringLiteral("color: #aaa; font-size: 11px;"));
        m_zoomLabel->setText(QStringLiteral("100%"));

        auto *zoomResetBtn = new QToolButton(m_zoomBar);
        MaterialSymbols::setIconText(zoomResetBtn, MaterialSymbols::Names::ZoomReset, 18);
        zoomResetBtn->setToolTip(tr("Reset zoom to 100% (Ctrl+0)"));
        zoomResetBtn->setFixedSize(28, 28);
        zoomResetBtn->setStyleSheet(btnStyle);

        auto *zoomInBtn = new QToolButton(m_zoomBar);
        MaterialSymbols::setIconText(zoomInBtn, MaterialSymbols::Names::ZoomIn, 18);
        zoomInBtn->setToolTip(tr("Zoom in (Ctrl++)"));
        zoomInBtn->setFixedSize(28, 28);
        zoomInBtn->setStyleSheet(btnStyle);

        zLayout->addWidget(zoomOutBtn);
        zLayout->addWidget(m_zoomLabel);
        zLayout->addWidget(zoomResetBtn);
        zLayout->addWidget(zoomInBtn);

        connect(zoomOutBtn, &QToolButton::clicked, m_view, [this]() { m_view->zoomOut(); });
        connect(zoomInBtn, &QToolButton::clicked, m_view, [this]() { m_view->zoomIn(); });
        connect(zoomResetBtn, &QToolButton::clicked, m_view, [this]() { m_view->resetZoom(); });

        m_zoomOutBtn = zoomOutBtn;
        m_zoomInBtn  = zoomInBtn;

        m_view->onZoomChanged = [this](qreal scale) {
            syncZoomUi(scale);
            if (m_minimap)
                m_minimap->update();
        };
        syncZoomUi(m_view->zoomScale());
    }

    ClipNodeView *graphicsView() const { return m_view; }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        const QRect r = rect();
        m_view->setGeometry(r);

        constexpr int miniW = 148;
        constexpr int miniH = 96;
        m_minimap->setGeometry(r.width() - miniW - 10, r.height() - miniH - 10, miniW, miniH);

        m_zoomBar->adjustSize();
        const int barW = m_zoomBar->sizeHint().width();
        m_zoomBar->setGeometry(10, r.height() - 36, barW, 32);

        m_minimap->raise();
        m_zoomBar->raise();
    }

private:
    void syncZoomUi(qreal scale) {
        const int pct = qRound(scale * 100.0);
        m_zoomLabel->setText(QStringLiteral("%1%").arg(pct));
        if (m_zoomOutBtn)
            m_zoomOutBtn->setEnabled(scale > ClipNodeView::kMinZoom + 0.01);
        if (m_zoomInBtn)
            m_zoomInBtn->setEnabled(scale < ClipNodeView::kMaxZoom - 0.01);
    }

    ClipNodeView      *m_view = nullptr;
    ClipNodeMinimap   *m_minimap = nullptr;
    QWidget           *m_zoomBar = nullptr;
    QLabel            *m_zoomLabel = nullptr;
    QToolButton       *m_zoomOutBtn = nullptr;
    QToolButton       *m_zoomInBtn = nullptr;
};

ClipNodeEditor::ClipNodeEditor(QWidget *parent)
    : QWidget(parent)
{
    m_scene = new ClipNodeScene(this);
    m_scene->onConnectionChanged = [this]() {
        if (!m_restoring) { normalizeSwitchingInputs(); updateAbHighlights(); }
        emit clipChainChanged();
        emit audioGraphChanged();
    };

    m_view = new ClipNodeView(m_scene);
    auto *nodeView = static_cast<ClipNodeView *>(m_view);
    nodeView->onDeleteSelection = [this]() { deleteSelection(m_view); };
    nodeView->onFileDrop = [this](const QStringList &paths, const QPoint &viewPos) {
        const QPoint globalPos = m_view->mapToGlobal(viewPos);
        for (int i = 0; i < paths.size(); ++i) {
            ClipNodeModel *model = addClipNode(paths.at(i),
                                               ThumbnailExtractor::extract(paths.at(i), 110, 65),
                                               nullptr, m_view);
            if (auto *item = dynamic_cast<ClipNodeItem *>(m_itemMap.value(model->nodeId())))
                item->setPos(scenePosForView(m_view, globalPos) + QPointF(i * 20.0, i * 20.0));
        }
    };

    auto *canvas = new ClipNodeCanvasWidget(nodeView, this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(canvas);
    setLayout(layout);

    connect(m_view, &QGraphicsView::customContextMenuRequested, this, &ClipNodeEditor::onCanvasContextMenu);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
}

ClipNodeEditor::~ClipNodeEditor() {
    blockSignals(true);
    if (m_view)
        m_view->setScene(nullptr);
    clearAllNodes();
}

QGraphicsView *ClipNodeEditor::graphicsViewFrom(QWidget *widget) {
    if (auto *canvas = dynamic_cast<ClipNodeCanvasWidget *>(widget))
        return canvas->graphicsView();
    return qobject_cast<QGraphicsView *>(widget);
}


// ── Session persistence ───────────────────────────────────────────────────────

static QJsonObject descriptorToJson(const SourceDescriptor &d, const QDir &sessionDir) {
    QJsonObject o;
    o["kind"]               = (int)d.kind;
    o["path"]               = sessionDir.isAbsolute()
        ? AssetPathResolver::storePath(d.path, sessionDir) : d.path;
    o["displayName"]        = d.displayName;
    o["color"]              = d.color.name(QColor::HexArgb);
    o["cameraIndex"]        = d.cameraIndex;
    o["screenIndex"]        = d.screenIndex;
    o["windowIndex"]        = d.windowIndex;
    o["slideshowIntervalMs"]= d.slideshowIntervalMs;
    o["slideshowEffect"]    = d.slideshowEffect;
    o["slideshowTransitionMs"] = d.slideshowTransitionMs;
    o["canvasWidth"]        = d.canvasWidth;
    o["canvasHeight"]       = d.canvasHeight;
    o["canvasFill"]         = (int)d.canvasFill;
    o["shaderCode"]         = d.shaderCode;
    o["htmlContent"]        = d.htmlContent;
    o["htmlWorkspace"]      = d.htmlWorkspace;
    o["obsSceneName"]       = d.obsSceneName;
    o["textTemplate"]       = d.textTemplate;
    o["fontFamily"]         = d.fontFamily;
    o["fontSize"]           = d.fontSize;
    o["textAlign"]          = d.textAlign;
    o["textBgTransparent"]  = d.textBgTransparent;
    o["textBgColor"]        = d.textBgColor.name(QColor::HexArgb);
    o["webrtcRelayUrl"]     = d.webrtcRelayUrl;
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
    d.slideshowEffect    = o["slideshowEffect"].toInt(0);
    d.slideshowTransitionMs = o["slideshowTransitionMs"].toInt(800);
    d.canvasWidth        = o["canvasWidth"].toInt(1280);
    d.canvasHeight       = o["canvasHeight"].toInt(720);
    d.canvasFill         = (SourceDescriptor::CanvasFill)o["canvasFill"].toInt();
    d.shaderCode         = o["shaderCode"].toString();
    d.htmlContent        = o["htmlContent"].toString();
    d.htmlWorkspace      = o["htmlWorkspace"].toString();
    d.obsSceneName       = o["obsSceneName"].toString();
    d.textTemplate       = o["textTemplate"].toString();
    d.fontFamily         = o["fontFamily"].toString(QStringLiteral("Sans Serif"));
    d.fontSize           = o["fontSize"].toInt(48);
    d.textAlign          = o["textAlign"].toInt(0x0084);
    d.textBgTransparent  = o["textBgTransparent"].toBool(true);
    d.textBgColor        = QColor(o["textBgColor"].toString("#ff000000"));
    d.webrtcRelayUrl     = o["webrtcRelayUrl"].toString();
    return d;
}

// ── Node registration / callbacks ───────────────────────────────────────────

static void wireDeleteCallback(NodeItemBase *item, const std::function<void(NodeId)> &cb) {
    if (auto *clip = dynamic_cast<ClipNodeItem *>(item)) clip->onDeleteRequested = cb;
    else if (auto *pr = dynamic_cast<ProcessNodeItem *>(item)) pr->onDeleteRequested = cb;
    else if (auto *ly = dynamic_cast<LayerNodeItem *>(item)) ly->onDeleteRequested = cb;
    else if (auto *ab = dynamic_cast<AbSelectNodeItem *>(item)) ab->onDeleteRequested = cb;
    else if (auto *scr = dynamic_cast<ScriptNodeItem *>(item)) scr->onDeleteRequested = cb;
    else if (auto *mas = dynamic_cast<MasterAudioOutputNodeItem *>(item)) mas->onDeleteRequested = cb;
    else if (auto *mai = dynamic_cast<MasterAudioInputNodeItem *>(item)) mai->onDeleteRequested = cb;
}

static void wireEditAudioCallback(ClipNodeItem *item, const std::function<void(NodeId)> &cb) {
    if (item) item->onEditAudioRequested = cb;
}

void ClipNodeEditor::registerItem(NodeItemBase *item) {
    m_itemMap[item->nodeId()] = item;
}

void ClipNodeEditor::removeSceneItem(NodeItemBase *item) {
    if (!item || !item->scene()) return;
    item->scene()->removeItem(item);
    delete item;
}

ClipNodeScene *ClipNodeEditor::sceneForNode(NodeId id) const {
    if (auto *item = m_itemMap.value(id))
        return static_cast<ClipNodeScene *>(item->scene());
    return m_scene;
}

void ClipNodeEditor::ensureOutputNode() {
    if (m_outputNode != 0) return;
    const NodeId id = m_nextId++;
    auto *out = new OutputNodeItem(id);
    out->setPos(360, 40);
    m_scene->addItem(out);
    m_outputNode = id;
    registerItem(out);
}

// ── Input node management ───────────────────────────────────────────────────

ClipNodeModel *ClipNodeEditor::addClipNode(const QString &path, const QPixmap &thumbnail,
                                           ClipNodeScene *targetScene,
                                           QGraphicsView *viewForPos,
                                           bool /*groupMember*/) {
    ClipNodeScene *scene = targetScene ? targetScene : m_scene;
    QGraphicsView *view = viewForPos ? viewForPos : m_view;

    const bool hasAudio = VideoPlayer::fileHasAudio(path);

    const NodeId id = m_nextId++;
    auto *model = new ClipNodeModel(this);
    auto *nodeItem = new ClipNodeItem(model, id, hasAudio);

    if (viewForPos)
        nodeItem->setPos(scenePosForView(view, QCursor::pos()));
    else {
        const int idx = m_nodeMap.size();
        nodeItem->setPos(idx * 160.0 + 20.0, idx * 60.0 + 20.0);
    }

    scene->addItem(nodeItem);
    m_nodeMap[id] = model;
    registerItem(nodeItem);
    wireDeleteCallback(nodeItem, [this](NodeId nid) { deleteNodeById(nid); });
    wireEditAudioCallback(nodeItem, [this](NodeId cid) { onEditClipAudio(cid); });

    model->setNodeId(id);
    model->loadClip(path, thumbnail);
    connectNodeSignals(model, id);

    ensureOutputNode();
    emit nodeAdded(id);
    return model;
}

ClipNodeModel *ClipNodeEditor::addSourceNode(const SourceDescriptor &desc, const QPixmap &thumbnail,
                                             ClipNodeScene *targetScene,
                                             QGraphicsView *viewForPos,
                                             bool /*groupMember*/) {
    ClipNodeScene *scene = targetScene ? targetScene : m_scene;
    QGraphicsView *view = viewForPos ? viewForPos : m_view;

    const NodeId id = m_nextId++;
    auto *model = new ClipNodeModel(this);
    const bool hasShaderAudioIn = (desc.kind == SourceDescriptor::Kind::Shader);
    const bool hasDataIn = (desc.kind == SourceDescriptor::Kind::Text);
    auto *nodeItem = new ClipNodeItem(model, id, false, hasShaderAudioIn, hasDataIn);

    if (viewForPos)
        nodeItem->setPos(scenePosForView(view, QCursor::pos()));
    else {
        const int idx = m_nodeMap.size();
        nodeItem->setPos(idx * 160.0 + 20.0, idx * 60.0 + 20.0);
    }

    scene->addItem(nodeItem);
    m_nodeMap[id] = model;
    registerItem(nodeItem);
    wireDeleteCallback(nodeItem, [this](NodeId nid) { deleteNodeById(nid); });

    model->setNodeId(id);
    model->loadSource(desc, thumbnail);
    connectNodeSignals(model, id);

    ensureOutputNode();
    emit nodeAdded(id);
    return model;
}

// ── Process / Layer / A-B node creation ─────────────────────────────────────

void ClipNodeEditor::addProcessNodeAt(int effect, const QPoint &globalPos) {
    const NodeId id = m_nextId++;
    auto *node = new ProcessNodeItem(id, (ProcessNodeItem::Effect)effect);
    node->setPos(scenePosForView(m_view, globalPos));
    node->onEditRequested = [this](NodeId nid) { onEditProcessCrop(nid); };
    m_scene->addItem(node);
    m_processNodes[id] = static_cast<void *>(node);
    registerItem(node);
    wireDeleteCallback(node, [this](NodeId nid) { deleteNodeById(nid); });
    ensureOutputNode();
    emit clipChainChanged();
}

void ClipNodeEditor::addLayerNodeAt(const QPoint &globalPos) {
    const NodeId id = m_nextId++;
    auto *node = new LayerNodeItem(id);
    node->setPos(scenePosForView(m_view, globalPos));
    node->onEditRequested = [this](NodeId nid) { onEditLayerTransform(nid); };
    node->onEditCanvasRequested = [this](NodeId nid) { onEditLayerCanvas(nid); };
    node->onChanged = [this]() { emit clipChainChanged(); };
    m_scene->addItem(node);
    m_layerNodes[id] = static_cast<void *>(node);
    registerItem(node);
    wireDeleteCallback(node, [this](NodeId nid) { deleteNodeById(nid); });
    ensureOutputNode();
    emit clipChainChanged();
}

void ClipNodeEditor::updateAbHighlights() {
    for (auto it = m_abSelectNodes.cbegin(); it != m_abSelectNodes.cend(); ++it) {
        auto *ab = static_cast<AbSelectNodeItem *>(it.value());
        ab->setOutputConnected(m_scene->portHasEdge(ab->abOutPort()));
        int aSlot = -1, bSlot = -1;
        for (int i = 0; i < ab->slotCount(); ++i) {
            const NodeId prod = m_scene->producerForInputPort(ab->inPort(i));
            if (prod != 0 && prod == m_deckAInput) aSlot = i;
            if (prod != 0 && prod == m_deckBInput) bSlot = i;
        }
        ab->setDeckSlots(aSlot, bSlot);
    }
}

void ClipNodeEditor::addAbSelectNodeAt(const QPoint &globalPos) {
    const NodeId id = m_nextId++;
    auto *node = new AbSelectNodeItem(id);
    node->setPos(scenePosForView(m_view, globalPos));
    node->onChanged = [this]() { emit clipChainChanged(); };
    node->onAssignDeck = [this](NodeId abId, int slot, bool deckA) {
        auto *ab = static_cast<AbSelectNodeItem *>(m_abSelectNodes.value(abId));
        if (!ab) return;
        const NodeId prod = m_scene->producerForInputPort(ab->inPort(slot));
        if (prod == 0) return;
        assignInputToDeck(prod, deckA);
    };
    m_scene->addItem(node);
    m_abSelectNodes[id] = static_cast<void *>(node);
    registerItem(node);
    wireDeleteCallback(node, [this](NodeId nid) { deleteNodeById(nid); });
    ensureOutputNode();
    emit clipChainChanged();
}

// ── Deletion ────────────────────────────────────────────────────────────────

void ClipNodeEditor::deleteNodeById(NodeId nodeId) {
    if (nodeId == m_outputNode) {
        const bool onlyOutput = m_nodeMap.isEmpty() && m_processNodes.isEmpty()
            && m_layerNodes.isEmpty() && m_abSelectNodes.isEmpty()
            && m_scriptNodes.isEmpty() && m_masterAudioNodes.isEmpty()
            && m_masterAudioInputNodes.isEmpty();
        if (!onlyOutput) return;   // Output is not deletable while other nodes exist.
    }

    ClipNodeScene *scene = sceneForNode(nodeId);
    NodeItemBase *item = m_itemMap.value(nodeId);
    if (!item) return;
    scene->removeConnectionsForNode(nodeId);

    if (m_deckAInput == nodeId) { m_deckAInput = 0; emit deckAClipChanged(0); }
    if (m_deckBInput == nodeId) { m_deckBInput = 0; emit deckBClipChanged(0); }

    if (m_nodeMap.contains(nodeId)) {
        ClipNodeModel *model = m_nodeMap.take(nodeId);
        m_itemMap.remove(nodeId);
        disconnectNodeSignals(model);
        model->clearCard();
        removeSceneItem(item);
        delete model;
        emit nodeRemoved(nodeId);
        emit clipChainChanged();
        emit audioGraphChanged();
        return;
    }

    m_itemMap.remove(nodeId);
    m_processNodes.remove(nodeId);
    m_layerNodes.remove(nodeId);
    m_abSelectNodes.remove(nodeId);
    m_scriptNodes.remove(nodeId);
    m_masterAudioNodes.remove(nodeId);
    m_masterAudioInputNodes.remove(nodeId);
    if (nodeId == m_outputNode) m_outputNode = 0;
    removeSceneItem(item);
    updateAbHighlights();
    emit clipChainChanged();
    emit audioGraphChanged();
}

void ClipNodeEditor::removeNode(NodeId nodeId) {
    deleteNodeById(nodeId);
}

void ClipNodeEditor::deleteSelection(QGraphicsView *fromView) {
    if (!fromView) fromView = m_view;
    auto *scene = static_cast<ClipNodeScene *>(fromView->scene());

    QList<NodeId> nodeIds;
    QList<ConnectionItem *> connections;
    for (auto *item : scene->selectedItems()) {
        if (auto *conn = dynamic_cast<ConnectionItem *>(item))
            connections.append(conn);
        else if (auto *node = dynamic_cast<NodeItemBase *>(item))
            nodeIds.append(node->nodeId());
    }

    for (NodeId id : nodeIds) {
        if (m_itemMap.contains(id))
            deleteNodeById(id);
    }
    for (auto *conn : connections) {
        if (conn->scene())
            scene->removeConnection(conn);
    }
}

void ClipNodeEditor::clearAllNodes() {
    const QVector<NodeId> clipIds = m_nodeMap.keys().toVector();
    for (NodeId id : clipIds) deleteNodeById(id);

    for (NodeId id : m_processNodes.keys().toVector()) deleteNodeById(id);
    for (NodeId id : m_layerNodes.keys().toVector()) deleteNodeById(id);
    for (NodeId id : m_abSelectNodes.keys().toVector()) deleteNodeById(id);
    for (NodeId id : m_masterAudioNodes.keys().toVector()) deleteNodeById(id);
    for (NodeId id : m_masterAudioInputNodes.keys().toVector()) deleteNodeById(id);
    for (NodeId id : m_scriptNodes.keys().toVector()) deleteNodeById(id);

    if (m_outputNode != 0) {
        if (auto *item = m_itemMap.value(m_outputNode)) {
            m_scene->removeConnectionsForNode(m_outputNode);
            removeSceneItem(item);
        }
        m_itemMap.remove(m_outputNode);
        m_outputNode = 0;
    }

    m_itemMap.clear();
    m_processNodes.clear();
    m_layerNodes.clear();
    m_abSelectNodes.clear();
    m_scriptNodes.clear();
    m_masterAudioNodes.clear();
    m_masterAudioInputNodes.clear();
    m_deckAInput = 0;
    m_deckBInput = 0;
}

QVector<ClipNodeModel *> ClipNodeEditor::allNodes() const {
    return m_nodeMap.values().toVector();
}

ClipNodeModel *ClipNodeEditor::nodeAt(NodeId id) const {
    auto it = m_nodeMap.find(id);
    return (it != m_nodeMap.end()) ? *it : nullptr;
}

// ── Deck assignment ─────────────────────────────────────────────────────────

void ClipNodeEditor::setActiveDeckClip(NodeId clipId, bool deckA) {
    if (deckA) { m_deckAInput = clipId; emit deckAClipChanged(clipId); }
    else       { m_deckBInput = clipId; emit deckBClipChanged(clipId); }
    updateAbHighlights();
    emit clipChainChanged();
}

void ClipNodeEditor::assignInputToDeck(NodeId inputProducerNode, bool deckA) {
    if (deckA) {
        if (m_deckBInput == inputProducerNode) { m_deckBInput = 0; emit deckBClipChanged(0); }
        m_deckAInput = inputProducerNode;
        emit deckAClipChanged(inputProducerNode);
    } else {
        if (m_deckAInput == inputProducerNode) { m_deckAInput = 0; emit deckAClipChanged(0); }
        m_deckBInput = inputProducerNode;
        emit deckBClipChanged(inputProducerNode);
    }
    updateAbHighlights();
    emit clipChainChanged();
}

// ── Evaluator ───────────────────────────────────────────────────────────────

ResolvedStream ClipNodeEditor::evaluateVideoInput(NodeId producerNode) const {
    return evaluateVideoInputGuarded(producerNode, {});
}

ResolvedStream ClipNodeEditor::evaluateVideoInputGuarded(NodeId producerNode,
                                                         QSet<NodeId> visited) const {
    ResolvedStream out;
    if (producerNode == 0 || visited.contains(producerNode)) return out;
    visited.insert(producerNode);

    NodeItemBase *item = m_itemMap.value(producerNode);
    if (!item) return out;

    if (dynamic_cast<ClipNodeItem *>(item)) {
        ResolvedLayer layer;
        layer.inputNodeId = producerNode;
        out.layers.push_back(layer);
        return out;
    }

    if (auto *pr = dynamic_cast<ProcessNodeItem *>(item)) {
        const NodeId up = m_scene->producerForInputPort(pr->inPort());
        out = evaluateVideoInputGuarded(up, visited);
        for (ResolvedLayer &l : out.layers) {
            if (pr->effect() == ProcessNodeItem::Effect::FlipH) {
                l.flipH = !l.flipH;
            } else if (pr->effect() == ProcessNodeItem::Effect::FlipV) {
                l.flipV = !l.flipV;
            } else { // Crop
                float nx, ny, nw, nh;
                pr->cropRect(nx, ny, nw, nh);
                float ax = l.flipH ? (1.f - nx - nw) : nx;
                float ay = l.flipV ? (1.f - ny - nh) : ny;
                l.cropX = l.cropX + ax * l.cropW;
                l.cropY = l.cropY + ay * l.cropH;
                l.cropW = nw * l.cropW;
                l.cropH = nh * l.cropH;
            }
        }
        return out;
    }

    if (auto *ly = dynamic_cast<LayerNodeItem *>(item)) {
        // Bottom of the list = bottom-most layer, so walk slots last→first
        // (ResolvedStream index 0 is the bottom layer).
        for (int i = ly->slotCount() - 1; i >= 0; --i) {
            const NodeId up = m_scene->producerForInputPort(ly->inPort(i));
            if (up == 0) continue;
            ResolvedStream sub = evaluateVideoInputGuarded(up, visited);
            const LayerSlot &s = ly->slot(i);
            for (ResolvedLayer &l : sub.layers) {
                l.baseX = s.baseX; l.baseY = s.baseY; l.baseW = s.baseW; l.baseH = s.baseH;
                l.visible = l.visible && s.visible;
                out.layers.push_back(l);
            }
        }
        out.canvasWidth = ly->canvasW();
        out.canvasHeight = ly->canvasH();
        return out;
    }

    // A/B-select and everything else are not blue producers.
    return out;
}

// ── Output querying ─────────────────────────────────────────────────────────

bool ClipNodeEditor::outputIsSingleStream() const {
    return outputSingleProducer() != 0;
}

NodeId ClipNodeEditor::outputSingleProducer() const {
    if (m_outputNode == 0) return 0;
    auto *out = dynamic_cast<OutputNodeItem *>(m_itemMap.value(m_outputNode));
    if (!out) return 0;
    return m_scene->producerForInputPort(out->chainInPort());
}

// ── Layer placement views ───────────────────────────────────────────────────

void ClipNodeEditor::normalizeSwitchingInputs() {
    auto defaultName = [this](NodeId producer) -> QString {
        const ResolvedStream s = evaluateVideoInput(producer);
        if (!s.layers.isEmpty())
            if (auto *m = nodeAt(s.layers.first().inputNodeId))
                return m->sourceName();
        return {};
    };
    for (auto it = m_layerNodes.cbegin(); it != m_layerNodes.cend(); ++it) {
        auto *ly = static_cast<LayerNodeItem *>(it.value());
        ly->normalizeInputs();
        for (int i = 0; i < ly->slotCount(); ++i) {
            if (!ly->slotsRef()[i].name.isEmpty()) continue;
            const NodeId prod = m_scene->producerForInputPort(ly->inPort(i));
            if (prod != 0) ly->slotsRef()[i].name = defaultName(prod);
        }
    }
    for (auto it = m_abSelectNodes.cbegin(); it != m_abSelectNodes.cend(); ++it) {
        auto *ab = static_cast<AbSelectNodeItem *>(it.value());
        ab->normalizeInputs();
        for (int i = 0; i < ab->slotCount(); ++i) {
            if (!ab->slotsRef()[i].name.isEmpty()) continue;
            const NodeId prod = m_scene->producerForInputPort(ab->inPort(i));
            if (prod != 0) ab->slotsRef()[i].name = defaultName(prod);
        }
    }
}

QVector<ClipNodeEditor::LayerSlotView> ClipNodeEditor::layerSlotViews(NodeId layerId) const {
    QVector<LayerSlotView> views;
    auto *ly = dynamic_cast<LayerNodeItem *>(m_itemMap.value(layerId));
    if (!ly) return views;
    // Reversed so the canvas z-order matches the output (bottom of the node list =
    // bottom layer, drawn first; top of the list = top layer, drawn last/on top).
    for (int i = ly->slotCount() - 1; i >= 0; --i) {
        const NodeId up = m_scene->producerForInputPort(ly->inPort(i));
        if (up == 0) continue;   // skip the trailing empty input
        LayerSlotView v;
        v.index = i;
        const LayerSlot &s = ly->slot(i);
        v.rect = QRectF(s.baseX, s.baseY, s.baseW, s.baseH);
        ResolvedStream sub = evaluateVideoInput(up);
        if (!sub.layers.isEmpty()) {
            if (auto *m = nodeAt(sub.layers.first().inputNodeId))
                v.thumb = m->thumbnail();
        }
        views.push_back(v);
    }
    return views;
}

void ClipNodeEditor::setLayerSlotRect(NodeId layerId, int index, float x, float y, float w, float h) {
    auto *ly = dynamic_cast<LayerNodeItem *>(m_itemMap.value(layerId));
    if (!ly || index < 0 || index >= ly->slotCount()) return;
    LayerSlot &s = ly->slotsRef()[index];
    s.baseX = x; s.baseY = y; s.baseW = w; s.baseH = h;
    ly->update();
    emit clipChainChanged();
}

bool ClipNodeEditor::layerCanvasSize(NodeId layerId, int &w, int &h) const {
    auto *ly = dynamic_cast<LayerNodeItem *>(m_itemMap.value(layerId));
    if (!ly) return false;
    w = ly->canvasW(); h = ly->canvasH();
    return true;
}

// ── Audio / script queries ──────────────────────────────────────────────────

bool ClipNodeEditor::audioSettingsForClip(NodeId clipId, int &volume, bool &muted, bool &routedToMaster, AudioPlaybackMode &playbackMode, int &delayMs, QString &outputDeviceId) const {
    auto *clip = dynamic_cast<ClipNodeItem *>(m_itemMap.value(clipId));
    if (!clip || !clip->hasAudio()) return false;

    volume = clip->volume();
    muted = clip->muted();
    const NodeId masterId = m_scene->masterNodeForClip(clipId);
    routedToMaster = (masterId != 0);
    outputDeviceId.clear();
    if (masterId) {
        if (auto *mn = dynamic_cast<MasterAudioOutputNodeItem *>(m_itemMap.value(masterId)))
            outputDeviceId = mn->deviceId();
    }
    playbackMode = clip->playbackMode();
    delayMs = clip->delayMs();
    return true;
}

bool ClipNodeEditor::masterAudioInputSettings(NodeId inputNodeId, MasterAudioInputSettings &settings) const {
    auto *inputNode = dynamic_cast<MasterAudioInputNodeItem *>(m_itemMap.value(inputNodeId));
    if (!inputNode) return false;

    settings.inputDeviceId = inputNode->deviceId();
    settings.inputDeviceLabel = inputNode->deviceLabel();
    settings.volume = inputNode->volume();
    settings.muted = inputNode->muted();
    settings.routedMasterOutputId = m_scene->masterOutputForInput(inputNodeId);
    return true;
}

bool ClipNodeEditor::masterAudioOutputDevice(NodeId masterOutputNodeId, QString &outputDeviceId) const {
    auto *outputNode = dynamic_cast<MasterAudioOutputNodeItem *>(m_itemMap.value(masterOutputNodeId));
    if (!outputNode) return false;
    outputDeviceId = outputNode->deviceId();
    return true;
}

QVector<NodeId> ClipNodeEditor::allMasterAudioInputNodeIds() const {
    return m_masterAudioInputNodes.keys().toVector();
}

bool ClipNodeEditor::audioSourceForShader(NodeId shaderNodeId, QString &filePath) const {
    const NodeId clipId = m_scene->clipForShaderAudio(shaderNodeId);
    if (clipId == 0) return false;
    const ClipNodeModel *node = nodeAt(clipId);
    if (!node) return false;
    const SourceDescriptor &desc = node->sourceDescriptor();
    if (desc.kind != SourceDescriptor::Kind::VideoFile) return false;
    filePath = desc.path;
    return !filePath.isEmpty();
}

std::shared_ptr<ScriptOutput> ClipNodeEditor::scriptOutputForDataNode(NodeId dataNodeId) const {
    const NodeId scriptId = m_scene->scriptNodeForData(dataNodeId);
    if (scriptId == 0) return nullptr;
    auto *scriptNode = static_cast<ScriptNodeItem *>(m_scriptNodes.value(scriptId));
    return scriptNode ? scriptNode->output() : nullptr;
}

// ── Context menu / add-node flow ────────────────────────────────────────────

void ClipNodeEditor::onCanvasContextMenu() {
    QMenu menu;
    const QPoint pos = QCursor::pos();
    menu.addAction("Add Input Node…", this, [this]() { emit addInputNodeRequested(); });

    QMenu *proc = menu.addMenu("Add Process Node");
    proc->addAction("Crop", this, [this, pos]() { addProcessNodeAt((int)ProcessNodeItem::Effect::Crop, pos); });
    proc->addAction("Flip Horizontal", this, [this, pos]() { addProcessNodeAt((int)ProcessNodeItem::Effect::FlipH, pos); });
    proc->addAction("Flip Vertical", this, [this, pos]() { addProcessNodeAt((int)ProcessNodeItem::Effect::FlipV, pos); });

    QMenu *sw = menu.addMenu("Add Switching Node");
    sw->addAction("Layer", this, [this, pos]() { addLayerNodeAt(pos); });
    sw->addAction("A/B Deck Select", this, [this, pos]() { addAbSelectNodeAt(pos); });

    menu.addSeparator();
    menu.addAction("Add Master Audio Output", this, &ClipNodeEditor::onAddMasterAudioOutput);
    menu.addAction("Add Master Audio Input", this, &ClipNodeEditor::onAddMasterAudioInput);
#ifdef PRISM_HAVE_LUA
    menu.addAction("Add Script Node", this, &ClipNodeEditor::onAddScriptNode);
#endif
    menu.exec(pos);
}

void ClipNodeEditor::onAddMasterAudioOutput() { addMasterAudioOutputTo(m_scene, m_view, QCursor::pos()); }
void ClipNodeEditor::onAddMasterAudioInput()  { addMasterAudioInputTo(m_scene, m_view, QCursor::pos()); }
void ClipNodeEditor::onAddScriptNode()        { addScriptNodeTo(m_scene, m_view, QCursor::pos()); }

QPointF ClipNodeEditor::scenePosForView(QGraphicsView *view, const QPoint &globalPos) const {
    if (!view) return QPointF(20, 20);
    return view->mapToScene(view->mapFromGlobal(globalPos));
}

void ClipNodeEditor::addMasterAudioOutputTo(ClipNodeScene *scene, QGraphicsView *view,
                                            const QPoint &globalPos) {
    if (!scene) return;
    auto *masterNode = new MasterAudioOutputNodeItem(m_nextId++);
    masterNode->setPos(scenePosForView(view, globalPos));
    scene->addItem(masterNode);
    m_masterAudioNodes[masterNode->nodeId()] = static_cast<void *>(masterNode);
    masterNode->onDeviceChanged = [this](NodeId) { emit audioGraphChanged(); };
    registerItem(masterNode);
    wireDeleteCallback(masterNode, [this](NodeId nid) { deleteNodeById(nid); });
    emit audioGraphChanged();
}

void ClipNodeEditor::addMasterAudioInputTo(ClipNodeScene *scene, QGraphicsView *view,
                                           const QPoint &globalPos) {
    if (!scene) return;
    auto *inputNode = new MasterAudioInputNodeItem(m_nextId++);
    inputNode->setPos(scenePosForView(view, globalPos));
    scene->addItem(inputNode);
    m_masterAudioInputNodes[inputNode->nodeId()] = static_cast<void *>(inputNode);
    inputNode->onSettingsChanged = [this](NodeId) { emit audioGraphChanged(); };
    registerItem(inputNode);
    wireDeleteCallback(inputNode, [this](NodeId nid) { deleteNodeById(nid); });
    emit audioGraphChanged();
}

void ClipNodeEditor::addScriptNodeTo(ClipNodeScene *scene, QGraphicsView *view,
                                     const QPoint &globalPos) {
#ifdef PRISM_HAVE_LUA
    if (!scene) return;
    ScriptEditDialog dlg(QString(), ScriptTriggerMode::Periodic, 1000, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString code = dlg.resultCode().trimmed();
    if (code.isEmpty()) return;

    auto *scriptNode = new ScriptNodeItem(m_nextId++, code,
                                          dlg.resultTriggerMode(), dlg.resultIntervalMs());
    scriptNode->setPos(scenePosForView(view, globalPos));
    scriptNode->onEditRequested = [this](NodeId sid) { onEditScriptNode(sid); };
    scene->addItem(scriptNode);
    m_scriptNodes[scriptNode->nodeId()] = static_cast<void *>(scriptNode);
    registerItem(scriptNode);
    wireDeleteCallback(scriptNode, [this](NodeId nid) { deleteNodeById(nid); });
    emit audioGraphChanged();
#else
    Q_UNUSED(scene); Q_UNUSED(view); Q_UNUSED(globalPos);
#endif
}

// ── Edit dialogs ────────────────────────────────────────────────────────────

void ClipNodeEditor::onEditClipAudio(NodeId clipId) {
    auto *clip = dynamic_cast<ClipNodeItem *>(m_itemMap.value(clipId));
    if (!clip || !clip->hasAudio()) return;

    QDialog dialog(this);
    Ui::AudioNodeDialog ui;
    ui.setupUi(&dialog);
    ui.volumeSpin->setValue(clip->volume());
    ui.mutedCheck->setChecked(clip->muted());
    ui.modeCombo->setCurrentIndex((int)clip->playbackMode());
    ui.delaySpin->setValue(clip->delayMs());
    if (dialog.exec() == QDialog::Accepted) {
        clip->setVolume(ui.volumeSpin->value());
        clip->setMuted(ui.mutedCheck->isChecked());
        clip->setPlaybackMode((AudioPlaybackMode)ui.modeCombo->currentIndex());
        clip->setDelayMs(ui.delaySpin->value());
        emit audioControllerChanged(clipId);
    }
}

void ClipNodeEditor::onEditScriptNode(NodeId nodeId) {
#ifdef PRISM_HAVE_LUA
    auto *scriptNode = static_cast<ScriptNodeItem *>(m_scriptNodes.value(nodeId));
    if (!scriptNode) return;
    ScriptEditDialog dlg(scriptNode->code(), scriptNode->triggerMode(),
                         scriptNode->intervalMs(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString code = dlg.resultCode().trimmed();
    if (code.isEmpty()) return;
    scriptNode->applySettings(code, dlg.resultTriggerMode(), dlg.resultIntervalMs());
    emit audioGraphChanged();
#else
    Q_UNUSED(nodeId);
#endif
}

void ClipNodeEditor::onEditLayerCanvas(NodeId layerId) {
    auto *ly = dynamic_cast<LayerNodeItem *>(m_itemMap.value(layerId));
    if (!ly) return;

    QDialog dialog(this);
    Ui::ContextNodeDialog ui;
    ui.setupUi(&dialog);
    ui.wSpin->setValue(ly->canvasW());
    ui.hSpin->setValue(ly->canvasH());
    connect(ui.presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, [&ui](int idx) {
        if      (idx == 1) { ui.wSpin->setValue(1280); ui.hSpin->setValue(720);  }
        else if (idx == 2) { ui.wSpin->setValue(1920); ui.hSpin->setValue(1080); }
        else if (idx == 3) { ui.wSpin->setValue(1024); ui.hSpin->setValue(768);  }
    });
    if (dialog.exec() == QDialog::Accepted) {
        ly->setCanvasSize(ui.wSpin->value(), ui.hSpin->value());
        emit clipChainChanged();
    }
}

void ClipNodeEditor::onEditLayerTransform(NodeId layerId) {
    TransformEditorDialog dialog((int)layerId, this, this);
    dialog.exec();
}

void ClipNodeEditor::onEditProcessCrop(NodeId processId) {
    auto *pr = dynamic_cast<ProcessNodeItem *>(m_itemMap.value(processId));
    if (!pr) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Edit Crop");
    auto *layout = new QVBoxLayout(&dialog);
    auto *selector = new CropSelectorWidget(&dialog);
    float x, y, w, h;
    pr->cropRect(x, y, w, h);
    // Show the upstream input node's thumbnail as reference, if any.
    const NodeId up = m_scene->producerForInputPort(pr->inPort());
    ResolvedStream sub = evaluateVideoInput(up);
    if (!sub.layers.isEmpty()) {
        if (auto *m = nodeAt(sub.layers.first().inputNodeId))
            selector->setFrame(m->thumbnail().toImage());
    }
    selector->setCrop(x, y, w, h);
    layout->addWidget(selector);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() == QDialog::Accepted) {
        pr->setCropRect(selector->cropX(), selector->cropY(),
                        selector->cropW(), selector->cropH());
        emit clipChainChanged();
    }
}

void ClipNodeEditor::onNodeRemoveRequested(NodeId nodeId) { removeNode(nodeId); }

void ClipNodeEditor::connectNodeSignals(ClipNodeModel *model, NodeId id) {
    connect(model, &ClipNodeModel::removeRequested, this, [this, id]() { onNodeRemoveRequested(id); });
}

void ClipNodeEditor::disconnectNodeSignals(ClipNodeModel *model) {
    disconnect(model, nullptr, this, nullptr);
}

// ── Serialization ───────────────────────────────────────────────────────────

static QJsonObject layerSlotToJson(const LayerSlot &s) {
    QJsonObject o;
    o["baseX"] = (double)s.baseX; o["baseY"] = (double)s.baseY;
    o["baseW"] = (double)s.baseW; o["baseH"] = (double)s.baseH;
    o["visible"] = s.visible;
    o["name"] = s.name;
    return o;
}

QJsonObject ClipNodeEditor::saveState(const QDir &sessionDir) const {
    QJsonObject root;
    root["graphVersion"] = 2;
    root["nextId"] = (qint64)m_nextId;
    root["deckAInput"] = (qint64)m_deckAInput;
    root["deckBInput"] = (qint64)m_deckBInput;

    QJsonArray inputNodes;
    for (auto it = m_nodeMap.cbegin(); it != m_nodeMap.cend(); ++it) {
        const NodeId id = it.key();
        const ClipNodeModel *model = it.value();
        QJsonObject nodeObj;
        nodeObj["id"] = (qint64)id;
        if (auto *ci = dynamic_cast<ClipNodeItem *>(m_itemMap.value(id))) {
            nodeObj["posX"] = ci->pos().x();
            nodeObj["posY"] = ci->pos().y();
            nodeObj["hasAudio"] = ci->hasAudio();
            if (ci->hasAudio()) {
                nodeObj["audioVolume"] = ci->volume();
                nodeObj["audioMuted"] = ci->muted();
                nodeObj["audioPlaybackMode"] = (int)ci->playbackMode();
                nodeObj["audioDelayMs"] = ci->delayMs();
            }
        }
        nodeObj["source"] = descriptorToJson(model->sourceDescriptor(), sessionDir);
        QJsonObject settingsObj = model->settings().toJson();
        if (sessionDir.isAbsolute()) {
            QJsonArray overlays = settingsObj["overlays"].toArray();
            for (int i = 0; i < overlays.size(); ++i) {
                QJsonObject ov = overlays.at(i).toObject();
                if (ov["type"].toString() == QLatin1String("image")) {
                    ov["content"] = AssetPathResolver::storePath(ov["content"].toString(), sessionDir);
                    overlays[i] = ov;
                }
            }
            settingsObj["overlays"] = overlays;
        }
        nodeObj["settings"] = settingsObj;
        nodeObj["repeat"] = model->isRepeat();
        inputNodes.append(nodeObj);
    }
    root["inputNodes"] = inputNodes;

    QJsonArray processNodes;
    for (auto it = m_processNodes.cbegin(); it != m_processNodes.cend(); ++it) {
        auto *pr = static_cast<ProcessNodeItem *>(it.value());
        QJsonObject o;
        o["id"] = (qint64)pr->nodeId();
        o["posX"] = pr->pos().x(); o["posY"] = pr->pos().y();
        o["effect"] = (int)pr->effect();
        float x, y, w, h; pr->cropRect(x, y, w, h);
        o["cropX"] = (double)x; o["cropY"] = (double)y;
        o["cropW"] = (double)w; o["cropH"] = (double)h;
        processNodes.append(o);
    }
    root["processNodes"] = processNodes;

    QJsonArray layerNodes;
    for (auto it = m_layerNodes.cbegin(); it != m_layerNodes.cend(); ++it) {
        auto *ly = static_cast<LayerNodeItem *>(it.value());
        QJsonObject o;
        o["id"] = (qint64)ly->nodeId();
        o["posX"] = ly->pos().x(); o["posY"] = ly->pos().y();
        o["canvasW"] = ly->canvasW(); o["canvasH"] = ly->canvasH();
        QJsonArray slotArr;
        for (int i = 0; i < ly->slotCount(); ++i)
            slotArr.append(layerSlotToJson(ly->slot(i)));
        o["slots"] = slotArr;
        layerNodes.append(o);
    }
    root["layerNodes"] = layerNodes;

    QJsonArray abSelectNodes;
    for (auto it = m_abSelectNodes.cbegin(); it != m_abSelectNodes.cend(); ++it) {
        auto *ab = static_cast<AbSelectNodeItem *>(it.value());
        QJsonObject o;
        o["id"] = (qint64)ab->nodeId();
        o["posX"] = ab->pos().x(); o["posY"] = ab->pos().y();
        QJsonArray slotArr;
        for (int i = 0; i < ab->slotCount(); ++i) {
            QJsonObject so; so["name"] = ab->slot(i).name; slotArr.append(so);
        }
        o["slots"] = slotArr;
        abSelectNodes.append(o);
    }
    root["abSelectNodes"] = abSelectNodes;

    if (m_outputNode != 0) {
        if (auto *out = m_itemMap.value(m_outputNode)) {
            QJsonObject o;
            o["id"] = (qint64)m_outputNode;
            o["posX"] = out->pos().x(); o["posY"] = out->pos().y();
            root["outputNode"] = o;
        }
    }

    QJsonArray scriptNodes;
    for (auto it = m_scriptNodes.cbegin(); it != m_scriptNodes.cend(); ++it) {
        auto *sn = static_cast<ScriptNodeItem *>(it.value());
        QJsonObject obj;
        obj["id"] = (qint64)sn->nodeId();
        obj["posX"] = sn->pos().x(); obj["posY"] = sn->pos().y();
        obj["luaCode"] = sn->code();
        obj["triggerMode"] = static_cast<int>(sn->triggerMode());
        obj["intervalMs"] = sn->intervalMs();
        scriptNodes.append(obj);
    }
    root["scriptNodes"] = scriptNodes;

    QJsonArray masterAudioNodes;
    for (auto it = m_masterAudioNodes.cbegin(); it != m_masterAudioNodes.cend(); ++it) {
        auto *mn = static_cast<MasterAudioOutputNodeItem *>(it.value());
        QJsonObject obj;
        obj["id"] = (qint64)mn->nodeId();
        obj["posX"] = mn->pos().x(); obj["posY"] = mn->pos().y();
        obj["deviceId"] = mn->deviceId();
        obj["deviceLabel"] = mn->deviceLabel();
        masterAudioNodes.append(obj);
    }
    root["masterAudioNodes"] = masterAudioNodes;

    QJsonArray masterAudioInputNodes;
    for (auto it = m_masterAudioInputNodes.cbegin(); it != m_masterAudioInputNodes.cend(); ++it) {
        auto *in = static_cast<MasterAudioInputNodeItem *>(it.value());
        QJsonObject obj;
        obj["id"] = (qint64)in->nodeId();
        obj["posX"] = in->pos().x(); obj["posY"] = in->pos().y();
        obj["deviceId"] = in->deviceId();
        obj["deviceLabel"] = in->deviceLabel();
        obj["volume"] = in->volume();
        obj["muted"] = in->muted();
        masterAudioInputNodes.append(obj);
    }
    root["masterAudioInputNodes"] = masterAudioInputNodes;

    root["connections"] = m_scene->edgesToJson();
    return root;
}

PortItem *ClipNodeEditor::findPort(NodeId nodeId, int portKindInt, int slotIndex) const {
    const PortKind kind = (PortKind)portKindInt;
    auto *base = m_itemMap.value(nodeId);
    if (!base) return nullptr;

    if (auto *ci = dynamic_cast<ClipNodeItem *>(base)) {
        if (kind == PortKind::ChainOut)           return ci->chainOutPort();
        if (kind == PortKind::AudioControllerOut) return ci->audioPort();
        if (kind == PortKind::ShaderAudioIn)      return ci->shaderAudioInPort();
        if (kind == PortKind::DataIn)             return ci->dataInPort();
    }
    if (auto *pr = dynamic_cast<ProcessNodeItem *>(base)) {
        if (kind == PortKind::ChainIn)  return pr->inPort();
        if (kind == PortKind::ChainOut) return pr->outPort();
    }
    if (auto *ly = dynamic_cast<LayerNodeItem *>(base)) {
        if (kind == PortKind::ChainOut) return ly->outPort();
        if (kind == PortKind::ChainIn)  return ly->inPort(slotIndex);
    }
    if (auto *ab = dynamic_cast<AbSelectNodeItem *>(base)) {
        if (kind == PortKind::AbOut)   return ab->abOutPort();
        if (kind == PortKind::ChainIn) return ab->inPort(slotIndex);
    }
    if (auto *out = dynamic_cast<OutputNodeItem *>(base)) {
        if (kind == PortKind::ChainIn) return out->chainInPort();
        if (kind == PortKind::AbIn)    return out->abInPort();
    }
    if (auto *mo = dynamic_cast<MasterAudioOutputNodeItem *>(base)) {
        if (kind == PortKind::MasterAudioIn) return mo->audioInPort();
    }
    if (auto *mi = dynamic_cast<MasterAudioInputNodeItem *>(base)) {
        if (kind == PortKind::MasterAudioInputOut) return mi->audioOutPort();
    }
    if (auto *si = dynamic_cast<ScriptNodeItem *>(base)) {
        if (kind == PortKind::ScriptOut) return si->scriptOutPort();
    }
    return nullptr;
}

void ClipNodeEditor::restoreConnections(ClipNodeScene *scene, const QJsonArray &conns) {
    for (const auto &val : conns) {
        const QJsonObject obj = val.toObject();
        const NodeId from = (NodeId)obj["from"].toInteger();
        const NodeId to   = (NodeId)obj["to"].toInteger();
        const int    kind = obj["kind"].toInt();
        const int    slot = obj["toPortIndex"].toInt(-1);

        PortItem *fromPort = nullptr, *toPort = nullptr;
        if (kind == ConnectionItem::Chain) {
            fromPort = findPort(from, (int)PortKind::ChainOut);
            toPort   = findPort(to,   (int)PortKind::ChainIn, slot);
        } else if (kind == ConnectionItem::AbToOutput) {
            fromPort = findPort(from, (int)PortKind::AbOut);
            toPort   = findPort(to,   (int)PortKind::AbIn);
        } else if (kind == ConnectionItem::ClipToShaderAudio) {
            fromPort = findPort(from, (int)PortKind::AudioControllerOut);
            toPort   = findPort(to,   (int)PortKind::ShaderAudioIn);
        } else if (kind == ConnectionItem::ControllerToMaster) {
            fromPort = findPort(from, (int)PortKind::AudioControllerOut);
            toPort   = findPort(to,   (int)PortKind::MasterAudioIn);
        } else if (kind == ConnectionItem::InputToMaster) {
            fromPort = findPort(from, (int)PortKind::MasterAudioInputOut);
            toPort   = findPort(to,   (int)PortKind::MasterAudioIn);
        } else if (kind == ConnectionItem::ScriptToData) {
            fromPort = findPort(from, (int)PortKind::ScriptOut);
            toPort   = findPort(to,   (int)PortKind::DataIn);
        }
        if (fromPort && toPort)
            scene->createConnectionManually(fromPort, toPort, (ConnectionItem::EdgeKind)kind);
    }
}

void ClipNodeEditor::restoreState(const QJsonObject &state) {
    m_restoring = true;
    clearAllNodes();

    // Clean break: only graphVersion >= 2 graphs are loadable.
    if (state.value("graphVersion").toInt(0) < 2) {
        m_nextId = 1;
        ensureOutputNode();
        // Leave the graph empty but with an Output node present.
        if (m_nodeMap.isEmpty() && m_processNodes.isEmpty() && m_layerNodes.isEmpty()
            && m_abSelectNodes.isEmpty()) {
            // remove auto Output so a fresh empty graph has nothing
            if (m_outputNode) { deleteNodeById(m_outputNode); }
        }
        m_restoring = false;
        emit clipChainChanged();
        emit audioGraphChanged();
        return;
    }

    for (const auto &val : state["inputNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const NodeId clipId = (NodeId)obj["id"].toInteger();
        m_nextId = clipId;
        const NodeId id = m_nextId++;

        const SourceDescriptor desc = descriptorFromJson(obj["source"].toObject());
        const bool hasAudio = obj["hasAudio"].toBool(false);
        const bool hasShaderAudioIn = (desc.kind == SourceDescriptor::Kind::Shader);
        const bool hasDataIn = (desc.kind == SourceDescriptor::Kind::Text);

        auto *model = new ClipNodeModel(this);
        auto *nodeItem = new ClipNodeItem(model, id, hasAudio, hasShaderAudioIn, hasDataIn,
                                          obj["audioVolume"].toInt(100),
                                          obj["audioMuted"].toBool(false),
                                          (AudioPlaybackMode)obj["audioPlaybackMode"].toInt((int)AudioPlaybackMode::Always),
                                          obj["audioDelayMs"].toInt(0));
        nodeItem->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        m_scene->addItem(nodeItem);
        m_nodeMap[id] = model;
        registerItem(nodeItem);
        wireDeleteCallback(nodeItem, [this](NodeId nid) { deleteNodeById(nid); });
        wireEditAudioCallback(nodeItem, [this](NodeId cid) { onEditClipAudio(cid); });

        using Kind = SourceDescriptor::Kind;
        model->setNodeId(id);
        if (desc.kind == Kind::VideoFile || desc.kind == Kind::Image)
            model->loadClip(desc.path, QPixmap{});
        else
            model->loadSource(desc, QPixmap{});
        if (obj.contains("settings"))
            model->applySettings(ClipSettings::fromJson(obj["settings"].toObject()));
        if (obj["repeat"].toBool()) model->setRepeat(true);
        connectNodeSignals(model, id);
        emit nodeAdded(id);
    }

    for (const auto &val : state["processNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const NodeId pid = (NodeId)obj["id"].toInteger();
        m_nextId = pid;
        auto *pr = new ProcessNodeItem(m_nextId++, (ProcessNodeItem::Effect)obj["effect"].toInt());
        pr->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        pr->setCropRect((float)obj["cropX"].toDouble(0.0), (float)obj["cropY"].toDouble(0.0),
                        (float)obj["cropW"].toDouble(1.0), (float)obj["cropH"].toDouble(1.0));
        pr->onEditRequested = [this](NodeId nid) { onEditProcessCrop(nid); };
        m_scene->addItem(pr);
        m_processNodes[pid] = static_cast<void *>(pr);
        registerItem(pr);
        wireDeleteCallback(pr, [this](NodeId nid) { deleteNodeById(nid); });
    }

    for (const auto &val : state["layerNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const NodeId lid = (NodeId)obj["id"].toInteger();
        m_nextId = lid;
        auto *ly = new LayerNodeItem(m_nextId++, 0);
        ly->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        ly->setCanvasSize(obj["canvasW"].toInt(1280), obj["canvasH"].toInt(720));
        QVector<LayerSlot> slotVec;
        for (const auto &sv : obj["slots"].toArray()) {
            const QJsonObject so = sv.toObject();
            LayerSlot s;
            s.baseX = (float)so["baseX"].toDouble(0.0);
            s.baseY = (float)so["baseY"].toDouble(0.0);
            s.baseW = (float)so["baseW"].toDouble(1.0);
            s.baseH = (float)so["baseH"].toDouble(1.0);
            s.visible = so["visible"].toBool(true);
            s.name = so["name"].toString();
            slotVec.push_back(s);
        }
        if (slotVec.isEmpty()) slotVec.push_back(LayerSlot{});
        ly->setSlots(slotVec);
        ly->onEditRequested = [this](NodeId nid) { onEditLayerTransform(nid); };
        ly->onEditCanvasRequested = [this](NodeId nid) { onEditLayerCanvas(nid); };
        ly->onChanged = [this]() { emit clipChainChanged(); };
        m_scene->addItem(ly);
        m_layerNodes[lid] = static_cast<void *>(ly);
        registerItem(ly);
        wireDeleteCallback(ly, [this](NodeId nid) { deleteNodeById(nid); });
    }

    for (const auto &val : state["abSelectNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const NodeId aid = (NodeId)obj["id"].toInteger();
        m_nextId = aid;
        auto *ab = new AbSelectNodeItem(m_nextId++, 0);
        ab->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        QVector<AbSlot> slotVec;
        for (const auto &sv : obj["slots"].toArray()) {
            AbSlot s; s.name = sv.toObject()["name"].toString(); slotVec.push_back(s);
        }
        if (slotVec.isEmpty()) slotVec.push_back(AbSlot{});
        ab->setSlots(slotVec);
        ab->onChanged = [this]() { emit clipChainChanged(); };
        ab->onAssignDeck = [this](NodeId abId, int slot, bool deckA) {
            auto *node = static_cast<AbSelectNodeItem *>(m_abSelectNodes.value(abId));
            if (!node) return;
            const NodeId prod = m_scene->producerForInputPort(node->inPort(slot));
            if (prod == 0) return;
            assignInputToDeck(prod, deckA);
        };
        m_scene->addItem(ab);
        m_abSelectNodes[aid] = static_cast<void *>(ab);
        registerItem(ab);
        wireDeleteCallback(ab, [this](NodeId nid) { deleteNodeById(nid); });
    }

    if (state.contains("outputNode")) {
        const QJsonObject obj = state["outputNode"].toObject();
        const NodeId oid = (NodeId)obj["id"].toInteger();
        m_nextId = oid;
        auto *out = new OutputNodeItem(m_nextId++);
        out->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        m_scene->addItem(out);
        m_outputNode = oid;
        registerItem(out);
    }

    for (const auto &val : state["scriptNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const NodeId scriptId = (NodeId)obj["id"].toInteger();
        m_nextId = scriptId;
#ifdef PRISM_HAVE_LUA
        auto *sn = new ScriptNodeItem(m_nextId++, obj["luaCode"].toString(),
            static_cast<ScriptTriggerMode>(obj["triggerMode"].toInt(0)),
            obj["intervalMs"].toInt(1000));
        sn->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        sn->onEditRequested = [this](NodeId sid) { onEditScriptNode(sid); };
        m_scene->addItem(sn);
        m_scriptNodes[scriptId] = static_cast<void *>(sn);
        registerItem(sn);
        wireDeleteCallback(sn, [this](NodeId nid) { deleteNodeById(nid); });
#else
        ++m_nextId;
#endif
    }

    for (const auto &val : state["masterAudioNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const NodeId masterId = (NodeId)obj["id"].toInteger();
        m_nextId = masterId;
        auto *mn = new MasterAudioOutputNodeItem(m_nextId++);
        mn->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        mn->setDevice(obj["deviceId"].toString(), obj["deviceLabel"].toString());
        mn->onDeviceChanged = [this](NodeId) { emit audioGraphChanged(); };
        m_scene->addItem(mn);
        m_masterAudioNodes[masterId] = static_cast<void *>(mn);
        registerItem(mn);
        wireDeleteCallback(mn, [this](NodeId nid) { deleteNodeById(nid); });
    }

    for (const auto &val : state["masterAudioInputNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const NodeId inputId = (NodeId)obj["id"].toInteger();
        m_nextId = inputId;
        auto *in = new MasterAudioInputNodeItem(m_nextId++, obj["volume"].toInt(100),
                                                obj["muted"].toBool(false));
        in->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        in->setDevice(obj["deviceId"].toString(), obj["deviceLabel"].toString());
        in->onSettingsChanged = [this](NodeId) { emit audioGraphChanged(); };
        m_scene->addItem(in);
        m_masterAudioInputNodes[inputId] = static_cast<void *>(in);
        registerItem(in);
        wireDeleteCallback(in, [this](NodeId nid) { deleteNodeById(nid); });
    }

    restoreConnections(m_scene, state["connections"].toArray());

    m_restoring = false;
    normalizeSwitchingInputs();   // append the trailing empty input to each switching node

    if (m_outputNode == 0
        && !(m_nodeMap.isEmpty() && m_processNodes.isEmpty()
             && m_layerNodes.isEmpty() && m_abSelectNodes.isEmpty()
             && m_scriptNodes.isEmpty() && m_masterAudioNodes.isEmpty()
             && m_masterAudioInputNodes.isEmpty()))
        ensureOutputNode();

    const qint64 savedNext = state["nextId"].toInteger(0);
    if (savedNext > (qint64)m_nextId)
        m_nextId = (NodeId)savedNext;

    m_deckAInput = (NodeId)state["deckAInput"].toInteger(0);
    m_deckBInput = (NodeId)state["deckBInput"].toInteger(0);
    updateAbHighlights();

    emit deckAClipChanged(m_deckAInput);
    emit deckBClipChanged(m_deckBInput);
    emit clipChainChanged();
    emit audioGraphChanged();
}

