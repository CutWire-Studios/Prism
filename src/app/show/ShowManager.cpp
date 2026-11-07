#include "app/show/ShowManager.h"

#include "app/graph/GraphRuntime.h"
#include "app/show/SceneListModel.h"
#include "core/graph/ClipGraph.h"
#include "core/graph/ClipGraphModels.h"

#include <QDebug>
#include <QJsonArray>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUuid>

#include <algorithm>

namespace {

QJsonObject shiftIds(QJsonObject graph, qint64 delta)
{
    auto shiftObj = [delta](QJsonObject o, std::initializer_list<const char *> keys) {
        for (const char *k : keys) {
            if (o.contains(QLatin1String(k)))
                o[QLatin1String(k)] = o.value(QLatin1String(k)).toInteger() + delta;
        }
        return o;
    };
    for (auto it = graph.begin(); it != graph.end(); ++it) {
        if (it.key() == QLatin1String("outputNode")) {
            it.value() = shiftObj(it.value().toObject(), {"id"});
        } else if (it.key() == QLatin1String("connections")) {
            QJsonArray arr = it.value().toArray();
            for (qsizetype i = 0; i < arr.size(); ++i)
                arr[i] = shiftObj(arr.at(i).toObject(), {"from", "to"});
            it.value() = arr;
        } else if (it.key() == QLatin1String("nextId")) {
            it.value() = it.value().toInteger() + delta;
        } else if (it.value().isArray()) {
            QJsonArray arr = it.value().toArray();
            for (qsizetype i = 0; i < arr.size(); ++i)
                arr[i] = shiftObj(arr.at(i).toObject(), {"id"});
            it.value() = arr;
        }
    }
    return graph;
}

QStringList sceneRefsOf(const Scene *scene)
{
    QStringList refs;
    const ClipGraph *g = scene->graph();
    for (const NodeId id : g->nodeIdsOfType(ClipGraph::NodeType::Input)) {
        const SourceDescriptor d = g->sourceDescriptor(id);
        if (d.kind == SourceDescriptor::Kind::Scene && !d.sceneId.isEmpty())
            refs.append(d.sceneId);
    }
    return refs;
}

const Scene *findScene(const QList<Scene *> &scenes, const QString &id)
{
    for (const Scene *c : scenes) {
        if (c->id() == id)
            return c;
    }
    return nullptr;
}

/// True when @p goal is reachable from @p from (inclusive) through Scene nodes.
bool reaches(const QList<Scene *> &scenes, const QString &from, const QString &goal)
{
    QSet<QString> seen;
    QStringList stack{from};
    while (!stack.isEmpty()) {
        const QString id = stack.takeLast();
        if (id == goal)
            return true;
        if (seen.contains(id))
            continue;
        seen.insert(id);
        if (const Scene *c = findScene(scenes, id))
            stack.append(sceneRefsOf(c));
    }
    return false;
}

/// Clears the Scene nodes whose target leads back to their own scene; returns how many.
int breakSceneCycles(const QList<Scene *> &scenes)
{
    int cleared = 0;
    for (Scene *c : scenes) {
        ClipGraph *g = c->graph();
        for (const NodeId id : g->nodeIdsOfType(ClipGraph::NodeType::Input)) {
            SourceDescriptor d = g->sourceDescriptor(id);
            if (d.kind != SourceDescriptor::Kind::Scene || d.sceneId.isEmpty() || !reaches(scenes, d.sceneId, c->id()))
                continue;
            qWarning().noquote() << "Scene" << c->name() << "node" << id << "referenced scene" << d.sceneId
                                 << "which contains it; reference cleared";
            d.sceneId.clear();
            g->setSourceDescriptor(id, d);
            ++cleared;
        }
    }
    return cleared;
}

} // namespace

