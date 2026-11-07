#include "core/graph/ClipGraphModels.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QElapsedTimer>
#include <QSet>
#include <QUndoCommand>

namespace {

class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(ClipGraph *graph, const QString &text, const QJsonObject &before,
                    const QJsonObject &after)
        : QUndoCommand(text), m_graph(graph), m_before(before), m_after(after) {}

    void undo() override { m_graph->fromJson(m_before); }
    void redo() override {
        if (m_applied) m_graph->fromJson(m_after);
        m_applied = true;
    }

private:
    ClipGraph *m_graph;
    QJsonObject m_before;
    QJsonObject m_after;
    bool m_applied = false;
};

class MoveCommand : public QUndoCommand {
public:
    MoveCommand(ClipGraph *graph, NodeId id, const QPointF &from, const QPointF &to,
                const bool *mergeFlag)
        : QUndoCommand(QStringLiteral("Move node")), m_graph(graph), m_id(id), m_from(from),
          m_to(to), m_mergeFlag(mergeFlag) {}

    int id() const override { return 1; }
    void undo() override { m_graph->moveNode(m_id, m_from); }
    void redo() override { m_graph->moveNode(m_id, m_to); }
    bool mergeWith(const QUndoCommand *other) override {
        if (!*m_mergeFlag || other->id() != id()) return false;
        const auto *o = static_cast<const MoveCommand *>(other);
        if (o->m_id != m_id) return false;
        m_to = o->m_to;
        return true;
    }

private:
    ClipGraph *m_graph;
    NodeId m_id;
    QPointF m_from;
    QPointF m_to;
    const bool *m_mergeFlag;
};

class MultiMoveCommand : public QUndoCommand {
public:
    struct Entry { NodeId id; QPointF from; QPointF to; };

    MultiMoveCommand(ClipGraph *graph, const QVector<Entry> &entries, const bool *mergeFlag)
        : QUndoCommand(QStringLiteral("Move nodes")), m_graph(graph), m_entries(entries),
          m_mergeFlag(mergeFlag) {}

    int id() const override { return 2; }
    void undo() override {
        for (const Entry &e : std::as_const(m_entries)) m_graph->moveNode(e.id, e.from);
    }
    void redo() override {
        for (const Entry &e : std::as_const(m_entries)) m_graph->moveNode(e.id, e.to);
    }
    bool mergeWith(const QUndoCommand *other) override {
        if (!*m_mergeFlag || other->id() != id()) return false;
        const auto *o = static_cast<const MultiMoveCommand *>(other);
        if (o->m_entries.size() != m_entries.size()) return false;
        for (qsizetype i = 0; i < m_entries.size(); ++i)
            if (m_entries[i].id != o->m_entries[i].id) return false;
        for (qsizetype i = 0; i < m_entries.size(); ++i) m_entries[i].to = o->m_entries[i].to;
        return true;
    }

private:
    ClipGraph *m_graph;
    QVector<Entry> m_entries;
    const bool *m_mergeFlag;
};

class ParamsCommand : public QUndoCommand {
public:
    ParamsCommand(ClipGraph *graph, NodeId id, const QVariantMap &before, const QVariantMap &after,
                  bool coalesce)
        : QUndoCommand(QStringLiteral("Edit node")), m_graph(graph), m_id(id), m_before(before),
          m_after(after), m_coalesce(coalesce) {
        for (auto it = after.begin(); it != after.end(); ++it)
            if (before.value(it.key()) != it.value()) m_keys.insert(it.key());
        m_clock.start();
    }

    void undo() override { m_graph->setParams(m_id, m_before, false); }
    void redo() override { m_graph->setParams(m_id, m_after, false); }

