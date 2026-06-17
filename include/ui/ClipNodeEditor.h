#pragma once

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include "core/SourceDescriptor.h"
#include "ui/ClipNodeModel.h"

class ClipNodeScene;
class QGraphicsView;
class NodeItemBase;
class GroupNodeItem;

enum class AudioPlaybackMode {
    DeckAOnly = 0,
    DeckBOnly = 1,
    Always = 2
};

class ClipNodeEditor : public QWidget {
    Q_OBJECT

public:
    explicit ClipNodeEditor(QWidget *parent = nullptr);
    ~ClipNodeEditor();

    // ── Clip Management ──────────────────────────────────────────────────────
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

    // ── A/B Deck Selection ───────────────────────────────────────────────────
    void setActiveDeckClip(NodeId clipId, bool deckA);
    NodeId activeDeckClipA() const { return m_activeClipA; }
    NodeId activeDeckClipB() const { return m_activeClipB; }

    // ── Get Ordered Clip Chain ──────────────────────────────────────────────
    QVector<ClipNodeModel *> getClipChain(NodeId fromClip) const;

    // ── Transform queries ────────────────────────────────────────────────────
    bool clipTransform(NodeId clipId, float &x, float &y, float &w, float &h) const;
    void setClipTransform(NodeId clipId, float x, float y, float w, float h);
    QVector<NodeId> clipsForContext(NodeId contextId) const;
    QVector<NodeId> clipsForContextOrdered(NodeId contextId) const;
    bool contextCanvasSize(NodeId clipId, int &w, int &h) const;
    bool audioSettingsForClip(NodeId clipId, int &volume, bool &muted, bool &routedToMaster, AudioPlaybackMode &playbackMode, int &delayMs) const;

    // ── Groups ───────────────────────────────────────────────────────────────
    void groupSelection();
    void ungroup(NodeId groupId);
    QWidget *makeSubSceneView(ClipNodeScene *scene, QWidget *parent, NodeId groupId = 0);
    ClipNodeScene *subSceneForGroup(NodeId groupId) const;
    QString groupName(NodeId groupId) const;
    void renameGroup(NodeId groupId);
    void renameGroupMemberClip(NodeId clipId);
    void showGroupSceneContextMenu(NodeId groupId, QGraphicsView *view);
    void addClipsFromFileDialog(NodeId groupId, QGraphicsView *view, bool atViewCenter = false);
    void addTransformContextToGroup(NodeId groupId, QGraphicsView *view, bool atViewCenter = false);
    void addMasterAudioOutputToGroup(NodeId groupId, QGraphicsView *view, bool atViewCenter = false);

    // ── Session persistence ──────────────────────────────────────────────────
    QJsonObject saveState() const;
    void restoreState(const QJsonObject &state);

signals:
    void clipChainChanged();
    void deckAClipChanged(NodeId clipId);
    void deckBClipChanged(NodeId clipId);
    void nodeAdded(NodeId nodeId);
    void nodeRemoved(NodeId nodeId);
    void audioGraphChanged();
    void audioControllerChanged(NodeId clipId);

private slots:
    void onNodeAButtonClicked(NodeId nodeId);
    void onNodeBButtonClicked(NodeId nodeId);
    void onNodeRemoveRequested(NodeId nodeId);
    void onCanvasContextMenu();
    void onAddTransformContext();
    void onAddMasterAudioOutput();
    void onEditContextNode(NodeId nodeId);
    void onOpenTransformEditor(NodeId contextId);
    void onEditAudioNode(NodeId nodeId);

private:
    void connectNodeSignals(ClipNodeModel *model, NodeId id);
    void disconnectNodeSignals(ClipNodeModel *model);
    QVector<ClipNodeModel *> traverseUpstream(NodeId clipId) const;

    ClipNodeScene *sceneForNode(NodeId id) const;
    void registerItem(NodeItemBase *item);
    void removeSceneItem(NodeItemBase *item);
    void deleteNodeById(NodeId nodeId);
    void deleteSelection(QGraphicsView *fromView = nullptr);
    void openGroupEditor(NodeId groupId);
    void setGroupDelegate(NodeId groupId, NodeId clipId);
    void setGroupDelegateForClip(NodeId clipId);
    NodeId pickDefaultGroupDelegate(ClipNodeScene *subScene, const QSet<NodeId> &members) const;
    bool isNodeInSubScene(NodeId nodeId, ClipNodeScene *subScene) const;
    void updateGroupDeckHighlights();
    NodeId groupContainingNode(NodeId nodeId) const;
    QSet<NodeId> allGroupMemberIds() const;
    bool isGroupMember(NodeId nodeId) const;

    class PortItem *findPort(NodeId nodeId, int portKindInt) const;
    void restoreConnections(ClipNodeScene *scene, const QJsonArray &conns);
    QPointF scenePosForView(QGraphicsView *view, const QPoint &globalPos) const;
    void addTransformContextTo(ClipNodeScene *scene, QGraphicsView *view, const QPoint &globalPos);
    void addMasterAudioOutputTo(ClipNodeScene *scene, QGraphicsView *view, const QPoint &globalPos);

    ClipNodeScene *m_scene  = nullptr;
    QGraphicsView *m_view   = nullptr;

    QMap<NodeId, ClipNodeModel *> m_nodeMap;
    QMap<NodeId, NodeItemBase *> m_itemMap;
    QMap<NodeId, void *> m_contextNodes;
    QMap<NodeId, void *> m_audioNodes;
    QMap<NodeId, void *> m_masterAudioNodes;
    QMap<NodeId, GroupNodeItem *> m_groupNodes;
    NodeId m_activeClipA = 0;
    NodeId m_activeClipB = 0;
    NodeId m_nextId      = 1;
};