/// Inserts or removes a scene by snapshot. Add/duplicate push it already applied (first redo is
/// skipped); undoing them removes the scene, redoing re-creates it with the same id and graph ids.
class SceneStructureCommand : public QUndoCommand {
public:
    SceneStructureCommand(ShowManager *show, bool insert, const QString &id, const QJsonObject &snap, int index,
                        bool applied, const QString &text)
        : QUndoCommand(text), m_show(show), m_insert(insert), m_id(id), m_snap(snap), m_index(index), m_skip(applied) {}

    void redo() override
    {
        if (m_skip) {
            m_skip = false;
            return;
        }
        apply(m_insert);
    }
    void undo() override { apply(!m_insert); }

private:
    void apply(bool insert)
    {
        if (insert) {
            m_show->restoreScene(m_snap, m_index);
            return;
        }
        if (const Scene *c = m_show->scene(m_id)) {
            m_snap = c->toJson();
            m_index = m_show->indexOf(m_id);
            m_show->eraseScene(m_id);
        }
    }

    ShowManager *m_show;
    bool m_insert;
    QString m_id;
    QJsonObject m_snap;
    int m_index;
    bool m_skip;
};

class MoveSceneCommand : public QUndoCommand {
public:
    MoveSceneCommand(ShowManager *show, int from, int to)
        : QUndoCommand(QObject::tr("Move scene")), m_show(show), m_from(from), m_to(to) {}
    void redo() override { m_show->moveSceneRaw(m_from, m_to); }
    void undo() override { m_show->moveSceneRaw(m_to, m_from); }

private:
    ShowManager *m_show;
    int m_from, m_to;
};

namespace {

class SceneMetaCommand : public QUndoCommand {
public:
    SceneMetaCommand(ShowManager *show, const QString &id, const SceneMeta &before, const SceneMeta &after)
        : QUndoCommand(QObject::tr("Edit scene")), m_show(show), m_id(id), m_before(before), m_after(after) {}
    void redo() override { apply(m_after); }
    void undo() override { apply(m_before); }

private:
    void apply(const SceneMeta &m)
    {
        if (Scene *c = m_show->scene(m_id))
            c->setMeta(m);
    }
    ShowManager *m_show;
    QString m_id;
    SceneMeta m_before, m_after;
};

} // namespace

ShowManager::ShowManager(QObject *parent)
    : QObject(parent)
    , m_model(new SceneListModel(this))
    , m_thumbs(GraphRuntime::createThumbStore())
    , m_endTimer(new QTimer(this))
    , m_undo(new QUndoStack(this))
{
    m_endTimer->setSingleShot(true);
    connect(m_endTimer, &QTimer::timeout, this, &ShowManager::notifyLiveEnded);
    connect(this, &ShowManager::liveSceneChanged, this, &ShowManager::rearmEndTimer);
    reset();
}

ShowManager::~ShowManager() = default;

Scene *ShowManager::scene(const QString &id) const
{
    for (Scene *c : m_scenes) {
        if (c->id() == id)
            return c;
    }
    return nullptr;
}

int ShowManager::indexOf(const QString &id) const
{
    for (int i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes.at(i)->id() == id)
            return i;
    }
    return -1;
}

Scene *ShowManager::sceneForNode(quint64 nodeId) const
{
    for (Scene *c : m_scenes) {
        if (c->graph()->hasNode(nodeId))
            return c;
    }
    return nullptr;
}

Scene *ShowManager::nextScene(const QString &id) const
{
    const int i = indexOf(id);
    return i >= 0 && i + 1 < m_scenes.size() ? m_scenes.at(i + 1) : nullptr;
}

Scene *ShowManager::makeScene(const QString &id, int serial, bool withDefaults)
{
    auto *c = new Scene(id, serial, m_thumbs, withDefaults, this);
    c->graph()->setOutputCanvasSize(m_canvasW, m_canvasH);
    c->controller()->setSceneRefValidator(
        [this, id](const QString &target) { return !wouldCreateSceneCycle(id, target); });
    connect(c, &Scene::metaChanged, this, &ShowManager::changed);
    connect(c, &Scene::metaChanged, this, [this, c] { emit sceneMetaChanged(c); });
    connect(c, &Scene::thumbnailChanged, this, &ShowManager::thumbnailChanged);
    connect(c->graph(), &ClipGraph::changed, this, &ShowManager::changed);
    m_nextSerial = std::max(m_nextSerial, serial + 1);
    return c;
}