    // A slider drag commits every step; callers that ask to coalesce fold those into one entry.
    int id() const override { return m_coalesce ? 3 : -1; }
    bool mergeWith(const QUndoCommand *other) override {
        const auto *o = static_cast<const ParamsCommand *>(other);
        if (o->m_id != m_id || o->m_keys != m_keys || m_clock.elapsed() > 600) return false;
        m_after = o->m_after;
        m_clock.restart();
        return true;
    }

private:
    ClipGraph *m_graph;
    NodeId m_id;
    QVariantMap m_before;
    QVariantMap m_after;
    bool m_coalesce;
    QSet<QString> m_keys;
    QElapsedTimer m_clock;
};

}  // namespace

// ── Nodes model ─────────────────────────────────────────────────────────────

ClipGraphNodesModel::ClipGraphNodesModel(ClipGraph *graph, QObject *parent)
    : QAbstractListModel(parent), m_graph(graph), m_ids(graph->nodeIds())
{
    connect(graph, &ClipGraph::nodeAdded, this, [this](NodeId id) {
        beginInsertRows({}, int(m_ids.size()), int(m_ids.size()));
        m_ids.append(id);
        endInsertRows();
    });
    connect(graph, &ClipGraph::nodeRemoved, this, [this](NodeId id) {
        const int row = rowOfNode(id);
        if (row < 0) return;
        beginRemoveRows({}, row, row);
        m_ids.removeAt(row);
        endRemoveRows();
    });
    connect(graph, &ClipGraph::nodeChanged, this, [this](NodeId id) {
        const int row = rowOfNode(id);
        if (row >= 0) emit dataChanged(index(row), index(row));
    });
    connect(graph, &ClipGraph::edgeAdded, this, [this] { emitPortsChanged(); });
    connect(graph, &ClipGraph::edgeRemoved, this, [this] { emitPortsChanged(); });
    connect(graph, &ClipGraph::edgeChanged, this, [this] { emitPortsChanged(); });
    connect(graph, &ClipGraph::graphReset, this, [this] {
        beginResetModel();
        m_ids = m_graph->nodeIds();
        endResetModel();
    });
}

void ClipGraphNodesModel::emitPortsChanged() {
    if (m_ids.isEmpty()) return;
    emit dataChanged(index(0), index(int(m_ids.size()) - 1), {InputPortsRole, OutputPortsRole});
}

int ClipGraphNodesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_ids.size());
}

int ClipGraphNodesModel::rowOfNode(quint64 id) const {
    return int(m_ids.indexOf(id));
}

QVariantList ClipGraphNodesModel::portList(const QVector<ClipGraph::Port> &ports, NodeId id) const {
    QVariantList out;
    for (const ClipGraph::Port &p : ports) {
        out.append(QVariantMap{
            {"kind", int(p.kind)},
            {"direction", int(p.direction)},
            {"index", p.index},
            {"label", p.label},
            {"slotted", p.slotted},
            {"x", p.pos.x()},
            {"y", p.pos.y()},
            {"connected", m_graph->isPortConnected(id, p.direction, p.index)},
        });
    }
    return out;
}

QVariant ClipGraphNodesModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_ids.size() || !m_graph) return {};
    const NodeId id = m_ids[idx.row()];
    const ClipGraph::Node *n = m_graph->node(id);
    if (!n) return {};
    switch (role) {
    case NodeIdRole:      return QVariant::fromValue<quint64>(id);
    case TypeRole:        return int(n->type);
    case XRole:           return n->pos.x();
    case YRole:           return n->pos.y();
    case WidthRole:       return n->size.width();
    case HeightRole:      return n->size.height();
    case TitleRole:       return n->title;
    case ParamsRole: {
        if (n->type != ClipGraph::NodeType::Input) return n->params;
        const SourceDescriptor desc = m_graph->sourceDescriptor(id);
        QVariantMap params = n->params;
        params.insert(QStringLiteral("repeatable"), desc.isRepeatable());
        params.insert(QStringLiteral("seekable"), desc.isSeekable());
        params.insert(QStringLiteral("pausable"), desc.isPausable());
        params.insert(QStringLiteral("speedControl"), desc.hasSpeedControl());
        return params;
    }
    case InputPortsRole:  return portList(m_graph->inputPorts(id), id);
    case OutputPortsRole: return portList(m_graph->outputPorts(id), id);
    }
    return {};
}

