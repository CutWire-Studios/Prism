#pragma once

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QPixmap>
#include <QRectF>
#include "core/sources/SourceDescriptor.h"
#include "core/scripting/ScriptOutput.h"
#include "ui/nodes/ClipNodeModel.h"
#include "ui/nodes/ProcessEffects.h"

class ClipNodeScene;
class QGraphicsView;
class NodeItemBase;
class ProcessNodeItem;
class LayerNodeItem;
class AbSelectNodeItem;
class ScriptNodeItem;
class MasterAudioOutputNodeItem;
class MasterAudioInputNodeItem;

enum class AudioPlaybackMode {
    Always = 0,       // play while the clip is on either deck
    ActiveDeck = 1    // play only while the clip's deck is the active (crossfader) side
};

struct MasterAudioInputSettings {
    QString inputDeviceId;
    QString inputDeviceLabel;
    int     volume = 100;
    bool    muted = false;
    NodeId  routedMasterOutputId = 0;
};

/// Identifies one input slot of an A/B Select node (the target of a hotkey).
struct AbSlotRef {
    NodeId abNodeId = 0;
    int    slot     = -1;

    bool isValid() const { return abNodeId != 0 && slot >= 0; }
    bool operator==(const AbSlotRef &o) const {
        return abNodeId == o.abNodeId && slot == o.slot;
    }
    bool operator<(const AbSlotRef &o) const {
        return abNodeId != o.abNodeId ? abNodeId < o.abNodeId : slot < o.slot;
    }
};

/// A connected A/B Select input, as listed for the hotkey system.
struct AbSlotInfo {
    AbSlotRef ref;
    QString   name;         // user-assigned slot name (may be empty)
    QString   sourceName;   // display name of the resolved upstream source
    NodeId    producer = 0; // node wired into the slot
};

/// One resolved layer feeding a deck: an Input node producing pixels plus the
/// folded crop/flip, Layer placement, and decorator effects to apply.
struct ResolvedLayer {
    NodeId inputNodeId = 0;
    float  cropX = 0.f, cropY = 0.f, cropW = 1.f, cropH = 1.f;
    bool   flipH = false, flipV = false;
    float  baseX = 0.f, baseY = 0.f, baseW = 1.f, baseH = 1.f;
    bool   visible = true;
    QVector<SourceEffectRef> sourceEffects;   // decorator effects, upstream→downstream
};

/// A fully resolved blue video stream (bottom→top layers plus canvas size).
struct ResolvedStream {
    QVector<ResolvedLayer> layers;   // index 0 = bottom, last = top
    int canvasWidth = 0, canvasHeight = 0;
};

/// The node-graph compositing editor. Hosts the four node types on the blue video
/// pipeline (Input → Process → Switching → Output) plus audio/script routing, and
/// is the source of truth for what each deck composites.
class ClipNodeEditor : public QWidget {
    Q_OBJECT

public:
    explicit ClipNodeEditor(QWidget *parent = nullptr);
    ~ClipNodeEditor();

    // ── Input node management ────────────────────────────────────────────────
    ClipNodeModel *addClipNode(const QString &path, const QPixmap &thumbnail,
                               ClipNodeScene *targetScene = nullptr,
                               QGraphicsView *viewForPos = nullptr,
                               bool groupMember = false);
    ClipNodeModel *addSourceNode(const SourceDescriptor &desc, const QPixmap &thumbnail,
                                 ClipNodeScene *targetScene = nullptr,
                                 QGraphicsView *viewForPos = nullptr,
                                 bool groupMember = false);
    void removeNode(NodeId nodeId);
    void clearAllNodes();

    // ── Access ──────────────────────────────────────────────────────────────
    QVector<ClipNodeModel *> allNodes() const;
    ClipNodeModel *nodeAt(NodeId id) const;
    /// True when the graph has no user-placed nodes (Output-only counts as empty).
    bool isEmptyGraph() const;

    // ── A/B Deck Selection (global invariant across A/B-select nodes) ─────────
    void setActiveDeckClip(NodeId clipId, bool deckA);
    void assignInputToDeck(NodeId inputProducerNode, bool deckA);

    // ── A/B Select inputs (hotkey targets) ───────────────────────────────────
    /// All connected inputs across every A/B Select node, in stable order.
    QVector<AbSlotInfo> abSelectInputs() const;
    /// Same effect as clicking the slot's A/B button; false if the slot is
    /// missing, unconnected, or its switcher isn't wired to the Output.
    bool triggerAbSlot(const AbSlotRef &ref, bool deckA);
    void setAbSlotHotkeyLabel(const AbSlotRef &ref, const QString &label);
    void clearAbSlotHotkeyLabels();
    QString abSlotHotkeyLabel(const AbSlotRef &ref) const;
    NodeId deckAInput() const { return m_deckAInput; }
    NodeId deckBInput() const { return m_deckBInput; }
    NodeId activeDeckClipA() const { return m_deckAInput; }
    NodeId activeDeckClipB() const { return m_deckBInput; }

    // ── Evaluator ────────────────────────────────────────────────────────────
    ResolvedStream evaluateVideoInput(NodeId producerNode) const;