Scene *ShowManager::addScene(const QString &name, int index)
{
    Scene *c = makeScene(QUuid::createUuid().toString(QUuid::WithoutBraces), m_nextSerial, true);
    c->setName(name.isEmpty() ? tr("Scene %1").arg(c->serial()) : name);
    m_scenes.insert(index < 0 || index > m_scenes.size() ? m_scenes.size() : index, c);
    emit sceneAdded(c);
    updateRuntimeActivity();
    emit changed();
    m_undo->push(new SceneStructureCommand(this, true, c->id(), {}, indexOf(c->id()), true, tr("Add scene")));
    return c;
}

Scene *ShowManager::duplicateScene(const QString &id)
{
    const Scene *src = scene(id);
    if (!src)
        return nullptr;
    Scene *c = makeScene(QUuid::createUuid().toString(QUuid::WithoutBraces), m_nextSerial, false);
    QJsonObject state = src->toJson();
    state[QStringLiteral("graph")] = shiftIds(state.value(QStringLiteral("graph")).toObject(),
                                              (qint64(c->serial()) - src->serial()) << 40);
    c->loadJson(state);
    c->setName(tr("%1 copy").arg(src->name()));
    m_scenes.insert(indexOf(id) + 1, c);
    emit sceneAdded(c);
    updateRuntimeActivity();
    emit changed();
    m_undo->push(new SceneStructureCommand(this, true, c->id(), {}, indexOf(c->id()), true, tr("Duplicate scene")));
    return c;
}

bool ShowManager::removeScene(const QString &id)
{
    Scene *c = scene(id);
    if (!c || id == m_liveId || m_scenes.size() <= 1)
        return false;
    m_undo->push(new SceneStructureCommand(this, false, id, c->toJson(), indexOf(id), false, tr("Remove scene")));
    return true;
}

Scene *ShowManager::restoreScene(const QJsonObject &snapshot, int index)
{
    Scene *c = makeScene(snapshot.value(QStringLiteral("id")).toString(),
                     snapshot.value(QStringLiteral("serial")).toInt(), false);
    c->loadJson(snapshot);
    m_scenes.insert(qBound(0, index, int(m_scenes.size())), c);
    emit sceneAdded(c);
    updateRuntimeActivity();
    emit changed();
    return c;
}

bool ShowManager::eraseScene(const QString &id)
{
    Scene *c = scene(id);
    if (!c || id == m_liveId || m_scenes.size() <= 1)
        return false;
    emit sceneAboutToBeRemoved(c);
    m_scenes.removeOne(c);
    if (m_stagingId == id) {
        m_stagingId.clear();
        emit stagingSceneChanged();
    }
    if (m_editedId == id) {
        m_editedId = m_liveId;
        emit editedSceneChanged();
    }
    delete c;
    emit scenesReset();
    emit changed();
    return true;
}

bool ShowManager::moveScene(int from, int to)
{
    if (from < 0 || from >= m_scenes.size() || to < 0 || to >= m_scenes.size() || from == to)
        return false;
    m_undo->push(new MoveSceneCommand(this, from, to));
    return true;
}

void ShowManager::setSceneMeta(const QString &id, const SceneMeta &meta)
{
    Scene *c = scene(id);
    if (!c || c->meta() == meta)
        return;
    m_undo->push(new SceneMetaCommand(this, id, c->meta(), meta));
}

bool ShowManager::moveSceneRaw(int from, int to)
{
    if (from < 0 || from >= m_scenes.size() || to < 0 || to >= m_scenes.size() || from == to)
        return false;
    m_scenes.move(from, to);
    emit scenesReset();
    emit changed();
    return true;
}

