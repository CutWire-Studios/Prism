#include "app/show/ShowManager.h"

#include "app/graph/GraphRuntime.h"
#include "app/show/GlobalsModel.h"
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
#include <cmath>

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

/// One undo step over the whole globals list. Consecutive value edits of the same global merge so a
/// slider drag is a single step.
class GlobalsCommand : public QUndoCommand {
public:
    GlobalsCommand(ShowManager *show, const QList<ShowGlobal> &before, const QList<ShowGlobal> &after,
                   const QString &text, const QString &mergeKey)
        : QUndoCommand(text), m_show(show), m_before(before), m_after(after), m_key(mergeKey) {}
    void redo() override { m_show->applyGlobals(m_after); }
    void undo() override { m_show->applyGlobals(m_before); }
    int id() const override { return m_key.isEmpty() ? -1 : 0x474c42; }
    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *o = static_cast<const GlobalsCommand *>(other);
        if (o->m_key != m_key)
            return false;
        m_after = o->m_after;
        return true;
    }

private:
    ShowManager *m_show;
    QList<ShowGlobal> m_before, m_after;
    QString m_key;
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
    , m_globalsModel(new GlobalsModel(this))
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
    connect(c, &Scene::metaChanged, this, &ShowManager::updateRuntimeActivity);
    connect(c, &Scene::thumbnailChanged, this, &ShowManager::thumbnailChanged);
    connect(c->graph(), &ClipGraph::changed, this, &ShowManager::changed);
    c->runtime()->setGlobals(globalValues());
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

