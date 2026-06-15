#pragma once

#include <QWidget>
#include <QVector>
#include <QMap>
#include "core/SourceDescriptor.h"
#include "ui/ClipNodeModel.h"

class ClipNodeScene;
class QGraphicsView;

class ClipNodeEditor : public QWidget {
    Q_OBJECT

public:
    explicit ClipNodeEditor(QWidget *parent = nullptr);
    ~ClipNodeEditor();

    // ── Clip Management ──────────────────────────────────────────────────────
    ClipNodeModel *addClipNode(const QString &path, const QPixmap &thumbnail);
    ClipNodeModel *addSourceNode(const SourceDescriptor &desc, const QPixmap &thumbnail);
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
    NodeId transformNodeForClip(NodeId clipId) const;
    bool clipTransform(NodeId clipId, float &x, float &y, float &w, float &h) const;
    void setClipTransform(NodeId clipId, float x, float y, float w, float h);
    QVector<NodeId> clipsForContext(NodeId contextId) const;
    bool contextCanvasSize(NodeId clipId, int &w, int &h) const;

signals:
    void clipChainChanged();
    void deckAClipChanged(NodeId clipId);
    void deckBClipChanged(NodeId clipId);
    void nodeAdded(NodeId nodeId);
    void nodeRemoved(NodeId nodeId);

private slots:
    void onNodeAButtonClicked(NodeId nodeId);
    void onNodeBButtonClicked(NodeId nodeId);
    void onNodeRemoveRequested(NodeId nodeId);
    void onCanvasContextMenu();
    void onAddTransformContext();
    void onEditTransformNode(NodeId nodeId);
    void onEditContextNode(NodeId nodeId);
    void onOpenTransformEditor(NodeId contextId);

private:
    ClipNodeModel *createAndAddNode(NodeId nodeId);
    void connectNodeSignals(ClipNodeModel *model, NodeId id);
    void disconnectNodeSignals(ClipNodeModel *model);
    QVector<ClipNodeModel *> traverseUpstream(NodeId clipId) const;

    ClipNodeScene *m_scene  = nullptr;
    QGraphicsView *m_view   = nullptr;

    QMap<NodeId, ClipNodeModel *> m_nodeMap;
    QMap<NodeId, void *> m_transformNodes;
    QMap<NodeId, void *> m_contextNodes;
    NodeId m_activeClipA = 0;
    NodeId m_activeClipB = 0;
    NodeId m_nextId      = 1;
};
