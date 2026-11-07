#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QSet>
#include <QUndoStack>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>

#include "core/graph/ClipGraph.h"

class ClipGraphNodesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ClipGraphController")

public:
    enum Role {
        NodeIdRole = Qt::UserRole + 1,
        TypeRole,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        TitleRole,
        ParamsRole,
        InputPortsRole,
        OutputPortsRole,
        GroupRole,
        HiddenRole,
    };

    explicit ClipGraphNodesModel(ClipGraph *graph, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int rowOfNode(quint64 id) const;

private:
    QVariantList portList(const QVector<ClipGraph::Port> &ports, NodeId id) const;
    void emitPortsChanged();

    QPointer<ClipGraph> m_graph;
    QList<NodeId> m_ids;
};

class ClipGraphEdgesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ClipGraphController")

public:
    enum Role {
        EdgeIdRole = Qt::UserRole + 1,
        FromNodeRole,
        FromPortRole,
        ToNodeRole,
        ToPortRole,
        KindRole,
        /// Where each end is drawn: the node itself, or the proxy port of its collapsed group
        /// (then the item is the group id and the port the proxy index).
        VisFromNodeRole,
        VisFromPortRole,
        VisToNodeRole,
        VisToPortRole,
        HiddenRole,
    };

    explicit ClipGraphEdgesModel(ClipGraph *graph, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int rowOfEdge(quint64 id) const;

private:
    void refreshVisual(bool force = false);

    QPointer<ClipGraph> m_graph;
    QList<EdgeId> m_ids;
};

class ClipGraphFramesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ClipGraphController")

public:
    enum Role {
        FrameIdRole = Qt::UserRole + 1,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        TitleRole,
        ColorRole,
        CommentRole,
    };

    explicit ClipGraphFramesModel(ClipGraph *graph, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int rowOfFrame(quint64 id) const;

private:
    QPointer<ClipGraph> m_graph;
    QList<quint64> m_ids;
};

class ClipGraphGroupsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ClipGraphController")

public:
    enum Role {
        GroupIdRole = Qt::UserRole + 1,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        NameRole,
        ColorRole,
        CollapsedRole,
        MemberCountRole,
        InputPortsRole,
        OutputPortsRole,
    };

    explicit ClipGraphGroupsModel(ClipGraph *graph, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int rowOfGroup(quint64 id) const;

private:
    void refreshAll();

    QPointer<ClipGraph> m_graph;
    QList<quint64> m_ids;
};

