#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtNumeric>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>

#include "core/Deck.h"
#include "core/sources/SourceDescriptor.h"
#include "core/project/ClipSettings.h"
#include "core/graph/AudioEffects.h"
#include "core/graph/ProcessEffects.h"

using NodeId = quint64;
using EdgeId = quint64;

/// Widget-free node graph: the model behind the compositing/audio node editor.
/// Holds the same state and answers the same routing/evaluation queries as
/// ClipNodeEditor, but has no scene, items or UI dependencies.
class ClipGraph : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ClipGraph is owned by ClipGraphController")

public:
    enum class NodeType {
        Input,
        Process,
        AudioEffect,
        Layer,
        AbSelect,
        Output,
        AudioMixer,
        MasterAudioOutput,
        MasterAudioInput,
        MasterAudioCapture,
        Script,
        AudioScript,
        Trigger,
    };
    Q_ENUM(NodeType)

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
        DataIn,
        AudioMixerIn,
        AudioMixerOut,
        AudioEffectIn,
        AudioEffectOut,
    };
    Q_ENUM(PortKind)

    enum class PortDirection { In, Out };
    Q_ENUM(PortDirection)

    /// Values are persisted in saved projects; append only.
    enum class EdgeKind {
        Chain = 0,
        TransformToContext = 1,
        AudioToController = 2,
        ControllerToMaster = 3,
        LegacyClipToTransform = 4,
        ClipToShaderAudio = 5,
        ScriptToData = 6,
        InputToMaster = 7,
        AbToOutput = 8,
        StreamToMixer = 9,
        MixerToOutput = 10,
        AudioEffectChain = 11,
        ClipToAudioScript = 12,
    };
    Q_ENUM(EdgeKind)

    struct Port {
        PortKind kind = PortKind::ChainIn;
        PortDirection direction = PortDirection::In;
        int index = 0;          // position in the node's input or output list
        QString label;
        bool slotted = false;   // persisted as toPortIndex; false ports serialise as -1
        QPointF pos;            // offset from the node origin
    };

    struct Node {
        NodeId id = 0;
        NodeType type = NodeType::Input;
        QPointF pos;
        QSizeF size;
        QString title;
        QVariantMap params;
    };

    struct Edge {
        EdgeId id = 0;
        NodeId from = 0;
        int fromPort = -1;      // index into the source node's outputs
        NodeId to = 0;
        int toPort = -1;        // index into the target node's inputs
        EdgeKind kind = EdgeKind::Chain;
    };

    struct ResolvedStream;

    /// One resolved layer feeding a deck. When @ref composite is set this is a
    /// Layer node flattened into a single composited stream (inputNodeId is 0).
    struct ResolvedLayer {
        NodeId inputNodeId = 0;
        float cropX = 0.f, cropY = 0.f, cropW = 1.f, cropH = 1.f;
        bool flipH = false, flipV = false;
        float baseX = 0.f, baseY = 0.f, baseW = 1.f, baseH = 1.f;
        bool visible = true;
        QVector<SourceEffectRef> sourceEffects;
        std::shared_ptr<ResolvedStream> composite;
        NodeId layerNodeId = 0;
    };

    struct ResolvedStream {
        QVector<ResolvedLayer> layers;   // index 0 = bottom
        int canvasWidth = 0, canvasHeight = 0;
    };

    struct ResolvedAudioRoute {
        NodeId outputNodeId = 0;
        QString outputDeviceId;
        int outputPortIndex = -1;
        NodeId mixerNodeId = 0;
        int mixerSlotIndex = -1;
        int mixerSlotVolume = 100;
        bool mixerSlotMuted = false;
        QVector<AudioEffectRef> effects;   // upstream -> downstream
        bool isValid() const { return outputNodeId != 0; }
    };

    struct MasterAudioSettings {
        QString deviceId;
        QString deviceLabel;
        int volume = 100;
        bool muted = false;
        NodeId routedMasterOutputId = 0;
        int routedOutputPortIndex = -1;
    };

    struct AbSlotRef {
        NodeId abNodeId = 0;
        int slot = -1;

        bool isValid() const { return abNodeId != 0 && slot >= 0; }
        bool operator==(const AbSlotRef &o) const { return abNodeId == o.abNodeId && slot == o.slot; }
        bool operator<(const AbSlotRef &o) const {
            return abNodeId != o.abNodeId ? abNodeId < o.abNodeId : slot < o.slot;
        }
    };

    struct AbSlotInfo {
        AbSlotRef ref;
        QString name;
        QString sourceName;
        NodeId producer = 0;
    };

    struct LayerSlotView {
        int index = 0;
        QRectF rect;           // normalized base placement
        QString name;
        bool visible = true;
        NodeId producer = 0;   // node wired into the slot
        NodeId sourceNode = 0; // Input node supplying the pixels (0 for a flattened Layer)
    };

    explicit ClipGraph(QObject *parent = nullptr);

    // ── Access ───────────────────────────────────────────────────────────────
    bool hasNode(NodeId id) const { return m_nodes.contains(id); }
    const Node *node(NodeId id) const;
    /// Insertion order, which is the order a view should list them in.
    QList<NodeId> nodeIds() const { return m_order; }
    QVector<NodeId> nodeIdsOfType(NodeType type) const;
    const QList<Edge> &edges() const { return m_edges; }
    const Edge *edge(EdgeId id) const;
    /// True when the graph has no user-placed nodes (Output-only counts as empty).
    bool isEmptyGraph() const;
    NodeId outputNodeId() const { return m_outputNode; }
    NodeId liveInput() const { return m_liveInput; }
    NodeId stagingInput() const { return m_stagingInput; }
    QString sourceName(NodeId inputNodeId) const;
    /// Input-node payload accessors (defaults for non-Input / unknown ids).
    SourceDescriptor sourceDescriptor(NodeId inputNodeId) const;
    ClipSettings clipSettings(NodeId inputNodeId) const;
    bool clipRepeat(NodeId inputNodeId) const;
    bool setSourceDescriptor(NodeId inputNodeId, const SourceDescriptor &desc);
    /// The "source" param value for a descriptor (same shape sourceDescriptor() reads back).
    static QVariantMap sourceToMap(const SourceDescriptor &desc);
    bool setClipSettings(NodeId inputNodeId, const ClipSettings &settings);

    QVector<Port> inputPorts(NodeId id) const;
    QVector<Port> outputPorts(NodeId id) const;
    bool isPortConnected(NodeId id, PortDirection dir, int index) const;

    // ── Mutators ─────────────────────────────────────────────────────────────
    /// NaN x/y pick a staggered position. Process / AudioEffect nodes read
    /// params["effect"] and optional params["params"]; Input nodes read
    /// params["source"] (a SourceDescriptor JSON object) and params["hasAudio"].
    /// Returns 0 when the node can't be created.
    NodeId addNode(NodeType type, const QPointF &pos = QPointF(qQNaN(), qQNaN()),
                   const QVariantMap &params = {});
    NodeId addInputNode(const SourceDescriptor &desc, const QPointF &pos = QPointF(qQNaN(), qQNaN()),
                        bool hasAudio = false, bool audioOnly = false);
    /// The Output node is only removable once nothing else is left.
    bool removeNode(NodeId id);
    void clear();
    bool moveNode(NodeId id, const QPointF &pos);
    /// Top-level keys of the node's params. merge=false replaces the whole map.
    bool setParams(NodeId id, const QVariantMap &params, bool merge = true);
    /// Process / Audio FX nodes: the nested effect params.
    bool setEffectParams(NodeId id, const QVariantMap &params, bool merge = true);

    /// Wire two nodes. kind -1 infers from ports (prefers the video chain).
    /// toPortIndex is the destination slot (Layer / Switcher / mixer / device).
    bool connectNodes(NodeId from, NodeId to, int kind = -1, int toPortIndex = -1);
    /// Drag-style connect between two concrete ports; inserts a mixer for
    /// stream -> audio output the way the editor does.
    bool connectPorts(NodeId from, int fromPort, NodeId to, int toPort);
    bool disconnectNodes(NodeId from, NodeId to, int toPortIndex = -1);
    bool disconnectEdge(EdgeId id);
    /// Splice an unconnected single-in/single-out node (Process, Audio FX) into @p edge.
    bool insertIntoEdge(NodeId node, EdgeId edge);

    // ── Validation ───────────────────────────────────────────────────────────
    static bool portsCompatible(PortKind a, PortKind b);
    static bool isSingleConnection(PortKind kind);
    static PortDirection directionOf(PortKind kind);
    static QString edgeKindName(int kind);
    /// Port-based check (output port -> input port).
    bool canConnectPorts(NodeId from, int fromPort, NodeId to, int toPort) const;
    /// Kind-based check, same lookup connectNodes() performs. kind -1 tries all.
    bool canConnectNodes(NodeId from, NodeId to, int kind = -1, int toPortIndex = -1) const;

    // ── Video evaluation / layers ────────────────────────────────────────────
    ResolvedStream evaluateVideoInput(NodeId producerNode) const;
    NodeId producerForInput(NodeId nodeId, int inPort) const;
    bool outputIsSingleStream() const { return outputSingleProducer() != 0; }
    NodeId outputSingleProducer() const;
    QVector<LayerSlotView> layerSlotViews(NodeId layerId) const;
    bool setLayerSlotRect(NodeId layerId, int index, float x, float y, float w, float h);
    bool setLayerSlotVisible(NodeId layerId, int index, bool visible);
    /// Effective canvas: the output size unless the layer's "canvasCustom" param is set.
    bool layerCanvasSize(NodeId layerId, int &w, int &h) const;
    /// Sets an explicit size and turns the custom override on.
    bool setLayerCanvasSize(NodeId layerId, int w, int h);
    bool layerCanvasCustom(NodeId layerId) const;
    bool setLayerCanvasCustom(NodeId layerId, bool custom);
    void setOutputCanvasSize(int w, int h);

    // ── Switcher and decks ─────────────────────────────────────────────────
    void setActiveDeckClip(NodeId clipId, Deck deck);
    /// Exchanges the Live and Staging inputs (a take).
    void swapDeckInputs();
    void assignInputToDeck(NodeId inputProducerNode, Deck deck);
    QVector<AbSlotInfo> abSelectInputs() const;
    /// False if the slot is missing, unconnected, or its switcher isn't wired to the Output.
    bool triggerAbSlot(const AbSlotRef &ref, Deck deck);
    bool abOutputConnected(NodeId abNodeId) const;
    /// Slot indices currently driving Live / Staging, or -1.
    void abDeckSlots(NodeId abNodeId, int &aSlot, int &bSlot) const;
    /// Applies a script's output object: "a"/"b" name a slot (case-insensitive) or a 1-based index.
    void applyAbScriptOutput(NodeId abNodeId, const QJsonObject &output);
    void setAbSlotHotkeyLabel(const AbSlotRef &ref, const QString &label);
    void clearAbSlotHotkeyLabels();
    QString abSlotHotkeyLabel(const AbSlotRef &ref) const;

    // ── Audio / script queries ───────────────────────────────────────────────
    bool resolveAudioStreamRoute(NodeId sourceNodeId, ResolvedAudioRoute &route) const;
    /// playbackMode: 0 = always, 1 = only while the clip's deck is active.
    bool audioSettingsForClip(NodeId clipId, int &volume, bool &muted, bool &routedToMaster,
                              int &playbackMode, int &delayMs, QString &outputDeviceId) const;
    bool masterAudioInputSettings(NodeId inputNodeId, MasterAudioSettings &settings) const;
    bool masterAudioCaptureSettings(NodeId captureNodeId, MasterAudioSettings &settings) const;
    bool masterAudioOutputDevice(NodeId masterOutputNodeId, QString &outputDeviceId) const;
    bool masterAudioOutputDeviceForPort(NodeId masterOutputNodeId, int portIndex,
                                        QString &outputDeviceId) const;
    NodeId audioOutputNodeId() const;
    QVector<NodeId> allMasterAudioInputNodeIds() const { return nodeIdsOfType(NodeType::MasterAudioInput); }
    QVector<NodeId> allMasterAudioCaptureNodeIds() const { return nodeIdsOfType(NodeType::MasterAudioCapture); }
    QVector<NodeId> allAudioMixerNodeIds() const { return nodeIdsOfType(NodeType::AudioMixer); }
    bool mixerSlotSettings(NodeId mixerId, int slotIndex, int &volume, bool &muted, QString &name) const;
    bool audioSourceForAudioScript(NodeId scriptNodeId, QString &filePath) const;
    NodeId audioSourceNodeIdForAudioScript(NodeId scriptNodeId) const;
    bool isLiveAudioScriptProducer(NodeId producerId) const;
    bool hasAudioScriptConsumer(NodeId producerId) const;
    NodeId dataScriptNodeId(NodeId dataNodeId) const;
    /// All script producers feeding a node's DataIn, in connection order.
    QVector<NodeId> scriptProducersForData(NodeId dataNodeId) const;

    // ── Persistence ──────────────────────────────────────────────────────────
    /// The `graph` object of a .psm session, identical to ClipNodeEditor::saveState().
    QJsonObject toJson() const;
    /// Replaces the graph. Returns false (leaving it empty) for graphVersion < kGraphVersion.
    bool fromJson(const QJsonObject &state);
    static constexpr int kGraphVersion = 4;
    /// The summary ClipNodeEditor::graphSnapshot() returns (MCP / remote control).
    QJsonObject snapshot() const;