QHash<int, QByteArray> ClipGraphNodesModel::roleNames() const {
    return {
        {NodeIdRole, "nodeId"},
        {TypeRole, "type"},
        {XRole, "x"},
        {YRole, "y"},
        {WidthRole, "width"},
        {HeightRole, "height"},
        {TitleRole, "title"},
        {ParamsRole, "params"},
        {InputPortsRole, "inputPorts"},
        {OutputPortsRole, "outputPorts"},
    };
}

// ── Edges model ─────────────────────────────────────────────────────────────

ClipGraphEdgesModel::ClipGraphEdgesModel(ClipGraph *graph, QObject *parent)
    : QAbstractListModel(parent), m_graph(graph)
{
    for (const ClipGraph::Edge &e : graph->edges())
        m_ids.append(e.id);

    connect(graph, &ClipGraph::edgeAdded, this, [this](EdgeId id) {
        beginInsertRows({}, int(m_ids.size()), int(m_ids.size()));
        m_ids.append(id);
        endInsertRows();
    });
    connect(graph, &ClipGraph::edgeRemoved, this, [this](EdgeId id) {
        const int row = rowOfEdge(id);
        if (row < 0) return;
        beginRemoveRows({}, row, row);
        m_ids.removeAt(row);
        endRemoveRows();
    });
    connect(graph, &ClipGraph::edgeChanged, this, [this](EdgeId id) {
        const int row = rowOfEdge(id);
        if (row >= 0) emit dataChanged(index(row), index(row));
    });
    connect(graph, &ClipGraph::graphReset, this, [this] {
        beginResetModel();
        m_ids.clear();
        for (const ClipGraph::Edge &e : m_graph->edges())
            m_ids.append(e.id);
        endResetModel();
    });
}

int ClipGraphEdgesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_ids.size());
}

int ClipGraphEdgesModel::rowOfEdge(quint64 id) const {
    return int(m_ids.indexOf(id));
}

QVariant ClipGraphEdgesModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_ids.size() || !m_graph) return {};
    const ClipGraph::Edge *e = m_graph->edge(m_ids[idx.row()]);
    if (!e) return {};
    switch (role) {
    case EdgeIdRole:   return QVariant::fromValue<quint64>(e->id);
    case FromNodeRole: return QVariant::fromValue<quint64>(e->from);
    case FromPortRole: return e->fromPort;
    case ToNodeRole:   return QVariant::fromValue<quint64>(e->to);
    case ToPortRole:   return e->toPort;
    case KindRole:     return int(e->kind);
    }
    return {};
}

QHash<int, QByteArray> ClipGraphEdgesModel::roleNames() const {
    return {
        {EdgeIdRole, "edgeId"},
        {FromNodeRole, "fromNode"},
        {FromPortRole, "fromPort"},
        {ToNodeRole, "toNode"},
        {ToPortRole, "toPort"},
        {KindRole, "kind"},
    };
}

// ── Controller ──────────────────────────────────────────────────────────────

ClipGraphController::ClipGraphController(QObject *parent)
    : QObject(parent), m_graph(new ClipGraph(this))
{
    init();
}

ClipGraphController::ClipGraphController(ClipGraph *graph, QObject *parent)
    : QObject(parent), m_graph(graph)
{
    init();
}