    // ── Layer node placement (for TransformEditorDialog) ─────────────────────
    struct LayerSlotView {
        int     index = 0;
        QRectF  rect;       // normalized base placement
        QPixmap thumb;
        QString name;
        bool    visible = true;
    };
    QVector<LayerSlotView> layerSlotViews(NodeId layerId) const;
    void setLayerSlotRect(NodeId layerId, int index, float x, float y, float w, float h);
    void setLayerSlotVisible(NodeId layerId, int index, bool visible);
    bool layerCanvasSize(NodeId layerId, int &w, int &h) const;
    void setLayerCanvasSize(NodeId layerId, int w, int h);

    // ── Output-node querying ─────────────────────────────────────────────────
    NodeId outputNodeId() const { return m_outputNode; }
    bool   outputIsSingleStream() const;
    NodeId outputSingleProducer() const;

    // ── Audio / script queries ───────────────────────────────────────────────
    bool audioSettingsForClip(NodeId clipId, int &volume, bool &muted, bool &routedToMaster, AudioPlaybackMode &playbackMode, int &delayMs, QString &outputDeviceId) const;
    bool masterAudioInputSettings(NodeId inputNodeId, MasterAudioInputSettings &settings) const;
    bool masterAudioOutputDevice(NodeId masterOutputNodeId, QString &outputDeviceId) const;
    QVector<NodeId> allMasterAudioInputNodeIds() const;
    bool audioSourceForShader(NodeId shaderNodeId, QString &filePath) const;
    std::shared_ptr<ScriptOutput> scriptOutputForDataNode(NodeId dataNodeId) const;

    // ── Session persistence ──────────────────────────────────────────────────
    QJsonObject saveState(const QDir &sessionDir = {}) const;
    void restoreState(const QJsonObject &state);

    /// Returns the inner QGraphicsView when @p widget is a canvas chrome wrapper.
    static QGraphicsView *graphicsViewFrom(QWidget *widget);

signals:
    void clipChainChanged();
    void deckAClipChanged(NodeId clipId);
    void deckBClipChanged(NodeId clipId);
    void nodeAdded(NodeId nodeId);
    void nodeRemoved(NodeId nodeId);
    void audioGraphChanged();
    void audioControllerChanged(NodeId clipId);
    void addInputNodeRequested();

private slots:
    void onNodeRemoveRequested(NodeId nodeId);
    void onCanvasContextMenu();
    void onAddMasterAudioOutput();
    void onAddMasterAudioInput();
    void onAddScriptNode();
    void onEditClipAudio(NodeId clipId);
    void onEditScriptNode(NodeId nodeId);
    void onEditLayerCanvas(NodeId layerId);
    void onEditLayerTransform(NodeId layerId);
    void onEditProcessNode(NodeId processId);

private:
    void connectNodeSignals(ClipNodeModel *model, NodeId id);
    void disconnectNodeSignals(ClipNodeModel *model);
    void wireTextScriptBinding(ClipNodeModel *model, NodeId id);

    ClipNodeScene *sceneForNode(NodeId id) const;
    void registerItem(NodeItemBase *item);
    void removeSceneItem(NodeItemBase *item);
    void deleteNodeById(NodeId nodeId);
    void deleteSelection(QGraphicsView *fromView = nullptr);
    void ensureOutputNode();
    void updateAbHighlights();
    void normalizeSwitchingInputs();
    ResolvedStream evaluateVideoInputGuarded(NodeId producerNode, QSet<NodeId> visited) const;

    class PortItem *findPort(NodeId nodeId, int portKindInt, int slotIndex = -1) const;
    void restoreConnections(ClipNodeScene *scene, const QJsonArray &conns);
    QPointF scenePosForView(QGraphicsView *view, const QPoint &globalPos) const;
    void addProcessNodeAt(int effect, const QPoint &globalPos);
    void addLayerNodeAt(const QPoint &globalPos);
    void addAbSelectNodeAt(const QPoint &globalPos);
    void addMasterAudioOutputTo(ClipNodeScene *scene, QGraphicsView *view, const QPoint &globalPos);
    void addMasterAudioInputTo(ClipNodeScene *scene, QGraphicsView *view, const QPoint &globalPos);
    void addScriptNodeTo(ClipNodeScene *scene, QGraphicsView *view, const QPoint &globalPos);

    ClipNodeScene *m_scene  = nullptr;
    QGraphicsView *m_view   = nullptr;

    QMap<NodeId, ClipNodeModel *> m_nodeMap;
    QMap<NodeId, NodeItemBase *> m_itemMap;
    QMap<NodeId, ProcessNodeItem *> m_processNodes;
    QMap<NodeId, LayerNodeItem *> m_layerNodes;
    QMap<NodeId, AbSelectNodeItem *> m_abSelectNodes;
    QMap<NodeId, ScriptNodeItem *> m_scriptNodes;
    QMap<NodeId, MasterAudioOutputNodeItem *> m_masterAudioNodes;
    QMap<NodeId, MasterAudioInputNodeItem *> m_masterAudioInputNodes;
    NodeId m_outputNode  = 0;
    NodeId m_deckAInput  = 0;
    NodeId m_deckBInput  = 0;
    NodeId m_nextId      = 1;
    bool   m_restoring   = false;
};