/// QML-facing facade over ClipGraph: undoable edits, selection and the
/// two-step drag-to-connect flow. Ports are addressed as (node, isOutput, index).
class ClipGraphController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ClipGraph *graph READ graph CONSTANT)
    Q_PROPERTY(ClipGraphNodesModel *nodes READ nodes CONSTANT)
    Q_PROPERTY(ClipGraphEdgesModel *edges READ edges CONSTANT)
    Q_PROPERTY(ClipGraphFramesModel *frames READ frames CONSTANT)
    Q_PROPERTY(ClipGraphGroupsModel *groups READ groups CONSTANT)
    /// The selected group (0 = none). Selecting a group selects all of its members too.
    Q_PROPERTY(quint64 selectedGroup READ selectedGroup NOTIFY selectedGroupChanged)
    /// The selected frame (0 = none). Frames never join the node selection.
    Q_PROPERTY(quint64 selectedFrame READ selectedFrame NOTIFY selectedFrameChanged)
    Q_PROPERTY(QVariantList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectingChanged)
    /// Bumped whenever routing or params change; bind to it to re-query derived state.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit ClipGraphController(QObject *parent = nullptr);
    explicit ClipGraphController(ClipGraph *graph, QObject *parent = nullptr);

    ClipGraph *graph() const { return m_graph; }
    ClipGraphNodesModel *nodes() const { return m_nodes; }
    ClipGraphEdgesModel *edges() const { return m_edges; }
    ClipGraphFramesModel *frames() const { return m_frames; }
    ClipGraphGroupsModel *groups() const { return m_groups; }
    quint64 selectedFrame() const { return m_selectedFrame; }
    quint64 selectedGroup() const { return m_selectedGroup; }
    QUndoStack *undoStack() { return &m_undo; }
    QVariantList selection() const;
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    bool connecting() const { return m_pending.node != 0; }
    int revision() const { return m_revision; }

    /// type is a ClipGraph::NodeType value; returns the new id, 0 on failure.
    Q_INVOKABLE quint64 addNode(int type, qreal x, qreal y, const QVariantMap &params = {});
    Q_INVOKABLE void removeNodes(const QVariantList &ids);
    /// The Modulator nodes of this graph as [{id, name, label, kind}].
    Q_INVOKABLE QVariantList modulators() const;
    Q_INVOKABLE QVariantMap paramSource(quint64 node, const QString &param) const;
    /// Drives param @p param (a name in the node's params.expressions, e.g. "params.opacity") with
    /// the output of Modulator @p modulator as one undo step. A modulator of 0 first creates an LFO
    /// next to the node, scaled to [min, max]; newKind picks that modulator's kind (default LFO,
    /// 9 = Control In). @p integer records that results are rounded (whole-numbered slider).
    /// Returns the modulator id, 0 on failure.
    Q_INVOKABLE quint64 bindParam(quint64 node, const QString &param, quint64 modulator = 0,
                                  qreal min = 0.0, qreal max = 1.0, int newKind = 0, bool integer = false);
    /// Consecutive moves of one node collapse into a single undo step until endMove().
    Q_INVOKABLE void moveNode(quint64 id, qreal x, qreal y);
    Q_INVOKABLE void endMove();
    /// coalesce: fold consecutive edits of the same keys into one undo step (slider drags).
    Q_INVOKABLE bool setParams(quint64 id, const QVariantMap &params, bool coalesce = false);
    /// Undoable replacement of an Input node's source; descriptor uses the .psm "source" keys.
    Q_INVOKABLE bool toggleBypass(const QVariantList &ids);
    Q_INVOKABLE bool setSource(quint64 id, const QVariantMap &descriptor);
    bool setSourceDescriptor(quint64 id, const SourceDescriptor &desc);
    /// Decides whether a Scene source of this graph may point at the given scene id; setParams and
    /// addNode refuse a "source" naming a scene it rejects.
    void setSceneRefValidator(std::function<bool(const QString &)> validator) { m_sceneRefValidator = std::move(validator); }

    Q_INVOKABLE bool beginConnection(quint64 node, bool isOutput, int port);
    Q_INVOKABLE bool canConnect(quint64 node, bool isOutput, int port) const;
    Q_INVOKABLE bool finishConnection(quint64 node, bool isOutput, int port);
    Q_INVOKABLE void cancelConnection();
    Q_INVOKABLE bool disconnectEdge(quint64 edgeId);
    Q_INVOKABLE bool disconnectParam(quint64 node, const QString &param);
    Q_INVOKABLE bool insertIntoEdge(quint64 node, quint64 edgeId);
    Q_INVOKABLE bool canInsertIntoEdge(quint64 node, quint64 edgeId) const;
    /// Adds a node and splices it into edgeId (0 = plain add) as one undo step.
    Q_INVOKABLE quint64 addNodeIntoEdge(int type, qreal x, qreal y, const QVariantMap &params, quint64 edgeId);
    /// Adds a node and wires it to the pending connection's first compatible port (then clears the
    /// pending connection) as one undo step. Without a pending connection this is addNode.
    Q_INVOKABLE quint64 addNodeConnected(int type, qreal x, qreal y, const QVariantMap &params);
    Q_INVOKABLE bool detachNode(quint64 node);

    /// Copies the selection (with the edges between its nodes) to the in-process clipboard and,
    /// when a GUI application exists, the system clipboard as application/x-prism-nodes.
    Q_INVOKABLE bool copySelection();
    /// Pastes the clipboard with its top-left corner at (x, y); NaN offsets it from the original.
    /// The new nodes become the selection. Returns their ids.
    Q_INVOKABLE QVariantList paste(qreal x = qQNaN(), qreal y = qQNaN());
    Q_INVOKABLE QVariantList duplicateSelection();

    /// Frames are decoration behind the nodes. Each op below is one undo step, except
    /// moveFrame / resizeFrame, whose consecutive calls collapse until endMove().
    Q_INVOKABLE quint64 addFrame(qreal x, qreal y, qreal w, qreal h);
    /// Wraps the selected nodes with padding in a new frame. Returns 0 with nothing selected.
    Q_INVOKABLE quint64 frameSelection();
    /// Moves the frame together with the nodes fully inside it (fixed at the first call of a drag).
    Q_INVOKABLE void moveFrame(quint64 id, qreal x, qreal y);
    Q_INVOKABLE void resizeFrame(quint64 id, qreal w, qreal h);
    /// Any of "title", "color", "comment".
    Q_INVOKABLE bool setFrameProps(quint64 id, const QVariantMap &props);
    Q_INVOKABLE bool removeFrame(quint64 id);
    Q_INVOKABLE void selectFrame(quint64 id);

    /// Groups are a view over ordinary nodes (see ClipGraph::Group). Each op is one undo step,
    /// except moveGroup, whose consecutive calls collapse until endMove().
    /// Groups the selected nodes, collapsed, and selects the group. Returns 0 if fewer than two
    /// ungrouped nodes are selected.
    Q_INVOKABLE quint64 groupSelection();
    /// The selected group, or the one group that contains the whole node selection (0 if none).
    Q_INVOKABLE quint64 selectionGroup() const;
    Q_INVOKABLE bool ungroupSelection();
    Q_INVOKABLE bool ungroup(quint64 id);
    Q_INVOKABLE bool setGroupCollapsed(quint64 id, bool collapsed);
    Q_INVOKABLE bool toggleGroupCollapsed(quint64 id);
    /// Any of "name", "color".
    Q_INVOKABLE bool setGroupProps(quint64 id, const QVariantMap &props);
    /// Moves a group by the delta between its current top-left and (x, y), carrying its members.
    Q_INVOKABLE void moveGroup(quint64 id, qreal x, qreal y);
    Q_INVOKABLE void selectGroup(quint64 id);
    /// The group's members as [{id, title}].
    Q_INVOKABLE QVariantList groupMembers(quint64 id) const;

    Q_INVOKABLE void select(const QVariantList &ids, bool additive = false);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(quint64 id) const { return m_selection.contains(id); }

    Q_INVOKABLE void selectAll();

    /// coords is a flat [x0, y0, x1, y1, ...] list parallel to ids. Consecutive calls with the
    /// same ids collapse into one undo step until endMove().
    Q_INVOKABLE void moveNodes(const QVariantList &ids, const QVariantList &coords);
    /// Reorders two slots of a Layer / Audio Mixer and the edges wired to them.
    Q_INVOKABLE bool swapSlots(quint64 id, int a, int b);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearHistory();