void ClipGraphController::init() {
    m_nodes = new ClipGraphNodesModel(m_graph, this);
    m_edges = new ClipGraphEdgesModel(m_graph, this);
    connect(&m_undo, &QUndoStack::indexChanged, this, &ClipGraphController::undoStateChanged);
    connect(&m_undo, &QUndoStack::cleanChanged, this, &ClipGraphController::undoStateChanged);
    connect(m_graph, &ClipGraph::graphReset, this, [this] {
        const qsizetype before = m_selection.size();
        m_selection.removeIf([this](NodeId id) { return !m_graph->hasNode(id); });
        if (m_selection.size() != before) emit selectionChanged();
        if (m_pending.node != 0 && !m_graph->hasNode(m_pending.node)) cancelConnection();
    });
    connect(m_graph, &ClipGraph::nodeRemoved, this, [this](NodeId id) {
        if (m_selection.remove(id)) emit selectionChanged();
        if (m_pending.node == id) cancelConnection();
    });
    auto bump = [this] { ++m_revision; emit revisionChanged(); };
    connect(m_graph, &ClipGraph::changed, this, bump);
    connect(m_graph, &ClipGraph::graphReset, this, bump);
}

void ClipGraphController::pushSnapshot(const QString &text, const QJsonObject &before) {
    const QJsonObject after = m_graph->toJson();
    if (after == before) return;
    m_mergeMoves = false;
    m_undo.push(new SnapshotCommand(m_graph, text, before, after));
}

bool ClipGraphController::sceneRefAllowed(const QVariantMap &params) const {
    if (!m_sceneRefValidator) return true;
    const QVariantMap source = params.value("source").toMap();
    if (source.value("kind").toInt() != int(SourceDescriptor::Kind::Scene)) return true;
    const QString sceneId = source.value("sceneId").toString();
    return sceneId.isEmpty() || m_sceneRefValidator(sceneId);
}

quint64 ClipGraphController::addNode(int type, qreal x, qreal y, const QVariantMap &params) {
    if (!sceneRefAllowed(params)) return 0;
    const QJsonObject before = m_graph->toJson();
    const NodeId id = m_graph->addNode(ClipGraph::NodeType(type), QPointF(x, y), params);
    if (id != 0) pushSnapshot(QStringLiteral("Add node"), before);
    return id;
}

void ClipGraphController::removeNodes(const QVariantList &ids) {
    const QJsonObject before = m_graph->toJson();
    for (const QVariant &v : ids)
        m_graph->removeNode(v.toULongLong());
    pushSnapshot(QStringLiteral("Remove nodes"), before);
}

QVariantList ClipGraphController::modulators() const {
    QVariantList out;
    for (NodeId id : m_graph->nodeIdsOfType(ClipGraph::NodeType::Modulator)) {
        const ClipGraph::Node *n = m_graph->node(id);
        out.append(QVariantMap{{"id", QVariant::fromValue<quint64>(id)}, {"name", n->title},
                               {"label", QStringLiteral("%1 #%2").arg(n->title).arg(id & 0xFFFFFFFFFFull)},
                               {"kind", n->params.value("kind").toInt()}});
    }
    return out;
}

quint64 ClipGraphController::bindParam(quint64 node, const QString &param, quint64 modulator,
                                       qreal min, qreal max, int newKind) {
    const ClipGraph::Node *target = m_graph->node(node);
    if (!target || param.isEmpty()) return 0;
    const ClipGraph::Node *existing = modulator ? m_graph->node(modulator) : nullptr;
    if (modulator && (!existing || existing->type != ClipGraph::NodeType::Modulator)) return 0;
    const QJsonObject before = m_graph->toJson();
    if (!modulator) {
        modulator = m_graph->addNode(ClipGraph::NodeType::Modulator,
                                     target->pos + QPointF(-190.0, 0.0),
                                     {{"kind", newKind}, {"min", min}, {"max", max}});
        if (!modulator) return 0;
    }
    QVariantMap exprs = m_graph->node(node)->params.value("expressions").toMap();
    exprs.insert(param, QStringLiteral("mod(%1)").arg(modulator));
    m_graph->setParams(node, {{"expressions", exprs}}, true);
    pushSnapshot(QStringLiteral("Link parameter"), before);
    return modulator;
}

