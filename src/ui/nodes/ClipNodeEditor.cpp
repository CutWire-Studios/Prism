#include "ui/nodes/ClipNodeEditor.h"
#include "ui/common/MaterialSymbols.h"
#include "core/project/AssetPathResolver.h"
#include "core/scripting/ScriptRuntime.h"
#include "ui/editors/ScriptEditDialog.h"
#include "ui/canvas/TransformEditorDialog.h"
#include "ui/canvas/GroupEditorDialog.h"
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
class TransformContextNodeItem;
class ScriptNodeItem;
class MasterAudioOutputNodeItem;
class MasterAudioInputNodeItem;
class GroupNodeItem;
class ClipNodeScene;
class ClipNodeView;

enum class PortKind {
    ChainIn,
    ChainOut,
    TransformContext,
    ContextHub,
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
    case PortKind::ChainOut:           return QColor(0x50, 0xd0, 0x90);
    case PortKind::TransformContext:   return QColor(0xd0, 0xb0, 0x50);
    case PortKind::ContextHub:         return QColor(0xd0, 0xb0, 0x50);
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
    if ((a == PortKind::TransformContext && b == PortKind::ContextHub) ||
        (a == PortKind::ContextHub && b == PortKind::TransformContext)) return true;
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
    if (kind == PortKind::ContextHub) return false;
    if (kind == PortKind::AudioControllerOut) return false;
    if (kind == PortKind::MasterAudioIn) return false;
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
                    ClipToShaderAudio = 5, ScriptToData = 6, InputToMaster = 7 };

    ConnectionItem(PortItem *from, PortItem *to, EdgeKind kind)
        : QGraphicsPathItem(nullptr), m_from(from), m_to(to), m_kind(kind)
    {
        setZValue(-1);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        QColor color;
        if (kind == Chain) color = QColor(0x70, 0xb8, 0xff);
        else if (kind == TransformToContext) color = QColor(0xd0, 0xb0, 0x70);
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

        m_proxy = new QGraphicsProxyWidget(this);
        m_proxy->setWidget(card);
        m_proxy->setPos(0, PROXY_Y);

        m_chainInPort = new PortItem(PortKind::ChainIn, this);
        m_chainInPort->setPos(PORT_X, IN_PORT_Y);

        m_chainOutPort = new PortItem(PortKind::ChainOut, this);
        m_contextPort = new PortItem(PortKind::TransformContext, this);

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
    PortItem *chainInPort()  const { return m_chainInPort; }
    PortItem *chainOutPort() const { return m_chainOutPort; }
    PortItem *contextPort() const { return m_contextPort; }
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

    float x() const { return m_x; }
    float y() const { return m_y; }
    float w() const { return m_w; }
    float h() const { return m_h; }

    void setTransform(float x, float y, float w, float h) {
        m_x = x; m_y = y; m_w = w; m_h = h;
        if (m_model) m_model->setTransform(x, y, w, h);
        update();
    }

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

        p->setPen(QColor(180, 160, 100));
        p->setFont(QFont("Monospace", 7));
        p->drawText(QRectF(4, 2, NODE_W - 8, HEADER_H),
                    Qt::AlignCenter,
                    QString("%1%,%2% %3%×%4%")
                        .arg((int)(m_x * 100)).arg((int)(m_y * 100))
                        .arg((int)(m_w * 100)).arg((int)(m_h * 100)));

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
        if (m_model && m_model->isGroupMember()) {
            menu.addAction("Rename", [this]() {
                if (onRenameRequested) onRenameRequested(m_nodeId);
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
    float m_x = 0.f, m_y = 0.f, m_w = 1.f, m_h = 1.f;
    QGraphicsProxyWidget *m_proxy;
    PortItem *m_chainInPort;
    PortItem *m_chainOutPort;
    PortItem *m_contextPort;
    PortItem *m_audioPort = nullptr;
    PortItem *m_shaderAudioInPort = nullptr;
    PortItem *m_dataInPort = nullptr;
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
    std::function<void(NodeId)> onDeleteRequested;

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

        if (isSelected()) {
            p->setPen(QPen(kSelectionAccent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(QRectF(0, 0, SMALL_NODE_W, SMALL_NODE_H).adjusted(1, 1, -1, -1), 4, 4);
        }
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
    int m_canvasW, m_canvasH;
    PortItem *m_hubPort;
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

class GroupNodeItem : public NodeItemBase {
public:
    GroupNodeItem(NodeId id, ClipNodeScene *subScene)
        : m_nodeId(id), m_subScene(subScene)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsSelectable);
        setZValue(0);
    }

    std::function<void(NodeId, bool)> onDeckRequested;
    std::function<void(NodeId)> onEditRequested;
    std::function<void(NodeId)> onUngroupRequested;
    std::function<void(NodeId)> onDeleteRequested;
    std::function<void(NodeId)> onRenameRequested;

    NodeId nodeId() const override { return m_nodeId; }
    ClipNodeScene *subScene() const { return m_subScene; }
    NodeId delegateId() const { return m_delegateId; }
    QString name() const { return m_name; }

    void setName(const QString &name) {
        m_name = name.trimmed().isEmpty() ? QStringLiteral("Group") : name.trimmed();
        update();
    }

    void setDelegateId(NodeId id) {
        m_delegateId = id;
        update();
    }

    bool containsMember(NodeId memberId) const;
    QVector<NodeId> memberIds() const;

    QRectF boundingRect() const override { return QRectF(0, 0, NODE_W, GROUP_NODE_H); }

    QRectF aButtonRect() const { return QRectF(4, PROXY_Y + 8, 36, 22); }
    QRectF bButtonRect() const { return QRectF(44, PROXY_Y + 8, 36, 22); }
    QRectF editButtonRect() const { return QRectF(84, PROXY_Y + 8, 34, 22); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF bounds(0, 0, NODE_W, GROUP_NODE_H);
        p->setPen(QPen(QColor(55, 56, 62), 1));
        p->setBrush(QColor(28, 32, 40));
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
        p->setBrush(QColor(42, 48, 58));
        p->drawPath(hdr);

        p->setPen(QColor(160, 180, 210));
        p->setFont(QFont("Monospace", 8, QFont::Bold));
        const QFontMetrics fm(p->font());
        p->drawText(QRectF(4, 2, NODE_W - 8, HEADER_H), Qt::AlignCenter,
                    fm.elidedText(m_name, Qt::ElideRight, NODE_W - 8));

        auto drawBtn = [&](const QRectF &r, const QString &label, bool active) {
            p->setPen(QPen(active ? QColor(0x2a, 0x8f, 0xa0) : QColor(80, 85, 95), 1));
            p->setBrush(active ? QColor(0x2a, 0x5c, 0x66) : QColor(36, 38, 44));
            p->drawRoundedRect(r, 3, 3);
            p->setPen(active ? Qt::white : QColor(180, 185, 195));
            p->setFont(QFont("Monospace", 8));
            p->drawText(r, Qt::AlignCenter, label);
        };
        drawBtn(aButtonRect(), "A", m_aActive);
        drawBtn(bButtonRect(), "B", m_bActive);
        drawBtn(editButtonRect(), "Edit", false);

        p->setPen(QColor(120, 130, 150));
        p->setFont(QFont("Monospace", 7));
        p->drawText(QRectF(4, PROXY_Y + 36, NODE_W - 8, 40), Qt::AlignCenter,
                    m_delegateId ? QString("Out: clip %1").arg(m_delegateId)
                                 : QString("No output"));

        if (isSelected()) {
            p->setPen(QPen(kSelectionAccent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 6, 6);
        }
    }

    void setDeckActive(bool deckA, bool active) {
        if (deckA) m_aActive = active;
        else       m_bActive = active;
        update();
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        if (aButtonRect().contains(e->pos())) {
            if (onDeckRequested && m_delegateId) {
                setDeckActive(true, true);
                setDeckActive(false, false);
                onDeckRequested(m_delegateId, true);
            }
            e->accept();
            return;
        }
        if (bButtonRect().contains(e->pos())) {
            if (onDeckRequested && m_delegateId) {
                setDeckActive(false, true);
                setDeckActive(true, false);
                onDeckRequested(m_delegateId, false);
            }
            e->accept();
            return;
        }
        if (editButtonRect().contains(e->pos())) {
            if (onEditRequested) onEditRequested(m_nodeId);
            e->accept();
            return;
        }
        QGraphicsItem::mousePressEvent(e);
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override {
        QMenu menu;
        menu.addAction("Rename", [this]() {
            if (onRenameRequested) onRenameRequested(m_nodeId);
        });
        menu.addAction("Edit Group", [this]() {
            if (onEditRequested) onEditRequested(m_nodeId);
        });
        menu.addAction("Ungroup", [this]() {
            if (onUngroupRequested) onUngroupRequested(m_nodeId);
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
    static constexpr qreal GROUP_NODE_H = PROXY_Y + 90.0;

    NodeId m_nodeId;
    NodeId m_delegateId = 0;
    ClipNodeScene *m_subScene;
    QString m_name = QStringLiteral("Group");
    bool m_aActive = false;
    bool m_bActive = false;
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
        } else if ((src->kind() == PortKind::ScriptOut && dst->kind() == PortKind::DataIn) ||
                   (src->kind() == PortKind::DataIn && dst->kind() == PortKind::ScriptOut)) {
            outPort = (src->kind() == PortKind::ScriptOut) ? src : dst;
            inPort  = (src->kind() == PortKind::DataIn)    ? src : dst;
            kind = ConnectionItem::ScriptToData;
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

    NodeId contextForClip(NodeId clipId) const {
        for (const auto &e : m_edges)
            if (e.edgeKind == ConnectionItem::TransformToContext && e.fromNodeId == clipId)
                return e.toNodeId;
        return 0;
    }

    QVector<NodeId> clipsForContext(NodeId contextId) const {
        QVector<NodeId> result;
        for (const auto &e : m_edges) {
            if (e.edgeKind == ConnectionItem::TransformToContext && e.toNodeId == contextId)
                result.append(e.fromNodeId);
        }
        return result;
    }

    bool chainCrossesSelectionBoundary(const QSet<NodeId> &members) const {
        for (const auto &e : m_edges) {
            if (e.edgeKind != ConnectionItem::Chain) continue;
            const bool fromIn = members.contains(e.fromNodeId);
            const bool toIn = members.contains(e.toNodeId);
            if (fromIn != toIn) return true;
        }
        return false;
    }

    QVector<ConnectionItem::EdgeKind> edgeKindsBetween(NodeId a, NodeId b) const {
        QVector<ConnectionItem::EdgeKind> kinds;
        for (const auto &e : m_edges) {
            if ((e.fromNodeId == a && e.toNodeId == b) ||
                (e.fromNodeId == b && e.toNodeId == a))
                kinds.append(e.edgeKind);
        }
        return kinds;
    }

    struct StoredEdge {
        NodeId from;
        NodeId to;
        ConnectionItem::EdgeKind kind;
        PortItem *fromPort;
        PortItem *toPort;
        ConnectionItem *item;
    };

    QVector<StoredEdge> internalEdges(const QSet<NodeId> &members) const {
        QVector<StoredEdge> result;
        for (const auto &e : m_edges) {
            if (members.contains(e.fromNodeId) && members.contains(e.toNodeId)) {
                result.append({ e.fromNodeId, e.toNodeId, e.edgeKind,
                                e.item->fromPort(), e.item->toPort(), e.item });
            }
        }
        return result;
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

    QJsonArray edgesToJson(const QSet<NodeId> *excludeBothIn = nullptr) const {
        QJsonArray arr;
        for (const auto &e : m_edges) {
            if (excludeBothIn && excludeBothIn->contains(e.fromNodeId)
                && excludeBothIn->contains(e.toNodeId))
                continue;
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

void ConnectionItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *e) {
    QMenu menu;
    menu.addAction("Delete connection", [this]() {
        if (auto *s = static_cast<ClipNodeScene *>(scene()))
            s->removeConnection(this);
    });
    menu.exec(e->screenPos());
    e->accept();
}

bool GroupNodeItem::containsMember(NodeId memberId) const {
    if (!m_subScene) return false;
    for (auto *item : m_subScene->items()) {
        if (auto *node = dynamic_cast<NodeItemBase *>(item)) {
            if (node->nodeId() == memberId) return true;
        }
    }
    return false;
}

QVector<NodeId> GroupNodeItem::memberIds() const {
    QVector<NodeId> ids;
    if (!m_subScene) return ids;
    for (auto *item : m_subScene->items()) {
        if (auto *node = dynamic_cast<NodeItemBase *>(item))
            ids.append(node->nodeId());
    }
    return ids;
}

void ClipNodeItem::updateLayout() {
    prepareGeometryChange();
    const qreal midY = nodeHeight() / 2.0;
    const qreal chainOutY = PROXY_Y + bodyHeight() + audioStripHeight() + PORT_R;
    m_chainOutPort->setPos(PORT_X, chainOutY);
    m_contextPort->setPos(NODE_W, midY);
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

QVariant TransformContextNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
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

QVariant GroupNodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
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

ClipNodeScene *ClipNodeEditor::subSceneForGroup(NodeId groupId) const {
    if (auto *group = m_groupNodes.value(groupId))
        return group->subScene();
    return nullptr;
}

QString ClipNodeEditor::groupName(NodeId groupId) const {
    if (auto *group = m_groupNodes.value(groupId))
        return group->name();
    return QStringLiteral("Group");
}

void ClipNodeEditor::renameGroup(NodeId groupId) {
    auto *group = m_groupNodes.value(groupId);
    if (!group) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Group"), tr("Name:"),
                                               QLineEdit::Normal, group->name(), &ok);
    if (!ok) return;

    group->setName(name);
}

void ClipNodeEditor::renameGroupMemberClip(NodeId clipId) {
    ClipNodeModel *model = nodeAt(clipId);
    if (!model || !model->isGroupMember()) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Clip"), tr("Name:"),
                                               QLineEdit::Normal, model->sourceName(), &ok);
    if (!ok) return;

    model->setDisplayName(name.trimmed());
}

QWidget *ClipNodeEditor::makeSubSceneView(ClipNodeScene *scene, QWidget *parent, NodeId groupId) {
    auto *view = new ClipNodeView(scene);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->onDeleteSelection = [this, view]() { deleteSelection(view); };
    if (groupId != 0) {
        connect(view, &QGraphicsView::customContextMenuRequested, this,
                [this, groupId, view](const QPoint &) {
                    showGroupSceneContextMenu(groupId, view);
                });
    }
    return new ClipNodeCanvasWidget(view, parent);
}

static void wireDeleteCallback(NodeItemBase *item, const std::function<void(NodeId)> &cb) {
    if (auto *clip = dynamic_cast<ClipNodeItem *>(item)) clip->onDeleteRequested = cb;
    else if (auto *ctx = dynamic_cast<TransformContextNodeItem *>(item)) ctx->onDeleteRequested = cb;
    else if (auto *scr = dynamic_cast<ScriptNodeItem *>(item)) scr->onDeleteRequested = cb;
    else if (auto *mas = dynamic_cast<MasterAudioOutputNodeItem *>(item)) mas->onDeleteRequested = cb;
    else if (auto *mai = dynamic_cast<MasterAudioInputNodeItem *>(item)) mai->onDeleteRequested = cb;
    else if (auto *grp = dynamic_cast<GroupNodeItem *>(item)) grp->onDeleteRequested = cb;
}

static void wireEditAudioCallback(ClipNodeItem *item, const std::function<void(NodeId)> &cb) {
    if (item) item->onEditAudioRequested = cb;
}

static void wireRenameCallback(NodeItemBase *item, const std::function<void(NodeId)> &cb) {
    if (auto *clip = dynamic_cast<ClipNodeItem *>(item)) clip->onRenameRequested = cb;
    else if (auto *grp = dynamic_cast<GroupNodeItem *>(item)) grp->onRenameRequested = cb;
}

ClipNodeModel *ClipNodeEditor::addClipNode(const QString &path, const QPixmap &thumbnail,
                                           ClipNodeScene *targetScene,
                                           QGraphicsView *viewForPos,
                                           bool groupMember) {
    ClipNodeScene *scene = targetScene ? targetScene : m_scene;
    QGraphicsView *view = viewForPos ? viewForPos : m_view;

    const bool hasAudio = VideoPlayer::fileHasAudio(path);

    const NodeId id = m_nextId++;
    auto *model = new ClipNodeModel(this);
    auto *nodeItem = new ClipNodeItem(model, id, hasAudio);

    if (viewForPos)
        nodeItem->setPos(scenePosForView(view, QCursor::pos()));
    else if (targetScene) {
        const int idx = m_nodeMap.size();
        nodeItem->setPos(idx * 160.0 + 20.0, idx * 60.0 + 20.0);
    } else {
        const int idx = m_nodeMap.size();
        nodeItem->setPos(idx * 160.0 + 20.0, idx * 60.0 + 20.0);
    }

    scene->addItem(nodeItem);
    m_nodeMap[id] = model;
    registerItem(nodeItem);
    wireDeleteCallback(nodeItem, [this](NodeId nid) { deleteNodeById(nid); });
    wireRenameCallback(nodeItem, [this](NodeId nid) { renameGroupMemberClip(nid); });
    wireEditAudioCallback(nodeItem, [this](NodeId cid) { onEditClipAudio(cid); });

    model->setNodeId(id);
    model->loadClip(path, thumbnail);
    if (groupMember)
        model->setCardMode(ClipCard::CardMode::GroupMember);
    connectNodeSignals(model, id);

    emit nodeAdded(id);
    return model;
}

ClipNodeModel *ClipNodeEditor::addSourceNode(const SourceDescriptor &desc, const QPixmap &thumbnail,
                                             ClipNodeScene *targetScene,
                                             QGraphicsView *viewForPos,
                                             bool groupMember) {
    ClipNodeScene *scene = targetScene ? targetScene : m_scene;
    QGraphicsView *view = viewForPos ? viewForPos : m_view;

    const NodeId id = m_nextId++;
    auto *model = new ClipNodeModel(this);
    const bool hasShaderAudioIn = (desc.kind == SourceDescriptor::Kind::Shader);
    const bool hasDataIn = (desc.kind == SourceDescriptor::Kind::Text);
    auto *nodeItem = new ClipNodeItem(model, id, false, hasShaderAudioIn, hasDataIn);

    if (viewForPos)
        nodeItem->setPos(scenePosForView(view, QCursor::pos()));
    else if (targetScene) {
        const int idx = m_nodeMap.size();
        nodeItem->setPos(idx * 160.0 + 20.0, idx * 60.0 + 20.0);
    } else {
        const int idx = m_nodeMap.size();
        nodeItem->setPos(idx * 160.0 + 20.0, idx * 60.0 + 20.0);
    }

    scene->addItem(nodeItem);
    m_nodeMap[id] = model;
    registerItem(nodeItem);
    wireDeleteCallback(nodeItem, [this](NodeId nid) { deleteNodeById(nid); });
    wireRenameCallback(nodeItem, [this](NodeId nid) { renameGroupMemberClip(nid); });

    model->setNodeId(id);
    model->loadSource(desc, thumbnail);
    if (groupMember)
        model->setCardMode(ClipCard::CardMode::GroupMember);
    connectNodeSignals(model, id);

    emit nodeAdded(id);
    return model;
}

void ClipNodeEditor::deleteNodeById(NodeId nodeId) {
    if (m_groupNodes.contains(nodeId)) {
        auto *group = m_groupNodes.take(nodeId);
        const QVector<NodeId> members = group->memberIds();
        for (NodeId memberId : members) {
            if (m_itemMap.contains(memberId) || m_nodeMap.contains(memberId))
                deleteNodeById(memberId);
        }
        m_itemMap.remove(nodeId);
        if (group->scene())
            static_cast<ClipNodeScene *>(group->scene())->removeConnectionsForNode(nodeId);
        removeSceneItem(group);
        emit clipChainChanged();
        emit audioGraphChanged();
        return;
    }

    if (m_nodeMap.contains(nodeId)) {
        ClipNodeScene *scene = sceneForNode(nodeId);

        ClipNodeModel *model = m_nodeMap.take(nodeId);
        m_itemMap.remove(nodeId);
        scene->removeConnectionsForNode(nodeId);

        if (m_activeClipA == nodeId) {
            m_activeClipA = 0;
            emit deckAClipChanged(0);
        }
        if (m_activeClipB == nodeId) {
            m_activeClipB = 0;
            emit deckBClipChanged(0);
        }

        ClipNodeItem *clipItem = nullptr;
        for (auto *sceneItem : scene->items()) {
            if (auto *ci = dynamic_cast<ClipNodeItem *>(sceneItem)) {
                if (ci->nodeId() == nodeId) {
                    clipItem = ci;
                    break;
                }
            }
        }

        disconnectNodeSignals(model);
        model->clearCard();
        removeSceneItem(clipItem);
        delete model;

        emit nodeRemoved(nodeId);
        emit clipChainChanged();
        emit audioGraphChanged();
        return;
    }

    ClipNodeScene *scene = sceneForNode(nodeId);

    if (m_contextNodes.contains(nodeId)) {
        m_contextNodes.remove(nodeId);
        m_itemMap.remove(nodeId);
        scene->removeConnectionsForNode(nodeId);
        TransformContextNodeItem *item = nullptr;
        for (auto *sceneItem : scene->items()) {
            if (auto *ci = dynamic_cast<TransformContextNodeItem *>(sceneItem)) {
                if (ci->nodeId() == nodeId) { item = ci; break; }
            }
        }
        removeSceneItem(item);
        emit clipChainChanged();
        return;
    }

    if (m_masterAudioNodes.contains(nodeId)) {
        m_masterAudioNodes.remove(nodeId);
        m_itemMap.remove(nodeId);
        scene->removeConnectionsForNode(nodeId);
        MasterAudioOutputNodeItem *item = nullptr;
        for (auto *sceneItem : scene->items()) {
            if (auto *mi = dynamic_cast<MasterAudioOutputNodeItem *>(sceneItem)) {
                if (mi->nodeId() == nodeId) { item = mi; break; }
            }
        }
        removeSceneItem(item);
        emit audioGraphChanged();
        return;
    }

    if (m_masterAudioInputNodes.contains(nodeId)) {
        m_masterAudioInputNodes.remove(nodeId);
        m_itemMap.remove(nodeId);
        scene->removeConnectionsForNode(nodeId);
        MasterAudioInputNodeItem *item = nullptr;
        for (auto *sceneItem : scene->items()) {
            if (auto *mi = dynamic_cast<MasterAudioInputNodeItem *>(sceneItem)) {
                if (mi->nodeId() == nodeId) { item = mi; break; }
            }
        }
        removeSceneItem(item);
        emit audioGraphChanged();
        return;
    }

    if (m_scriptNodes.contains(nodeId)) {
        m_scriptNodes.remove(nodeId);
        m_itemMap.remove(nodeId);
        scene->removeConnectionsForNode(nodeId);
        ScriptNodeItem *item = nullptr;
        for (auto *sceneItem : scene->items()) {
            if (auto *si = dynamic_cast<ScriptNodeItem *>(sceneItem)) {
                if (si->nodeId() == nodeId) { item = si; break; }
            }
        }
        removeSceneItem(item);
        emit audioGraphChanged();
        return;
    }
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
        if (m_itemMap.contains(id) || m_groupNodes.contains(id))
            deleteNodeById(id);
    }

    for (auto *conn : connections) {
        if (conn->scene())
            scene->removeConnection(conn);
    }
}

void ClipNodeEditor::clearAllNodes() {
    const QVector<NodeId> groupIds = m_groupNodes.keys().toVector();
    for (NodeId id : groupIds)
        deleteNodeById(id);

    const QVector<NodeId> clipIds = m_nodeMap.keys().toVector();
    for (NodeId id : clipIds)
        deleteNodeById(id);

    const QVector<NodeId> ctxIds = m_contextNodes.keys().toVector();
    for (NodeId id : ctxIds)
        deleteNodeById(id);

    const QVector<NodeId> masterIds = m_masterAudioNodes.keys().toVector();
    for (NodeId id : masterIds)
        deleteNodeById(id);

    const QVector<NodeId> masterInputIds = m_masterAudioInputNodes.keys().toVector();
    for (NodeId id : masterInputIds)
        deleteNodeById(id);

    const QVector<NodeId> scriptIds = m_scriptNodes.keys().toVector();
    for (NodeId id : scriptIds)
        deleteNodeById(id);

    m_itemMap.clear();
    m_groupNodes.clear();
    m_contextNodes.clear();
    m_scriptNodes.clear();
    m_masterAudioNodes.clear();
    m_masterAudioInputNodes.clear();
    m_activeClipA = 0;
    m_activeClipB = 0;
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
    updateGroupDeckHighlights();
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
    connect(model, &ClipNodeModel::transformChanged, this,
            [this, id](float x, float y, float w, float h) { setClipTransform(id, x, y, w, h); });
    connect(model, &ClipNodeModel::setOutputClicked, this, [this, id]() { setGroupDelegateForClip(id); });
}

void ClipNodeEditor::onCanvasContextMenu() {
    QMenu menu;
    menu.addAction("Group Selection", this, &ClipNodeEditor::groupSelection);
    menu.addAction("Add Transform Context Node", this, &ClipNodeEditor::onAddTransformContext);
    menu.addAction("Add Master Audio Output", this, &ClipNodeEditor::onAddMasterAudioOutput);
    menu.addAction("Add Master Audio Input", this, &ClipNodeEditor::onAddMasterAudioInput);
#ifdef SWITCHX_HAVE_LUA
    menu.addAction("Add Script Node", this, &ClipNodeEditor::onAddScriptNode);
#endif
    menu.exec(QCursor::pos());
}

void ClipNodeEditor::onAddTransformContext() {
    addTransformContextTo(m_scene, m_view, QCursor::pos());
}

void ClipNodeEditor::onAddMasterAudioOutput() {
    addMasterAudioOutputTo(m_scene, m_view, QCursor::pos());
}

void ClipNodeEditor::onAddMasterAudioInput() {
    addMasterAudioInputTo(m_scene, m_view, QCursor::pos());
}

void ClipNodeEditor::onAddScriptNode() {
    addScriptNodeTo(m_scene, m_view, QCursor::pos());
}

QPointF ClipNodeEditor::scenePosForView(QGraphicsView *view, const QPoint &globalPos) const {
    if (!view) return QPointF(20, 20);
    return view->mapToScene(view->mapFromGlobal(globalPos));
}

void ClipNodeEditor::addTransformContextTo(ClipNodeScene *scene, QGraphicsView *view,
                                           const QPoint &globalPos) {
    if (!scene) return;
    auto *contextNode = new TransformContextNodeItem(m_nextId++);
    contextNode->setPos(scenePosForView(view, globalPos));
    contextNode->onEditRequested = [this](NodeId cid) { onEditContextNode(cid); };
    contextNode->onOpenEditorRequested = [this](NodeId cid) { onOpenTransformEditor(cid); };
    scene->addItem(contextNode);
    m_contextNodes[contextNode->nodeId()] = static_cast<void *>(contextNode);
    registerItem(contextNode);
    wireDeleteCallback(contextNode, [this](NodeId nid) { deleteNodeById(nid); });
    emit clipChainChanged();
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
    emit clipChainChanged();
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
    emit clipChainChanged();
    emit audioGraphChanged();
}

void ClipNodeEditor::addScriptNodeTo(ClipNodeScene *scene, QGraphicsView *view,
                                    const QPoint &globalPos) {
#ifdef SWITCHX_HAVE_LUA
    if (!scene) return;

    ScriptEditDialog dlg(QString(), ScriptTriggerMode::Periodic, 1000, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString code = dlg.resultCode().trimmed();
    if (code.isEmpty()) return;

    auto *scriptNode = new ScriptNodeItem(m_nextId++, code,
                                            dlg.resultTriggerMode(),
                                            dlg.resultIntervalMs());
    scriptNode->setPos(scenePosForView(view, globalPos));
    scriptNode->onEditRequested = [this](NodeId sid) { onEditScriptNode(sid); };
    scene->addItem(scriptNode);
    m_scriptNodes[scriptNode->nodeId()] = static_cast<void *>(scriptNode);
    registerItem(scriptNode);
    wireDeleteCallback(scriptNode, [this](NodeId nid) { deleteNodeById(nid); });
    emit clipChainChanged();
    emit audioGraphChanged();
#else
    Q_UNUSED(scene);
    Q_UNUSED(view);
    Q_UNUSED(globalPos);
#endif
}

void ClipNodeEditor::showGroupSceneContextMenu(NodeId groupId, QGraphicsView *view) {
    ClipNodeScene *scene = subSceneForGroup(groupId);
    if (!scene || !view) return;

    QMenu menu;
    menu.addAction("Add Clip...", [this, groupId, view]() { addClipsFromFileDialog(groupId, view); });
    menu.addAction("Add Transform Context Node", [this, groupId, view]() {
        addTransformContextToGroup(groupId, view);
    });
    menu.addAction("Add Master Audio Output", [this, groupId, view]() {
        addMasterAudioOutputToGroup(groupId, view);
    });
    menu.addAction("Add Master Audio Input", [this, groupId, view]() {
        addMasterAudioInputToGroup(groupId, view);
    });
#ifdef SWITCHX_HAVE_LUA
    menu.addAction("Add Script Node", [this, groupId, view]() {
        addScriptNodeToGroup(groupId, view);
    });
#endif
    menu.exec(QCursor::pos());
}

void ClipNodeEditor::addClipsFromFileDialog(NodeId groupId, QGraphicsView *view, bool atViewCenter) {
    ClipNodeScene *scene = subSceneForGroup(groupId);
    if (!scene || !view) return;

    const QStringList files = QFileDialog::getOpenFileNames(
        view, "Add Clips to Group", {},
        "Media (*.mp4 *.mkv *.avi *.mov *.webm *.m4v *.mpg *.mpeg "
        "*.png *.jpg *.jpeg *.gif *.bmp *.webp)");
    const QPointF basePos = atViewCenter
        ? scenePosForView(view, view->viewport()->mapToGlobal(view->viewport()->rect().center()))
        : QPointF();

    for (int i = 0; i < files.size(); ++i) {
        const QString &path = files.at(i);
        ClipNodeModel *model = addClipNode(path, ThumbnailExtractor::extract(path, 110, 65),
                                           scene, atViewCenter ? nullptr : view, true);
        if (atViewCenter) {
            if (auto *item = dynamic_cast<ClipNodeItem *>(m_itemMap.value(model->nodeId())))
                item->setPos(basePos + QPointF(i * 40.0, i * 30.0));
        }
    }
}

void ClipNodeEditor::addTransformContextToGroup(NodeId groupId, QGraphicsView *view,
                                                bool atViewCenter) {
    const QPoint globalPos = atViewCenter && view
        ? view->viewport()->mapToGlobal(view->viewport()->rect().center())
        : QCursor::pos();
    addTransformContextTo(subSceneForGroup(groupId), view, globalPos);
}

void ClipNodeEditor::addMasterAudioOutputToGroup(NodeId groupId, QGraphicsView *view,
                                                 bool atViewCenter) {
    const QPoint globalPos = atViewCenter && view
        ? view->viewport()->mapToGlobal(view->viewport()->rect().center())
        : QCursor::pos();
    addMasterAudioOutputTo(subSceneForGroup(groupId), view, globalPos);
}

void ClipNodeEditor::addMasterAudioInputToGroup(NodeId groupId, QGraphicsView *view,
                                                bool atViewCenter) {
    const QPoint globalPos = atViewCenter && view
        ? view->viewport()->mapToGlobal(view->viewport()->rect().center())
        : QCursor::pos();
    addMasterAudioInputTo(subSceneForGroup(groupId), view, globalPos);
}

void ClipNodeEditor::addScriptNodeToGroup(NodeId groupId, QGraphicsView *view, bool atViewCenter) {
    const QPoint globalPos = atViewCenter && view
        ? view->viewport()->mapToGlobal(view->viewport()->rect().center())
        : QCursor::pos();
    addScriptNodeTo(subSceneForGroup(groupId), view, globalPos);
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
#ifdef SWITCHX_HAVE_LUA
    auto it = m_scriptNodes.find(nodeId);
    if (it == m_scriptNodes.end()) return;

    auto *scriptNode = static_cast<ScriptNodeItem *>(*it);
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

void ClipNodeEditor::disconnectNodeSignals(ClipNodeModel *model) {
    disconnect(model, nullptr, this, nullptr);
}

QVector<ClipNodeModel *> ClipNodeEditor::traverseUpstream(NodeId clipId) const {
    QVector<ClipNodeModel *> chain;
    auto *node = nodeAt(clipId);
    if (!node) return chain;

    chain.push_back(node);

    const NodeId upId = sceneForNode(clipId)->upstreamOf(clipId);
    if (upId != 0) {
        const auto upstream = traverseUpstream(upId);
        for (auto *n : upstream)
            if (!chain.contains(n)) chain.push_back(n);
    }
    return chain;
}

bool ClipNodeEditor::clipTransform(NodeId clipId, float &x, float &y, float &w, float &h) const {
    auto *item = dynamic_cast<ClipNodeItem *>(m_itemMap.value(clipId));
    if (!item) return false;
    x = item->x();
    y = item->y();
    w = item->w();
    h = item->h();
    return true;
}

void ClipNodeEditor::setClipTransform(NodeId clipId, float x, float y, float w, float h) {
    auto *item = dynamic_cast<ClipNodeItem *>(m_itemMap.value(clipId));
    if (!item) return;
    item->setTransform(x, y, w, h);
    emit clipChainChanged();
}

QVector<NodeId> ClipNodeEditor::clipsForContext(NodeId contextId) const {
    return sceneForNode(contextId)->clipsForContext(contextId);
}

QVector<NodeId> ClipNodeEditor::clipsForContextOrdered(NodeId contextId) const {
    ClipNodeScene *scene = sceneForNode(contextId);
    const QVector<NodeId> unordered = scene->clipsForContext(contextId);
    if (unordered.size() <= 1) return unordered;

    QSet<NodeId> clipSet(unordered.begin(), unordered.end());
    QVector<NodeId> ordered;
    ordered.reserve(unordered.size());

    for (NodeId clipId : unordered) {
        NodeId up = scene->upstreamOf(clipId);
        if (up == 0 || !clipSet.contains(up)) {
            NodeId current = clipId;
            while (current != 0 && clipSet.contains(current) && !ordered.contains(current)) {
                ordered.append(current);
                current = scene->downstreamOf(current);
            }
        }
    }

    for (NodeId clipId : unordered) {
        if (!ordered.contains(clipId))
            ordered.append(clipId);
    }

    return ordered;
}

bool ClipNodeEditor::contextCanvasSize(NodeId clipId, int &w, int &h) const {
    ClipNodeScene *scene = sceneForNode(clipId);
    const NodeId contextId = scene->contextForClip(clipId);
    if (contextId == 0) return false;

    auto it = m_contextNodes.find(contextId);
    if (it == m_contextNodes.end()) return false;

    auto *contextNode = static_cast<TransformContextNodeItem *>(*it);
    w = contextNode->canvasW();
    h = contextNode->canvasH();
    return true;
}

bool ClipNodeEditor::audioSettingsForClip(NodeId clipId, int &volume, bool &muted, bool &routedToMaster, AudioPlaybackMode &playbackMode, int &delayMs, QString &outputDeviceId) const {
    auto *clip = dynamic_cast<ClipNodeItem *>(m_itemMap.value(clipId));
    if (!clip || !clip->hasAudio()) return false;

    ClipNodeScene *scene = sceneForNode(clipId);
    volume = clip->volume();
    muted = clip->muted();
    const NodeId masterId = scene->masterNodeForClip(clipId);
    routedToMaster = (masterId != 0);
    outputDeviceId.clear();
    if (masterId) {
        auto *mn = static_cast<MasterAudioOutputNodeItem *>(m_itemMap.value(masterId));
        if (!mn) mn = static_cast<MasterAudioOutputNodeItem *>(m_masterAudioNodes.value(masterId));
        if (mn) outputDeviceId = mn->deviceId();
    }
    playbackMode = clip->playbackMode();
    delayMs = clip->delayMs();
    return true;
}

bool ClipNodeEditor::masterAudioInputSettings(NodeId inputNodeId, MasterAudioInputSettings &settings) const {
    auto *inputNode = static_cast<MasterAudioInputNodeItem *>(m_itemMap.value(inputNodeId));
    if (!inputNode)
        inputNode = static_cast<MasterAudioInputNodeItem *>(m_masterAudioInputNodes.value(inputNodeId));
    if (!inputNode) return false;

    settings.inputDeviceId = inputNode->deviceId();
    settings.inputDeviceLabel = inputNode->deviceLabel();
    settings.volume = inputNode->volume();
    settings.muted = inputNode->muted();
    settings.routedMasterOutputId = sceneForNode(inputNodeId)->masterOutputForInput(inputNodeId);
    return true;
}

bool ClipNodeEditor::masterAudioOutputDevice(NodeId masterOutputNodeId, QString &outputDeviceId) const {
    auto *outputNode = static_cast<MasterAudioOutputNodeItem *>(m_itemMap.value(masterOutputNodeId));
    if (!outputNode)
        outputNode = static_cast<MasterAudioOutputNodeItem *>(m_masterAudioNodes.value(masterOutputNodeId));
    if (!outputNode) return false;

    outputDeviceId = outputNode->deviceId();
    return true;
}

QVector<NodeId> ClipNodeEditor::allMasterAudioInputNodeIds() const {
    return m_masterAudioInputNodes.keys().toVector();
}

bool ClipNodeEditor::audioSourceForShader(NodeId shaderNodeId, QString &filePath) const {
    ClipNodeScene *scene = sceneForNode(shaderNodeId);
    const NodeId clipId = scene->clipForShaderAudio(shaderNodeId);
    if (clipId == 0) return false;

    const ClipNodeModel *node = nodeAt(clipId);
    if (!node) return false;

    const SourceDescriptor &desc = node->sourceDescriptor();
    if (desc.kind != SourceDescriptor::Kind::VideoFile) return false;

    filePath = desc.path;
    return !filePath.isEmpty();
}

std::shared_ptr<ScriptOutput> ClipNodeEditor::scriptOutputForDataNode(NodeId dataNodeId) const {
    ClipNodeScene *scene = sceneForNode(dataNodeId);
    const NodeId scriptId = scene->scriptNodeForData(dataNodeId);
    if (scriptId == 0) return nullptr;

    auto it = m_scriptNodes.find(scriptId);
    if (it == m_scriptNodes.end()) return nullptr;

    auto *scriptNode = static_cast<ScriptNodeItem *>(*it);
    return scriptNode->output();
}

NodeId ClipNodeEditor::groupContainingNode(NodeId nodeId) const {
    for (auto it = m_groupNodes.cbegin(); it != m_groupNodes.cend(); ++it) {
        if (isNodeInSubScene(nodeId, it.value()->subScene()))
            return it.key();
    }
    return 0;
}

QSet<NodeId> ClipNodeEditor::allGroupMemberIds() const {
    QSet<NodeId> ids;
    for (auto *group : m_groupNodes) {
        for (NodeId id : group->memberIds())
            ids.insert(id);
    }
    return ids;
}

bool ClipNodeEditor::isGroupMember(NodeId nodeId) const {
    return groupContainingNode(nodeId) != 0;
}

bool ClipNodeEditor::isNodeInSubScene(NodeId nodeId, ClipNodeScene *subScene) const {
    auto *item = m_itemMap.value(nodeId);
    return item && item->scene() == static_cast<QGraphicsScene *>(subScene);
}

NodeId ClipNodeEditor::pickDefaultGroupDelegate(ClipNodeScene *subScene,
                                                const QSet<NodeId> &members) const {
    if (!subScene || members.isEmpty()) return 0;

    for (NodeId clipId : members) {
        if (!nodeAt(clipId)) continue;
        const NodeId downstream = subScene->downstreamOf(clipId);
        if (downstream == 0 || !members.contains(downstream))
            return clipId;
    }

    for (NodeId clipId : members) {
        if (nodeAt(clipId)) return clipId;
    }
    return 0;
}

void ClipNodeEditor::updateGroupDeckHighlights() {
    for (auto *group : m_groupNodes) {
        const NodeId delegate = group->delegateId();
        group->setDeckActive(true, delegate != 0 && delegate == m_activeClipA);
        group->setDeckActive(false, delegate != 0 && delegate == m_activeClipB);
    }
}

void ClipNodeEditor::setGroupDelegate(NodeId groupId, NodeId clipId) {
    auto *group = m_groupNodes.value(groupId);
    if (!group || !nodeAt(clipId)) return;
    if (!isNodeInSubScene(clipId, group->subScene())) return;

    group->setDelegateId(clipId);
    for (NodeId memberId : group->memberIds()) {
        if (auto *model = nodeAt(memberId))
            model->setOutputSelected(memberId == clipId);
    }
    updateGroupDeckHighlights();
    emit clipChainChanged();
}

void ClipNodeEditor::setGroupDelegateForClip(NodeId clipId) {
    const NodeId groupId = groupContainingNode(clipId);
    if (groupId != 0)
        setGroupDelegate(groupId, clipId);
}

void ClipNodeEditor::openGroupEditor(NodeId groupId) {
    GroupEditorDialog dlg(groupId, this, this);
    dlg.exec();
}

void ClipNodeEditor::groupSelection() {
    QSet<NodeId> members;
    QPointF centroid;
    int count = 0;

    for (auto *item : m_scene->selectedItems()) {
        if (auto *node = dynamic_cast<NodeItemBase *>(item)) {
            if (dynamic_cast<GroupNodeItem *>(node)) continue;
            members.insert(node->nodeId());
            centroid += node->scenePos();
            ++count;
        }
    }

    if (members.size() < 2) {
        QMessageBox::information(this, "Group", "Select at least two nodes to group.");
        return;
    }

    if (m_scene->chainCrossesSelectionBoundary(members)) {
        QMessageBox::information(this, "Group",
            "Cannot group: a chain connection crosses the selection boundary.");
        return;
    }

    if (count > 0)
        centroid /= count;

    auto *subScene = new ClipNodeScene(this);
    subScene->onConnectionChanged = m_scene->onConnectionChanged;

    const NodeId groupId = m_nextId++;
    auto *group = new GroupNodeItem(groupId, subScene);
    group->setPos(centroid);
    group->onDeckRequested = [this](NodeId delegateId, bool deckA) {
        setActiveDeckClip(delegateId, deckA);
    };
    group->onEditRequested = [this](NodeId gid) { openGroupEditor(gid); };
    group->onUngroupRequested = [this](NodeId gid) { ungroup(gid); };
    group->onDeleteRequested = [this](NodeId gid) { deleteNodeById(gid); };
    group->onRenameRequested = [this](NodeId gid) { renameGroup(gid); };

    m_scene->addItem(group);
    m_groupNodes[groupId] = group;
    registerItem(group);

    const auto internalEdges = m_scene->internalEdges(members);
    for (const auto &edge : internalEdges)
        m_scene->removeConnection(edge.item);

    for (NodeId memberId : members) {
        if (auto *item = m_itemMap.value(memberId)) {
            m_scene->removeItem(item);
            subScene->addItem(item);
        }
        if (auto *model = nodeAt(memberId))
            model->setCardMode(ClipCard::CardMode::GroupMember);
    }

    for (const auto &edge : internalEdges)
        subScene->createConnectionManually(edge.fromPort, edge.toPort, edge.kind);

    NodeId delegateId = 0;
    for (NodeId memberId : members) {
        if (!m_nodeMap.contains(memberId)) continue;
        if (subScene->downstreamOf(memberId) == 0 ||
            !members.contains(subScene->downstreamOf(memberId))) {
            delegateId = memberId;
            break;
        }
    }
    if (delegateId == 0) {
        for (NodeId memberId : members) {
            if (m_nodeMap.contains(memberId)) {
                delegateId = memberId;
                break;
            }
        }
    }
    if (delegateId != 0)
        setGroupDelegate(groupId, delegateId);

    emit clipChainChanged();
}

void ClipNodeEditor::ungroup(NodeId groupId) {
    auto *group = m_groupNodes.value(groupId);
    if (!group) return;

    ClipNodeScene *subScene = group->subScene();
    const QSet<NodeId> members(group->memberIds().begin(), group->memberIds().end());
    const auto internalEdges = subScene->internalEdges(members);

    for (const auto &edge : internalEdges)
        subScene->removeConnection(edge.item);

    for (NodeId memberId : members) {
        if (auto *item = m_itemMap.value(memberId)) {
            subScene->removeItem(item);
            m_scene->addItem(item);
        }
        if (auto *model = nodeAt(memberId)) {
            model->setCardMode(ClipCard::CardMode::Deck);
            model->setOutputSelected(false);
        }
    }

    for (const auto &edge : internalEdges)
        m_scene->createConnectionManually(edge.fromPort, edge.toPort, edge.kind);

    m_groupNodes.remove(groupId);
    m_itemMap.remove(groupId);
    m_scene->removeConnectionsForNode(groupId);
    removeSceneItem(group);

    emit clipChainChanged();
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

QJsonObject ClipNodeEditor::saveState(const QDir &sessionDir) const {
    QJsonObject root;
    root["nextId"] = (qint64)m_nextId;

    const QSet<NodeId> groupMembers = allGroupMemberIds();

    QJsonArray clipNodes;
    for (auto it = m_nodeMap.cbegin(); it != m_nodeMap.cend(); ++it) {
        const NodeId id = it.key();
        const ClipNodeModel *model = it.value();
        QJsonObject nodeObj;
        nodeObj["id"] = (qint64)id;

        if (auto *ci = dynamic_cast<ClipNodeItem *>(m_itemMap.value(id))) {
            nodeObj["posX"]     = ci->pos().x();
            nodeObj["posY"]     = ci->pos().y();
            nodeObj["hasAudio"] = ci->hasAudio();
            if (ci->hasAudio()) {
                nodeObj["audioVolume"]       = ci->volume();
                nodeObj["audioMuted"]        = ci->muted();
                nodeObj["audioPlaybackMode"] = (int)ci->playbackMode();
                nodeObj["audioDelayMs"]      = ci->delayMs();
            }
            nodeObj["transformX"] = (double)ci->x();
            nodeObj["transformY"] = (double)ci->y();
            nodeObj["transformW"] = (double)ci->w();
            nodeObj["transformH"] = (double)ci->h();
        }

        nodeObj["source"]   = descriptorToJson(model->sourceDescriptor(), sessionDir);
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
        nodeObj["repeat"]   = model->isRepeat();
        clipNodes.append(nodeObj);
    }
    root["clipNodes"] = clipNodes;

    QJsonArray contextNodes;
    for (auto it = m_contextNodes.cbegin(); it != m_contextNodes.cend(); ++it) {
        auto *cn = static_cast<TransformContextNodeItem *>(m_itemMap.value(it.key()));
        if (!cn) cn = static_cast<TransformContextNodeItem *>(*it);
        QJsonObject obj;
        obj["id"]      = (qint64)cn->nodeId();
        obj["posX"]    = cn->pos().x();
        obj["posY"]    = cn->pos().y();
        obj["canvasW"] = cn->canvasW();
        obj["canvasH"] = cn->canvasH();
        contextNodes.append(obj);
    }
    root["contextNodes"] = contextNodes;

    QJsonArray scriptNodes;
    for (auto it = m_scriptNodes.cbegin(); it != m_scriptNodes.cend(); ++it) {
        auto *sn = static_cast<ScriptNodeItem *>(m_itemMap.value(it.key()));
        if (!sn) sn = static_cast<ScriptNodeItem *>(*it);
        QJsonObject obj;
        obj["id"]           = (qint64)sn->nodeId();
        obj["posX"]         = sn->pos().x();
        obj["posY"]         = sn->pos().y();
        obj["luaCode"]      = sn->code();
        obj["triggerMode"]  = static_cast<int>(sn->triggerMode());
        obj["intervalMs"]   = sn->intervalMs();
        scriptNodes.append(obj);
    }
    root["scriptNodes"] = scriptNodes;

    QJsonArray masterAudioNodes;
    for (auto it = m_masterAudioNodes.cbegin(); it != m_masterAudioNodes.cend(); ++it) {
        auto *mn = static_cast<MasterAudioOutputNodeItem *>(m_itemMap.value(it.key()));
        if (!mn) mn = static_cast<MasterAudioOutputNodeItem *>(*it);
        QJsonObject obj;
        obj["id"]   = (qint64)mn->nodeId();
        obj["posX"] = mn->pos().x();
        obj["posY"] = mn->pos().y();
        obj["deviceId"]    = mn->deviceId();
        obj["deviceLabel"] = mn->deviceLabel();
        masterAudioNodes.append(obj);
    }
    root["masterAudioNodes"] = masterAudioNodes;

    QJsonArray masterAudioInputNodes;
    for (auto it = m_masterAudioInputNodes.cbegin(); it != m_masterAudioInputNodes.cend(); ++it) {
        auto *in = static_cast<MasterAudioInputNodeItem *>(m_itemMap.value(it.key()));
        if (!in) in = static_cast<MasterAudioInputNodeItem *>(*it);
        QJsonObject obj;
        obj["id"]   = (qint64)in->nodeId();
        obj["posX"] = in->pos().x();
        obj["posY"] = in->pos().y();
        obj["deviceId"]    = in->deviceId();
        obj["deviceLabel"] = in->deviceLabel();
        obj["volume"] = in->volume();
        obj["muted"]  = in->muted();
        masterAudioInputNodes.append(obj);
    }
    root["masterAudioInputNodes"] = masterAudioInputNodes;

    QJsonArray groups;
    for (auto it = m_groupNodes.cbegin(); it != m_groupNodes.cend(); ++it) {
        GroupNodeItem *group = it.value();
        QJsonObject obj;
        obj["id"] = (qint64)group->nodeId();
        obj["posX"] = group->pos().x();
        obj["posY"] = group->pos().y();
        obj["delegateId"] = (qint64)group->delegateId();
        obj["name"] = group->name();
        QJsonArray members;
        for (auto mit = m_itemMap.cbegin(); mit != m_itemMap.cend(); ++mit) {
            if (mit.value()->scene() == group->subScene())
                members.append((qint64)mit.key());
        }
        obj["members"] = members;
        obj["connections"] = group->subScene()->edgesToJson();
        groups.append(obj);
    }
    root["groups"] = groups;

    root["connections"] = m_scene->edgesToJson(&groupMembers);
    return root;
}

PortItem *ClipNodeEditor::findPort(NodeId nodeId, int portKindInt) const {
    const PortKind kind = (PortKind)portKindInt;
    auto *base = m_itemMap.value(nodeId);
    if (!base) return nullptr;

    if (auto *ci = dynamic_cast<ClipNodeItem *>(base)) {
        if (kind == PortKind::ChainIn)          return ci->chainInPort();
        if (kind == PortKind::ChainOut)         return ci->chainOutPort();
        if (kind == PortKind::TransformContext) return ci->contextPort();
        if (kind == PortKind::AudioControllerOut) return ci->audioPort();
        if (kind == PortKind::ShaderAudioIn)    return ci->shaderAudioInPort();
        if (kind == PortKind::DataIn)           return ci->dataInPort();
    }
    if (auto *xi = dynamic_cast<TransformContextNodeItem *>(base)) {
        if (kind == PortKind::ContextHub) return xi->hubPort();
    }
    if (auto *mi = dynamic_cast<MasterAudioOutputNodeItem *>(base)) {
        if (kind == PortKind::MasterAudioIn) return mi->audioInPort();
    }
    if (auto *in = dynamic_cast<MasterAudioInputNodeItem *>(base)) {
        if (kind == PortKind::MasterAudioInputOut) return in->audioOutPort();
    }
    if (auto *si = dynamic_cast<ScriptNodeItem *>(base)) {
        if (kind == PortKind::ScriptOut) return si->scriptOutPort();
    }
    return nullptr;
}

void ClipNodeEditor::restoreConnections(ClipNodeScene *scene, const QJsonArray &conns,
                                        const QMap<NodeId, NodeId> &legacyClipForAudioNode) {
    for (const auto &val : conns) {
        const QJsonObject obj = val.toObject();
        NodeId from = (NodeId)obj["from"].toInteger();
        const NodeId to   = (NodeId)obj["to"].toInteger();
        const int    kind = obj["kind"].toInt();

        if (kind == (int)ConnectionItem::LegacyClipToTransform) continue;
        if (kind == (int)ConnectionItem::AudioToController) continue;

        if (kind == (int)ConnectionItem::ControllerToMaster) {
            const auto it = legacyClipForAudioNode.constFind(from);
            if (it != legacyClipForAudioNode.constEnd())
                from = it.value();
        }

        PortItem *fromPort = nullptr, *toPort = nullptr;
        if (kind == ConnectionItem::Chain) {
            fromPort = findPort(from, (int)PortKind::ChainOut);
            toPort   = findPort(to,   (int)PortKind::ChainIn);
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
        } else {
            fromPort = findPort(from, (int)PortKind::TransformContext);
            toPort   = findPort(to,   (int)PortKind::ContextHub);
        }
        if (fromPort && toPort)
            scene->createConnectionManually(fromPort, toPort,
                                            (ConnectionItem::EdgeKind)kind);
    }
}

void ClipNodeEditor::restoreState(const QJsonObject &state) {
    clearAllNodes();

    QMap<NodeId, NodeId> legacyClipForAudioNode;
    auto collectLegacyAudioLinks = [&](const QJsonArray &conns) {
        for (const auto &val : conns) {
            const QJsonObject obj = val.toObject();
            if (obj["kind"].toInt() != (int)ConnectionItem::AudioToController) continue;
            legacyClipForAudioNode.insert((NodeId)obj["to"].toInteger(),
                                          (NodeId)obj["from"].toInteger());
        }
    };
    collectLegacyAudioLinks(state["connections"].toArray());
    for (const auto &groupVal : state["groups"].toArray()) {
        const QJsonObject groupObj = groupVal.toObject();
        collectLegacyAudioLinks(groupObj["connections"].toArray());
    }

    struct LegacyAudioSettings {
        int volume = 100;
        bool muted = false;
        AudioPlaybackMode playbackMode = AudioPlaybackMode::Always;
        int delayMs = 0;
    };
    QMap<NodeId, LegacyAudioSettings> legacyAudioByNodeId;
    for (const auto &val : state["audioNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        LegacyAudioSettings settings;
        settings.volume = obj["volume"].toInt(100);
        settings.muted = obj["muted"].toBool(false);
        settings.playbackMode = (AudioPlaybackMode)obj["playbackMode"].toInt((int)AudioPlaybackMode::Always);
        settings.delayMs = obj["delayMs"].toInt(0);
        legacyAudioByNodeId.insert((NodeId)obj["id"].toInteger(), settings);
    }

    const QJsonArray clipNodes = state["clipNodes"].toArray();
    for (const auto &val : clipNodes) {
        const QJsonObject obj = val.toObject();
        const NodeId clipId = (NodeId)obj["id"].toInteger();

        m_nextId = clipId;
        const NodeId id = m_nextId++;

        const SourceDescriptor desc = descriptorFromJson(obj["source"].toObject());
        const bool hasAudio = obj["hasAudio"].toBool(false);
        const bool hasShaderAudioIn = (desc.kind == SourceDescriptor::Kind::Shader);
        const bool hasDataIn = (desc.kind == SourceDescriptor::Kind::Text);

        int volume = obj["audioVolume"].toInt(100);
        bool muted = obj["audioMuted"].toBool(false);
        auto playbackMode = (AudioPlaybackMode)obj["audioPlaybackMode"].toInt((int)AudioPlaybackMode::Always);
        int delayMs = obj["audioDelayMs"].toInt(0);

        auto *model = new ClipNodeModel(this);
        auto *nodeItem = new ClipNodeItem(model, id, hasAudio, hasShaderAudioIn, hasDataIn,
                                          volume, muted, playbackMode, delayMs);
        nodeItem->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        nodeItem->setTransform(
            (float)obj["transformX"].toDouble(0.0),
            (float)obj["transformY"].toDouble(0.0),
            (float)obj["transformW"].toDouble(1.0),
            (float)obj["transformH"].toDouble(1.0));
        m_scene->addItem(nodeItem);
        m_nodeMap[id] = model;
        registerItem(nodeItem);
        wireDeleteCallback(nodeItem, [this](NodeId nid) { deleteNodeById(nid); });
        wireRenameCallback(nodeItem, [this](NodeId nid) { renameGroupMemberClip(nid); });
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

    for (auto it = legacyAudioByNodeId.cbegin(); it != legacyAudioByNodeId.cend(); ++it) {
        const NodeId clipId = legacyClipForAudioNode.value(it.key(), 0);
        if (!clipId) continue;
        auto *clip = dynamic_cast<ClipNodeItem *>(m_itemMap.value(clipId));
        if (!clip || !clip->hasAudio()) continue;
        clip->setVolume(it.value().volume);
        clip->setMuted(it.value().muted);
        clip->setPlaybackMode(it.value().playbackMode);
        clip->setDelayMs(it.value().delayMs);
    }

    const QJsonArray contextNodes = state["contextNodes"].toArray();
    for (const auto &val : contextNodes) {
        const QJsonObject obj = val.toObject();
        const NodeId ctxId = (NodeId)obj["id"].toInteger();
        m_nextId = ctxId;
        auto *cn = new TransformContextNodeItem(m_nextId++,
            obj["canvasW"].toInt(1280), obj["canvasH"].toInt(720));
        cn->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        cn->onEditRequested = [this](NodeId cid) { onEditContextNode(cid); };
        cn->onOpenEditorRequested = [this](NodeId cid) { onOpenTransformEditor(cid); };
        m_scene->addItem(cn);
        m_contextNodes[ctxId] = static_cast<void *>(cn);
        registerItem(cn);
        wireDeleteCallback(cn, [this](NodeId nid) { deleteNodeById(nid); });
    }

    const QJsonArray scriptNodes = state["scriptNodes"].toArray();
    for (const auto &val : scriptNodes) {
        const QJsonObject obj = val.toObject();
        const NodeId scriptId = (NodeId)obj["id"].toInteger();
        m_nextId = scriptId;
#ifdef SWITCHX_HAVE_LUA
        auto *sn = new ScriptNodeItem(m_nextId++,
            obj["luaCode"].toString(),
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

    const QJsonArray masterAudioNodes = state["masterAudioNodes"].toArray();
    for (const auto &val : masterAudioNodes) {
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

    const QJsonArray masterAudioInputNodes = state["masterAudioInputNodes"].toArray();
    for (const auto &val : masterAudioInputNodes) {
        const QJsonObject obj = val.toObject();
        const NodeId inputId = (NodeId)obj["id"].toInteger();
        m_nextId = inputId;
        auto *in = new MasterAudioInputNodeItem(m_nextId++,
                                              obj["volume"].toInt(100),
                                              obj["muted"].toBool(false));
        in->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        in->setDevice(obj["deviceId"].toString(), obj["deviceLabel"].toString());
        in->onSettingsChanged = [this](NodeId) { emit audioGraphChanged(); };
        m_scene->addItem(in);
        m_masterAudioInputNodes[inputId] = static_cast<void *>(in);
        registerItem(in);
        wireDeleteCallback(in, [this](NodeId nid) { deleteNodeById(nid); });
    }

    struct DeferredSceneConnections {
        ClipNodeScene *scene = nullptr;
        QJsonArray connections;
    };
    QVector<DeferredSceneConnections> deferredGroupConnections;

    const QJsonArray groups = state["groups"].toArray();
    for (const auto &val : groups) {
        const QJsonObject obj = val.toObject();
        const NodeId groupId = (NodeId)obj["id"].toInteger();
        const NodeId delegateId = (NodeId)obj["delegateId"].toInteger();

        auto *subScene = new ClipNodeScene(this);
        subScene->onConnectionChanged = m_scene->onConnectionChanged;

        m_nextId = groupId;
        auto *group = new GroupNodeItem(m_nextId++, subScene);
        group->setPos(obj["posX"].toDouble(), obj["posY"].toDouble());
        if (obj.contains("name"))
            group->setName(obj["name"].toString());
        group->onDeckRequested = [this](NodeId did, bool deckA) { setActiveDeckClip(did, deckA); };
        group->onEditRequested = [this](NodeId gid) { openGroupEditor(gid); };
        group->onUngroupRequested = [this](NodeId gid) { ungroup(gid); };
        group->onDeleteRequested = [this](NodeId gid) { deleteNodeById(gid); };
        group->onRenameRequested = [this](NodeId gid) { renameGroup(gid); };
        m_scene->addItem(group);
        m_groupNodes[group->nodeId()] = group;
        registerItem(group);

        QSet<NodeId> members;
        for (const auto &memberVal : obj["members"].toArray())
            members.insert((NodeId)memberVal.toInteger());

        if (members.isEmpty()) {
            for (const auto &connVal : obj["connections"].toArray()) {
                const QJsonObject connObj = connVal.toObject();
                members.insert((NodeId)connObj["from"].toInteger());
                members.insert((NodeId)connObj["to"].toInteger());
            }
        }

        for (NodeId memberId : members) {
            if (auto *item = m_itemMap.value(memberId)) {
                if (item->scene() == m_scene)
                    m_scene->removeItem(item);
                subScene->addItem(item);
            }
            if (auto *model = nodeAt(memberId))
                model->setCardMode(ClipCard::CardMode::GroupMember);
        }

        deferredGroupConnections.append({ subScene, obj["connections"].toArray() });

        NodeId delegate = delegateId;
        if (delegate == 0 || !members.contains(delegate) || !nodeAt(delegate))
            delegate = pickDefaultGroupDelegate(subScene, members);
        if (delegate != 0)
            setGroupDelegate(group->nodeId(), delegate);
    }

    restoreConnections(m_scene, state["connections"].toArray(), legacyClipForAudioNode);
    for (const auto &deferred : deferredGroupConnections)
        restoreConnections(deferred.scene, deferred.connections, legacyClipForAudioNode);

    updateGroupDeckHighlights();
    emit clipChainChanged();
    emit audioGraphChanged();

    const qint64 savedNext = state["nextId"].toInteger(0);
    if (savedNext > (qint64)m_nextId)
        m_nextId = (NodeId)savedNext;
}