bool ShowManager::isValidGlobalName(const QString &name)
{
    // Expression built-ins and Lua keywords: `globals.<name>` cannot shadow the former, but a clash would still
    // read as ambiguous, and a keyword is not a valid field name.
    static const QSet<QString> reserved = {
        "t", "bpm", "beat", "audio", "param", "mod", "globals",
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
        "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
    if (name.isEmpty() || reserved.contains(name))
        return false;
    for (qsizetype i = 0; i < name.size(); ++i) {
        const char16_t ch = name.at(i).unicode();
        const bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
        const bool digit = ch >= '0' && ch <= '9';
        if (!alpha && !(digit && i > 0))
            return false;
    }
    return true;
}

int ShowManager::globalIndex(const QString &name) const
{
    for (int i = 0; i < m_globals.size(); ++i) {
        if (m_globals.at(i).name == name)
            return i;
    }
    return -1;
}

QVariantMap ShowManager::globalValues() const
{
    QVariantMap out;
    for (const ShowGlobal &g : m_globals)
        out.insert(g.name, g.value);
    return out;
}

void ShowManager::pushGlobals()
{
    const QVariantMap values = globalValues();
    for (Scene *c : std::as_const(m_scenes))
        c->runtime()->setGlobals(values);
}

void ShowManager::applyGlobals(const QList<ShowGlobal> &list)
{
    const int oldSize = m_globals.size(), newSize = list.size();
    int first = 0;
    while (first < std::min(oldSize, newSize) && m_globals.at(first) == list.at(first))
        ++first;
    auto tailEqual = [&](int oldFrom, int newFrom) {
        return std::equal(m_globals.cbegin() + oldFrom, m_globals.cend(), list.cbegin() + newFrom, list.cend());
    };
    if (oldSize == newSize) {
        const QList<ShowGlobal> before = m_globals;
        m_globals = list;
        for (int i = 0; i < newSize; ++i) {
            if (!(before.at(i) == list.at(i)))
                emit globalUpdated(i);
        }
    } else if (newSize == oldSize + 1 && tailEqual(first, first + 1)) {
        emit globalAboutToBeInserted(first);
        m_globals = list;
        emit globalInserted();
    } else if (newSize == oldSize - 1 && tailEqual(first + 1, first)) {
        emit globalAboutToBeRemoved(first);
        m_globals = list;
        emit globalRemoved();
    } else {
        m_globals = list;
        emit globalsReset();
    }
    pushGlobals();
    emit changed();
}

void ShowManager::editGlobals(const QList<ShowGlobal> &after, const QString &text, const QString &mergeKey)
{
    if (after == m_globals)
        return;
    m_undo->push(new GlobalsCommand(this, m_globals, after, text, mergeKey));
}

QString ShowManager::addGlobal(const QString &name, double value, double min, double max)
{
    QString use = name;
    if (use.isEmpty()) {
        int n = m_globals.size() + 1;
        while (globalIndex(QStringLiteral("global%1").arg(n)) >= 0)
            ++n;
        use = QStringLiteral("global%1").arg(n);
    }
    if (!isValidGlobalName(use) || globalIndex(use) >= 0 || !std::isfinite(min) || !std::isfinite(max)
        || !std::isfinite(value) || !(min < max))
        return {};
    QList<ShowGlobal> after = m_globals;
    after.append({use, std::clamp(value, min, max), min, max});
    editGlobals(after, tr("Add global"));
    return use;
}

bool ShowManager::removeGlobal(const QString &name)
{
    const int i = globalIndex(name);
    if (i < 0)
        return false;
    QList<ShowGlobal> after = m_globals;
    after.removeAt(i);
    editGlobals(after, tr("Remove global"));
    return true;
}

bool ShowManager::renameGlobal(const QString &name, const QString &newName)
{
    const int i = globalIndex(name);
    if (i < 0 || !isValidGlobalName(newName) || (newName != name && globalIndex(newName) >= 0))
        return false;
    QList<ShowGlobal> after = m_globals;
    after[i].name = newName;
    editGlobals(after, tr("Rename global"));
    return true;
}

bool ShowManager::setGlobalValue(const QString &name, double value)
{
    const int i = globalIndex(name);
    if (i < 0 || !std::isfinite(value))
        return false;
    QList<ShowGlobal> after = m_globals;
    after[i].value = std::clamp(value, after[i].min, after[i].max);
    editGlobals(after, tr("Set global"), name);
    return true;
}

bool ShowManager::setGlobalRange(const QString &name, double min, double max)
{
    const int i = globalIndex(name);
    if (i < 0 || !std::isfinite(min) || !std::isfinite(max) || !(min < max))
        return false;
    QList<ShowGlobal> after = m_globals;
    after[i].min = min;
    after[i].max = max;
    after[i].value = std::clamp(after[i].value, min, max);
    editGlobals(after, tr("Edit global range"));
    return true;
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
    m_globals.clear();
    emit globalsReset();
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

QSet<QString> ShowManager::residentSceneIds() const
{
    QSet<QString> resident;
    QStringList stack;
    for (const Scene *c : std::as_const(m_scenes)) {
        if (c->keepResident())
            stack.append(c->id());
    }
    while (!stack.isEmpty()) {
        const QString id = stack.takeLast();
        const Scene *c = scene(id);
        if (!c || resident.contains(id))
            continue;
        resident.insert(id);
        stack.append(sceneRefsOf(c));
    }
    return resident;
}

void ShowManager::updateRuntimeActivity()
{
    const QSet<QString> active = activeSceneIds() | residentSceneIds();
    for (Scene *c : std::as_const(m_scenes))
        c->runtime()->setActive(active.contains(c->id()));
}

QJsonObject ShowManager::toJson() const
{
    QJsonArray scenes;
    for (const Scene *c : m_scenes)
        scenes.append(c->toJson());
    QJsonArray globals;
    for (const ShowGlobal &g : m_globals) {
        globals.append(QJsonObject{{QStringLiteral("name"), g.name}, {QStringLiteral("value"), g.value},
                                   {QStringLiteral("min"), g.min}, {QStringLiteral("max"), g.max}});
    }
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
        {QStringLiteral("globals"), globals},
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

    m_globals.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("globals")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        double min = o.value(QStringLiteral("min")).toDouble(0.0);
        double max = o.value(QStringLiteral("max")).toDouble(1.0);
        if (!isValidGlobalName(name) || globalIndex(name) >= 0)
            continue;
        if (!(min < max)) {
            min = 0.0;
            max = 1.0;
        }
        m_globals.append({name, std::clamp(o.value(QStringLiteral("value")).toDouble(min), min, max), min, max});
    }
    emit globalsReset();
    pushGlobals();

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