void ClipGraphController::moveNode(quint64 id, qreal x, qreal y) {
    const ClipGraph::Node *n = m_graph->node(id);
    if (!n || n->pos == QPointF(x, y)) return;
    m_undo.push(new MoveCommand(m_graph, id, n->pos, QPointF(x, y), &m_mergeMoves));
    m_mergeMoves = true;
}

void ClipGraphController::endMove() {
    m_mergeMoves = false;
}

bool ClipGraphController::setParams(quint64 id, const QVariantMap &params, bool coalesce) {
    const ClipGraph::Node *n = m_graph->node(id);
    if (!n) return false;
    if (!sceneRefAllowed(params)) return false;
    QVariantMap next = n->params;
    for (auto it = params.begin(); it != params.end(); ++it)
        next.insert(it.key(), it.value());
    if (next == n->params) return true;
    m_mergeMoves = false;
    m_undo.push(new ParamsCommand(m_graph, id, n->params, next, coalesce));
    return true;
}

bool ClipGraphController::setSource(quint64 id, const QVariantMap &descriptor) {
    const ClipGraph::Node *n = m_graph->node(id);
    if (!n || n->type != ClipGraph::NodeType::Input) return false;
    return setParams(id, {{"source", descriptor}});
}

bool ClipGraphController::setSourceDescriptor(quint64 id, const SourceDescriptor &desc) {
    return setSource(id, ClipGraph::sourceToMap(desc));
}

bool ClipGraphController::beginConnection(quint64 node, bool isOutput, int port) {
    const auto ports = isOutput ? m_graph->outputPorts(node) : m_graph->inputPorts(node);
    if (port < 0 || port >= ports.size()) return false;
    m_pending = {node, isOutput, port};
    emit connectingChanged();
    return true;
}

bool ClipGraphController::canConnect(quint64 node, bool isOutput, int port) const {
    if (m_pending.node == 0 || m_pending.output == isOutput) return false;
    return isOutput
        ? m_graph->canConnectPorts(node, port, m_pending.node, m_pending.port)
        : m_graph->canConnectPorts(m_pending.node, m_pending.port, node, port);
}

bool ClipGraphController::finishConnection(quint64 node, bool isOutput, int port) {
    bool ok = false;
    if (canConnect(node, isOutput, port)) {
        const QJsonObject before = m_graph->toJson();
        ok = isOutput
            ? m_graph->connectPorts(node, port, m_pending.node, m_pending.port)
            : m_graph->connectPorts(m_pending.node, m_pending.port, node, port);
        if (ok) pushSnapshot(QStringLiteral("Connect"), before);
    }
    cancelConnection();
    return ok;
}

void ClipGraphController::cancelConnection() {
    if (m_pending.node == 0) return;
    m_pending = {};
    emit connectingChanged();
}

bool ClipGraphController::disconnectEdge(quint64 edgeId) {
    const QJsonObject before = m_graph->toJson();
    const bool ok = m_graph->disconnectEdge(edgeId);
    if (ok) pushSnapshot(QStringLiteral("Disconnect"), before);
    return ok;
}

bool ClipGraphController::insertIntoEdge(quint64 node, quint64 edgeId) {
    const QJsonObject before = m_graph->toJson();
    const bool ok = m_graph->insertIntoEdge(node, edgeId);
    if (ok) pushSnapshot(QStringLiteral("Insert node"), before);
    return ok;
}

bool ClipGraphController::canInsertIntoEdge(quint64 node, quint64 edgeId) const {
    return m_graph->canInsertIntoEdge(node, edgeId);
}

quint64 ClipGraphController::addNodeIntoEdge(int type, qreal x, qreal y, const QVariantMap &params,
                                             quint64 edgeId) {
    if (!sceneRefAllowed(params)) return 0;
    const QJsonObject before = m_graph->toJson();
    const NodeId id = m_graph->addNode(ClipGraph::NodeType(type), QPointF(x, y), params);
    if (id != 0 && edgeId != 0) m_graph->insertIntoEdge(id, edgeId);
    if (id != 0) pushSnapshot(QStringLiteral("Add node"), before);
    return id;
}