signals:
    void nodeAdded(NodeId id);
    void nodeRemoved(NodeId id);
    void nodeChanged(NodeId id);
    void edgeAdded(EdgeId id);
    void edgeRemoved(EdgeId id);
    void edgeChanged(EdgeId id);
    void liveChanged(NodeId clipId);
    void stagingChanged(NodeId clipId);
    /// The whole graph was replaced (fromJson / clear); models must rebuild.
    void graphReset();
    /// Anything that affects compositing or audio routing changed.
    void changed();

private:
    struct ConnectPlan;

    Node *mutableNode(NodeId id);
    NodeId allocateId() { return m_nextId++; }
    NodeId createNode(NodeType type, const QPointF &pos, const QVariantMap &params);
    void ensureOutputNode();
    QPointF autoPosition(NodeType type) const;

    void buildPorts(const Node &n, QVector<Port> &in, QVector<Port> &out) const;
    QSizeF nodeSize(const Node &n) const;
    void refreshNode(Node &n);
    int findPortIndex(NodeId id, PortKind kind, int slot) const;
    bool lookupConnectionPorts(NodeId from, NodeId to, int kind, int slot,
                               int *fromPort, int *toPort) const;
    int slotCount(const Node &n) const;

    EdgeId appendEdge(NodeId from, int fromPort, NodeId to, int toPort, EdgeKind kind);
    void removeEdgeAt(int i);
    void disconnectPort(NodeId node, PortDirection dir, int index);
    void removeEdgesOfNode(NodeId id);
    bool connectPortsRaw(NodeId from, int fromPort, NodeId to, int toPort, EdgeKind kind);
    bool outputInputBlocked(NodeId to, int toPort) const;
    std::optional<ConnectPlan> planConnection(NodeId from, int fromPort, NodeId to, int toPort) const;
    void insertMixerForStreamToOutput(NodeId streamNode, int streamPort, NodeId deviceNode, int devicePort);
    NodeId createMixerAt(const QPointF &scenePos);
    QPointF portScenePos(NodeId id, PortDirection dir, int index) const;
    int firstOpenMixerInput(NodeId mixer);
    void migrateLegacyAudioConnections();

    void afterConnectionsChanged();
    void normalizeSwitchingInputs();
    void normalizeMixerInputs();
    bool normalizeSlots(Node &n);
    void remapDevicePorts(Node &n, const QVariantList &oldPorts);

    NodeId edgeProducer(NodeId node, int inPort, EdgeKind kind) const;
    ResolvedStream evaluateGuarded(NodeId producer, QSet<NodeId> visited) const;
    bool edgeFromAudioStreamOut(NodeId from, const Edge **out) const;
    bool masterSettings(NodeId id, NodeType type, MasterAudioSettings &settings) const;
    NodeId clipForAudioScript(NodeId scriptNodeId) const;
    QString deviceIdAt(const Node &out, int index) const;

    void setDeck(NodeId id, Deck deck);
    void notifyNode(NodeId id);
    void notifyChanged();

    QMap<NodeId, Node> m_nodes;
    QList<NodeId> m_order;
    QList<Edge> m_edges;
    QMap<AbSlotRef, QString> m_hotkeyLabels;
    NodeId m_outputNode = 0;
    NodeId m_liveInput = 0;
    NodeId m_stagingInput = 0;
    NodeId m_nextId = 1;
    EdgeId m_nextEdgeId = 1;
    bool m_restoring = false;
    int m_outputCanvasW = 1280;
    int m_outputCanvasH = 720;
};