signals:
    void selectionChanged();
    void selectedFrameChanged();
    void selectedGroupChanged();
    void undoStateChanged();
    void connectingChanged();
    void revisionChanged();

private:
    struct Pending {
        NodeId node = 0;
        bool output = false;
        int port = -1;
    };

    void init();
    bool sceneRefAllowed(const QVariantMap &params) const;
    void pushSnapshot(const QString &text, const QJsonObject &before);
    void setSelectedGroup(quint64 id);
    /// A collapsed group's id and proxy index stand for the member port behind it.
    bool resolvePort(quint64 &node, bool isOutput, int &port) const;
    QVariantList pasteDocument(const QJsonObject &clip, const QPointF &delta, const QString &text);

    ClipGraph *m_graph = nullptr;
    ClipGraphNodesModel *m_nodes = nullptr;
    ClipGraphEdgesModel *m_edges = nullptr;
    ClipGraphFramesModel *m_frames = nullptr;
    ClipGraphGroupsModel *m_groups = nullptr;
    quint64 m_selectedFrame = 0;
    quint64 m_selectedGroup = 0;
    quint64 m_dragFrame = 0;
    QList<NodeId> m_dragFrameNodes;
    QUndoStack m_undo;
    QSet<NodeId> m_selection;
    Pending m_pending;
    QJsonObject m_clipboard;
    bool m_mergeMoves = false;
    int m_revision = 0;
    std::function<bool(const QString &)> m_sceneRefValidator;
};