quint64 ClipGraphController::addNodeConnected(int type, qreal x, qreal y, const QVariantMap &params) {
    if (m_pending.node == 0) return addNode(type, x, y, params);
    if (!sceneRefAllowed(params)) {
        cancelConnection();
        return 0;
    }
    const QJsonObject before = m_graph->toJson();
    const NodeId id = m_graph->addNode(ClipGraph::NodeType(type), QPointF(x, y), params);
    if (id != 0) {
        const auto ports = m_pending.output ? m_graph->inputPorts(id) : m_graph->outputPorts(id);
        for (const ClipGraph::Port &p : ports) {
            const bool ok = m_pending.output
                ? m_graph->canConnectPorts(m_pending.node, m_pending.port, id, p.index)
                : m_graph->canConnectPorts(id, p.index, m_pending.node, m_pending.port);
            if (!ok) continue;
            if (m_pending.output)
                m_graph->connectPorts(m_pending.node, m_pending.port, id, p.index);
            else
                m_graph->connectPorts(id, p.index, m_pending.node, m_pending.port);
            break;
        }
        pushSnapshot(QStringLiteral("Add node"), before);
    }
    cancelConnection();
    return id;
}

bool ClipGraphController::detachNode(quint64 node) {
    const QJsonObject before = m_graph->toJson();
    const bool ok = m_graph->detachNode(node);
    if (ok) pushSnapshot(QStringLiteral("Detach node"), before);
    return ok;
}

namespace {
constexpr auto kClipboardMime = "application/x-prism-nodes";
}

bool ClipGraphController::copySelection() {
    QList<NodeId> ids;
    for (const QVariant &v : selection()) ids.append(v.toULongLong());
    const QJsonObject clip = m_graph->copyNodes(ids);
    if (clip["nodes"].toArray().isEmpty()) return false;
    m_clipboard = clip;
    if (qGuiApp) {
        auto *mime = new QMimeData;
        mime->setData(kClipboardMime, QJsonDocument(clip).toJson(QJsonDocument::Compact));
        QGuiApplication::clipboard()->setMimeData(mime);
    }
    return true;
}

QVariantList ClipGraphController::pasteDocument(const QJsonObject &clip, const QPointF &delta,
                                                const QString &text) {
    const QJsonObject before = m_graph->toJson();
    const QList<NodeId> created = m_graph->pasteNodes(clip, delta);
    QVariantList out;
    for (NodeId id : created) out.append(QVariant::fromValue<quint64>(id));
    if (created.isEmpty()) return out;
    pushSnapshot(text, before);
    select(out);
    return out;
}

QVariantList ClipGraphController::paste(qreal x, qreal y) {
    QJsonObject clip = m_clipboard;
    if (qGuiApp) {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        if (mime && mime->hasFormat(kClipboardMime)) {
            const QJsonObject fromSystem = QJsonDocument::fromJson(mime->data(kClipboardMime)).object();
            if (!fromSystem.isEmpty()) clip = fromSystem;
        }
    }
    if (clip["nodes"].toArray().isEmpty()) return {};
    const QPointF delta = qIsNaN(x) || qIsNaN(y)
        ? QPointF(40, 40)
        : QPointF(x - clip["originX"].toDouble(), y - clip["originY"].toDouble());
    return pasteDocument(clip, delta, QStringLiteral("Paste"));
}

QVariantList ClipGraphController::duplicateSelection() {
    QList<NodeId> ids;
    for (const QVariant &v : selection()) ids.append(v.toULongLong());
    const QJsonObject clip = m_graph->copyNodes(ids);
    if (clip["nodes"].toArray().isEmpty()) return {};
    return pasteDocument(clip, QPointF(40, 40), QStringLiteral("Duplicate"));
}