void ShowManager::reset()
{
    m_undo->clear();
    for (Scene *c : std::as_const(m_scenes))
        emit sceneAboutToBeRemoved(c);
    qDeleteAll(m_scenes);
    m_scenes.clear();
    m_nextSerial = 1;
    m_default = {};
    Scene *c = makeScene(QUuid::createUuid().toString(QUuid::WithoutBraces), m_nextSerial, true);
    c->setName(tr("Scene %1").arg(c->serial()));
    m_scenes.append(c);
    m_liveId = c->id();
    m_stagingId.clear();
    m_editedId = c->id();
    m_inScene = true;
    emit scenesReset();
    emit sceneAdded(c);
    emit liveSceneChanged();
    emit stagingSceneChanged();
    emit editedSceneChanged();
    emit defaultTransitionChanged();
    updateRuntimeActivity();
    emit changed();
}

bool ShowManager::stageScene(const QString &id)
{
    if (!id.isEmpty() && !scene(id))
        return false;
    if (m_stagingId == id)
        return true;
    m_stagingId = id;
    emit stagingSceneChanged();
    updateRuntimeActivity();
    return true;
}

bool ShowManager::setLiveScene(const QString &id)
{
    if (!scene(id))
        return false;
    if (m_stagingId == id) {
        m_stagingId.clear();
        emit stagingSceneChanged();
    }
    if (m_liveId != id) {
        m_liveId = id;
        emit liveSceneChanged();
    }
    updateRuntimeActivity();
    return true;
}

void ShowManager::swapLiveStaging()
{
    if (m_stagingId.isEmpty())
        return;
    std::swap(m_liveId, m_stagingId);
    emit liveSceneChanged();
    emit stagingSceneChanged();
    updateRuntimeActivity();
    emit changed();
}

bool ShowManager::enterScene(const QString &id)
{
    if (!scene(id))
        return false;
    m_editedId = id;
    m_inScene = true;
    emit editedSceneChanged();
    updateRuntimeActivity();
    return true;
}

void ShowManager::exitScene()
{
    if (!m_inScene)
        return;
    m_inScene = false;
    emit editedSceneChanged();
    emit changed();
}

void ShowManager::setDefaultTransition(const TransitionSpec &spec)
{
    if (m_default == spec)
        return;
    m_default = spec;
    emit defaultTransitionChanged();
    emit changed();
}

TransitionSpec ShowManager::resolveTransition(const Scene *staging) const
{
    return staging && staging->incoming() ? *staging->incoming() : m_default;
}

void ShowManager::rearmEndTimer()
{
    m_endTimer->stop();
    const Scene *live = liveScene();
    if (live && live->autoAdvanceMs() > 0
        && (live->endAction() == Scene::EndAction::AutoAdvance || live->endAction() == Scene::EndAction::Black))
        m_endTimer->start(live->autoAdvanceMs());
}

void ShowManager::notifyLiveEnded()
{
    emit liveEnded();
}

void ShowManager::notifyLiveMediaEnded()
{
    const Scene *live = liveScene();
    if (live && live->advanceOnMediaEnd()
        && (live->endAction() == Scene::EndAction::AutoAdvance || live->endAction() == Scene::EndAction::Black)) {
        m_endTimer->stop();
        emit liveEnded();
    }
}

void ShowManager::setCanvasSize(int w, int h)
{
    m_canvasW = w;
    m_canvasH = h;
    for (Scene *c : std::as_const(m_scenes))
        c->graph()->setOutputCanvasSize(w, h);
}

QQuickImageProvider *ShowManager::createImageProvider() const
{
    return GraphRuntime::createImageProvider(m_thumbs);
}

QStringList ShowManager::sceneReferences(const QString &id) const
{
    const Scene *c = scene(id);
    return c ? sceneRefsOf(c) : QStringList();
}

