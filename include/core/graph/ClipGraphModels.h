#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QSet>
#include <QUndoStack>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

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
    };

    explicit ClipGraphEdgesModel(ClipGraph *graph, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int rowOfEdge(quint64 id) const;

private:
    QPointer<ClipGraph> m_graph;
    QList<EdgeId> m_ids;
};

/// QML-facing facade over ClipGraph: undoable edits, selection and the
/// two-step drag-to-connect flow. Ports are addressed as (node, isOutput, index).
class ClipGraphController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ClipGraph *graph READ graph CONSTANT)
    Q_PROPERTY(ClipGraphNodesModel *nodes READ nodes CONSTANT)
    Q_PROPERTY(ClipGraphEdgesModel *edges READ edges CONSTANT)
    Q_PROPERTY(QVariantList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectingChanged)
    /// Bumped whenever routing, params or deck assignment change; bind to it to re-query
    /// derived state (abState) that no model role carries.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit ClipGraphController(QObject *parent = nullptr);
    explicit ClipGraphController(ClipGraph *graph, QObject *parent = nullptr);

    ClipGraph *graph() const { return m_graph; }
    ClipGraphNodesModel *nodes() const { return m_nodes; }
    ClipGraphEdgesModel *edges() const { return m_edges; }
    QUndoStack *undoStack() { return &m_undo; }
    QVariantList selection() const;
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    bool connecting() const { return m_pending.node != 0; }
    int revision() const { return m_revision; }

    /// type is a ClipGraph::NodeType value; returns the new id, 0 on failure.
    Q_INVOKABLE quint64 addNode(int type, qreal x, qreal y, const QVariantMap &params = {});
    Q_INVOKABLE void removeNodes(const QVariantList &ids);
    /// Consecutive moves of one node collapse into a single undo step until endMove().
    Q_INVOKABLE void moveNode(quint64 id, qreal x, qreal y);
    Q_INVOKABLE void endMove();
    Q_INVOKABLE bool setParams(quint64 id, const QVariantMap &params);

    Q_INVOKABLE bool beginConnection(quint64 node, bool isOutput, int port);
    Q_INVOKABLE bool canConnect(quint64 node, bool isOutput, int port) const;
    Q_INVOKABLE bool finishConnection(quint64 node, bool isOutput, int port);
    Q_INVOKABLE void cancelConnection();
    Q_INVOKABLE bool disconnectEdge(quint64 edgeId);
    Q_INVOKABLE bool insertIntoEdge(quint64 node, quint64 edgeId);

    Q_INVOKABLE void select(const QVariantList &ids, bool additive = false);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(quint64 id) const { return m_selection.contains(id); }

    Q_INVOKABLE void selectAll();

    /// coords is a flat [x0, y0, x1, y1, ...] list parallel to ids. Consecutive calls with the
    /// same ids collapse into one undo step until endMove().
    Q_INVOKABLE void moveNodes(const QVariantList &ids, const QVariantList &coords);
    /// Reorders two slots of a Layer / A/B Select / Audio Mixer and the edges wired to them.
    Q_INVOKABLE bool swapSlots(quint64 id, int a, int b);
    /// Assigns the producer wired into an A/B Select slot to deck A or B.
    Q_INVOKABLE bool triggerAbSlot(quint64 node, int slot, bool deckA);
    /// {a, b, outputConnected} for an A/B Select node; a/b are slot indices or -1.
    Q_INVOKABLE QVariantMap abState(quint64 node) const;

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearHistory();

signals:
    void selectionChanged();
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
    void pushSnapshot(const QString &text, const QJsonObject &before);

    ClipGraph *m_graph = nullptr;
    ClipGraphNodesModel *m_nodes = nullptr;
    ClipGraphEdgesModel *m_edges = nullptr;
    QUndoStack m_undo;
    QSet<NodeId> m_selection;
    Pending m_pending;
    bool m_mergeMoves = false;
    int m_revision = 0;
};