QVariantList ClipGraphController::selection() const {
    QList<NodeId> sorted = m_selection.values();
    std::sort(sorted.begin(), sorted.end());
    QVariantList out;
    for (NodeId id : std::as_const(sorted))
        out.append(QVariant::fromValue<quint64>(id));
    return out;
}

void ClipGraphController::select(const QVariantList &ids, bool additive) {
    QSet<NodeId> next = additive ? m_selection : QSet<NodeId>{};
    for (const QVariant &v : ids)
        if (m_graph->hasNode(v.toULongLong())) next.insert(v.toULongLong());
    if (next == m_selection) return;
    m_selection = next;
    emit selectionChanged();
}

void ClipGraphController::clearSelection() {
    if (m_selection.isEmpty()) return;
    m_selection.clear();
    emit selectionChanged();
}

void ClipGraphController::undo() {
    m_mergeMoves = false;
    m_undo.undo();
}

void ClipGraphController::redo() {
    m_mergeMoves = false;
    m_undo.redo();
}

void ClipGraphController::clearHistory() {
    m_undo.clear();
}

void ClipGraphController::selectAll() {
    QSet<NodeId> next;
    for (NodeId id : m_graph->nodeIds()) next.insert(id);
    if (next == m_selection) return;
    m_selection = next;
    emit selectionChanged();
}

void ClipGraphController::moveNodes(const QVariantList &ids, const QVariantList &coords) {
    QVector<MultiMoveCommand::Entry> entries;
    for (qsizetype i = 0; i < ids.size() && i * 2 + 1 < coords.size(); ++i) {
        const NodeId id = ids[i].toULongLong();
        const ClipGraph::Node *n = m_graph->node(id);
        if (!n) continue;
        const QPointF to(coords[i * 2].toReal(), coords[i * 2 + 1].toReal());
        if (n->pos != to) entries.append({id, n->pos, to});
    }
    if (entries.isEmpty()) return;
    m_undo.push(new MultiMoveCommand(m_graph, entries, &m_mergeMoves));
    m_mergeMoves = true;
}

bool ClipGraphController::swapSlots(quint64 id, int a, int b) {
    const ClipGraph::Node *n = m_graph->node(id);
    if (!n || a == b || a < 0 || b < 0) return false;
    const char *key = nullptr;
    switch (n->type) {
    case ClipGraph::NodeType::Layer:      key = "layerNodes"; break;
    case ClipGraph::NodeType::AudioMixer: key = "audioMixerNodes"; break;
    default: return false;
    }
    const QJsonObject before = m_graph->toJson();
    QJsonObject state = before;
    QJsonArray nodes = state[key].toArray();
    bool swapped = false;
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        QJsonObject o = nodes[i].toObject();
        if (quint64(o["id"].toInteger()) != id) continue;
        QJsonArray slotList = o["slots"].toArray();
        if (a >= slotList.size() || b >= slotList.size()) return false;
        const QJsonValue tmp = slotList[a];
        slotList[a] = slotList[b];
        slotList[b] = tmp;
        o["slots"] = slotList;
        nodes[i] = o;
        swapped = true;
        break;
    }
    if (!swapped) return false;
    state[key] = nodes;
    QJsonArray conns = state["connections"].toArray();
    for (qsizetype i = 0; i < conns.size(); ++i) {
        QJsonObject c = conns[i].toObject();
        if (quint64(c["to"].toInteger()) != id) continue;
        const int port = c["toPortIndex"].toInt(-1);
        if (port == a) c["toPortIndex"] = b;
        else if (port == b) c["toPortIndex"] = a;
        else continue;
        conns[i] = c;
    }
    state["connections"] = conns;
    m_graph->fromJson(state);
    pushSnapshot(QStringLiteral("Reorder"), before);
    return true;
}