bool ShowManager::wouldCreateSceneCycle(const QString &host, const QString &target) const
{
    return reaches(m_scenes, target, host);
}

QSet<QString> ShowManager::activeSceneIds(bool includeEdited) const
{
    QSet<QString> active;
    QStringList stack{m_liveId, m_stagingId};
    if (includeEdited && m_inScene)
        stack.append(m_editedId);
    while (!stack.isEmpty()) {
        const QString id = stack.takeLast();
        const Scene *c = id.isEmpty() ? nullptr : scene(id);
        if (!c || active.contains(id))
            continue;
        active.insert(id);
        stack.append(sceneRefsOf(c));
    }
    return active;
}

void ShowManager::updateRuntimeActivity()
{
    const QSet<QString> active = activeSceneIds();
    for (Scene *c : std::as_const(m_scenes))
        c->runtime()->setActive(active.contains(c->id()));
}

QJsonObject ShowManager::toJson() const
{
    QJsonArray scenes;
    for (const Scene *c : m_scenes)
        scenes.append(c->toJson());
    return {
        {QStringLiteral("show"), QJsonObject{
            {QStringLiteral("defaultTransition"), m_default.toJson()},
            {QStringLiteral("state"), QJsonObject{
                {QStringLiteral("liveSceneId"), m_liveId},
                {QStringLiteral("stagingSceneId"), m_stagingId},
                {QStringLiteral("editedSceneId"), m_editedId},
            }},
        }},
        {QStringLiteral("scenes"), scenes},
    };
}

bool ShowManager::fromJson(const QJsonObject &root)
{
    const QJsonArray sceneArr = root.value(QStringLiteral("scenes")).toArray();
    if (sceneArr.isEmpty()) {
        reset();
        return false;
    }

    QList<Scene *> loaded;
    int maxSerial = 0;
    bool ok = true;
    for (const QJsonValue &v : sceneArr) {
        const QJsonObject o = v.toObject();
        const int serial = o.value(QStringLiteral("serial")).toInt(0);
        if (serial <= 0 || std::any_of(loaded.cbegin(), loaded.cend(),
                                       [serial](const Scene *c) { return c->serial() == serial; })) {
            ok = false;
            break;
        }
        Scene *c = makeScene(o.value(QStringLiteral("id")).toString(), serial, false);
        loaded.append(c);
        maxSerial = std::max(maxSerial, serial);
        if (!c->loadJson(o)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        qDeleteAll(loaded);
        reset();
        return false;
    }

    breakSceneCycles(loaded);

    m_undo->clear();
    for (Scene *c : std::as_const(m_scenes))
        emit sceneAboutToBeRemoved(c);
    qDeleteAll(m_scenes);
    m_scenes = loaded;
    m_nextSerial = maxSerial + 1;

    const QJsonObject show = root.value(QStringLiteral("show")).toObject();
    m_default = TransitionSpec::fromJson(show.value(QStringLiteral("defaultTransition")).toObject());
    const QJsonObject state = show.value(QStringLiteral("state")).toObject();
    m_liveId = state.value(QStringLiteral("liveSceneId")).toString();
    m_stagingId = state.value(QStringLiteral("stagingSceneId")).toString();
    m_editedId = state.value(QStringLiteral("editedSceneId")).toString();
    m_inScene = true;
    fixRoles();

    emit scenesReset();
    for (Scene *c : std::as_const(m_scenes))
        emit sceneAdded(c);
    emit liveSceneChanged();
    emit stagingSceneChanged();
    emit editedSceneChanged();
    emit defaultTransitionChanged();
    updateRuntimeActivity();
    emit changed();
    return true;
}

void ShowManager::fixRoles()
{
    if (!scene(m_liveId))
        m_liveId = m_scenes.first()->id();
    if (!scene(m_stagingId) || m_stagingId == m_liveId)
        m_stagingId.clear();
    if (!scene(m_editedId))
        m_editedId = m_liveId;
}
