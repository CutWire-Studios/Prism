#include "app/show/ShowManager.h"

#include "app/graph/GraphRuntime.h"
#include "app/show/CueListModel.h"
#include "core/graph/ClipGraph.h"

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

} // namespace

/// Inserts or removes a cue by snapshot. Add/duplicate push it already applied (first redo is
/// skipped); undoing them removes the cue, redoing re-creates it with the same id and graph ids.
class CueStructureCommand : public QUndoCommand {
public:
    CueStructureCommand(ShowManager *show, bool insert, const QString &id, const QJsonObject &snap, int index,
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
            m_show->restoreCue(m_snap, m_index);
            return;
        }
        if (const Cue *c = m_show->cue(m_id)) {
            m_snap = c->toJson();
            m_index = m_show->indexOf(m_id);
            m_show->eraseCue(m_id);
        }
    }

    ShowManager *m_show;
    bool m_insert;
    QString m_id;
    QJsonObject m_snap;
    int m_index;
    bool m_skip;
};

class MoveCueCommand : public QUndoCommand {
public:
    MoveCueCommand(ShowManager *show, int from, int to)
        : QUndoCommand(QObject::tr("Move cue")), m_show(show), m_from(from), m_to(to) {}
    void redo() override { m_show->moveCueRaw(m_from, m_to); }
    void undo() override { m_show->moveCueRaw(m_to, m_from); }

private:
    ShowManager *m_show;
    int m_from, m_to;
};

namespace {

class CueMetaCommand : public QUndoCommand {
public:
    CueMetaCommand(ShowManager *show, const QString &id, const CueMeta &before, const CueMeta &after)
        : QUndoCommand(QObject::tr("Edit cue")), m_show(show), m_id(id), m_before(before), m_after(after) {}
    void redo() override { apply(m_after); }
    void undo() override { apply(m_before); }

private:
    void apply(const CueMeta &m)
    {
        if (Cue *c = m_show->cue(m_id))
            c->setMeta(m);
    }
    ShowManager *m_show;
    QString m_id;
    CueMeta m_before, m_after;
};

} // namespace

ShowManager::ShowManager(QObject *parent)
    : QObject(parent)
    , m_model(new CueListModel(this))
    , m_thumbs(GraphRuntime::createThumbStore())
    , m_endTimer(new QTimer(this))
    , m_undo(new QUndoStack(this))
{
    m_endTimer->setSingleShot(true);
    connect(m_endTimer, &QTimer::timeout, this, &ShowManager::notifyLiveEnded);
    connect(this, &ShowManager::liveCueChanged, this, &ShowManager::rearmEndTimer);
    reset();
}

ShowManager::~ShowManager() = default;

Cue *ShowManager::cue(const QString &id) const
{
    for (Cue *c : m_cues) {
        if (c->id() == id)
            return c;
    }
    return nullptr;
}

int ShowManager::indexOf(const QString &id) const
{
    for (int i = 0; i < m_cues.size(); ++i) {
        if (m_cues.at(i)->id() == id)
            return i;
    }
    return -1;
}

Cue *ShowManager::cueForNode(quint64 nodeId) const
{
    for (Cue *c : m_cues) {
        if (c->graph()->hasNode(nodeId))
            return c;
    }
    return nullptr;
}

Cue *ShowManager::nextCue(const QString &id) const
{
    const int i = indexOf(id);
    return i >= 0 && i + 1 < m_cues.size() ? m_cues.at(i + 1) : nullptr;
}

Cue *ShowManager::makeCue(const QString &id, int serial, bool withDefaults)
{
    auto *c = new Cue(id, serial, m_thumbs, withDefaults, this);
    c->graph()->setOutputCanvasSize(m_canvasW, m_canvasH);
    connect(c, &Cue::metaChanged, this, &ShowManager::changed);
    connect(c, &Cue::metaChanged, this, [this, c] { emit cueMetaChanged(c); });
    connect(c, &Cue::thumbnailChanged, this, &ShowManager::thumbnailChanged);
    connect(c->graph(), &ClipGraph::changed, this, &ShowManager::changed);
    m_nextSerial = std::max(m_nextSerial, serial + 1);
    return c;
}

Cue *ShowManager::addCue(const QString &name, int index)
{
    Cue *c = makeCue(QUuid::createUuid().toString(QUuid::WithoutBraces), m_nextSerial, true);
    c->setName(name.isEmpty() ? tr("Cue %1").arg(c->serial()) : name);
    m_cues.insert(index < 0 || index > m_cues.size() ? m_cues.size() : index, c);
    emit cueAdded(c);
    updateRuntimeActivity();
    emit changed();
    m_undo->push(new CueStructureCommand(this, true, c->id(), {}, indexOf(c->id()), true, tr("Add cue")));
    return c;
}

Cue *ShowManager::duplicateCue(const QString &id)
{
    const Cue *src = cue(id);
    if (!src)
        return nullptr;
    Cue *c = makeCue(QUuid::createUuid().toString(QUuid::WithoutBraces), m_nextSerial, false);
    QJsonObject state = src->toJson();
    state[QStringLiteral("graph")] = shiftIds(state.value(QStringLiteral("graph")).toObject(),
                                              (qint64(c->serial()) - src->serial()) << 40);
    c->loadJson(state);
    c->setName(tr("%1 copy").arg(src->name()));
    m_cues.insert(indexOf(id) + 1, c);
    emit cueAdded(c);
    updateRuntimeActivity();
    emit changed();
    m_undo->push(new CueStructureCommand(this, true, c->id(), {}, indexOf(c->id()), true, tr("Duplicate cue")));
    return c;
}

bool ShowManager::removeCue(const QString &id)
{
    Cue *c = cue(id);
    if (!c || id == m_liveId || m_cues.size() <= 1)
        return false;
    m_undo->push(new CueStructureCommand(this, false, id, c->toJson(), indexOf(id), false, tr("Remove cue")));
    return true;
}

Cue *ShowManager::restoreCue(const QJsonObject &snapshot, int index)
{
    Cue *c = makeCue(snapshot.value(QStringLiteral("id")).toString(),
                     snapshot.value(QStringLiteral("serial")).toInt(), false);
    c->loadJson(snapshot);
    m_cues.insert(qBound(0, index, int(m_cues.size())), c);
    emit cueAdded(c);
    updateRuntimeActivity();
    emit changed();
    return c;
}

bool ShowManager::eraseCue(const QString &id)
{
    Cue *c = cue(id);
    if (!c || id == m_liveId || m_cues.size() <= 1)
        return false;
    emit cueAboutToBeRemoved(c);
    m_cues.removeOne(c);
    if (m_stagingId == id) {
        m_stagingId.clear();
        emit stagingCueChanged();
    }
    if (m_editedId == id) {
        m_editedId = m_liveId;
        emit editedCueChanged();
    }
    delete c;
    emit cuesReset();
    emit changed();
    return true;
}

bool ShowManager::moveCue(int from, int to)
{
    if (from < 0 || from >= m_cues.size() || to < 0 || to >= m_cues.size() || from == to)
        return false;
    m_undo->push(new MoveCueCommand(this, from, to));
    return true;
}

void ShowManager::setCueMeta(const QString &id, const CueMeta &meta)
{
    Cue *c = cue(id);
    if (!c || c->meta() == meta)
        return;
    m_undo->push(new CueMetaCommand(this, id, c->meta(), meta));
}

bool ShowManager::moveCueRaw(int from, int to)
{
    if (from < 0 || from >= m_cues.size() || to < 0 || to >= m_cues.size() || from == to)
        return false;
    m_cues.move(from, to);
    emit cuesReset();
    emit changed();
    return true;
}

void ShowManager::reset()
{
    m_undo->clear();
    for (Cue *c : std::as_const(m_cues))
        emit cueAboutToBeRemoved(c);
    qDeleteAll(m_cues);
    m_cues.clear();
    m_nextSerial = 1;
    m_default = {};
    Cue *c = makeCue(QUuid::createUuid().toString(QUuid::WithoutBraces), m_nextSerial, true);
    c->setName(tr("Cue %1").arg(c->serial()));
    m_cues.append(c);
    m_liveId = c->id();
    m_stagingId.clear();
    m_editedId = c->id();
    m_inCue = true;
    emit cuesReset();
    emit cueAdded(c);
    emit liveCueChanged();
    emit stagingCueChanged();
    emit editedCueChanged();
    emit defaultTransitionChanged();
    updateRuntimeActivity();
    emit changed();
}

bool ShowManager::stageCue(const QString &id)
{
    if (!id.isEmpty() && !cue(id))
        return false;
    if (m_stagingId == id)
        return true;
    m_stagingId = id;
    emit stagingCueChanged();
    updateRuntimeActivity();
    return true;
}

bool ShowManager::setLiveCue(const QString &id)
{
    if (!cue(id))
        return false;
    if (m_stagingId == id) {
        m_stagingId.clear();
        emit stagingCueChanged();
    }
    if (m_liveId != id) {
        m_liveId = id;
        emit liveCueChanged();
    }
    updateRuntimeActivity();
    return true;
}

void ShowManager::swapLiveStaging()
{
    if (m_stagingId.isEmpty())
        return;
    std::swap(m_liveId, m_stagingId);
    emit liveCueChanged();
    emit stagingCueChanged();
    updateRuntimeActivity();
    emit changed();
}

bool ShowManager::enterCue(const QString &id)
{
    if (!cue(id))
        return false;
    m_editedId = id;
    m_inCue = true;
    emit editedCueChanged();
    updateRuntimeActivity();
    return true;
}

void ShowManager::exitCue()
{
    if (!m_inCue)
        return;
    m_inCue = false;
    emit editedCueChanged();
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

TransitionSpec ShowManager::resolveTransition(const Cue *staging) const
{
    return staging && staging->incoming() ? *staging->incoming() : m_default;
}

void ShowManager::rearmEndTimer()
{
    m_endTimer->stop();
    const Cue *live = liveCue();
    if (live && live->autoAdvanceMs() > 0
        && (live->endAction() == Cue::EndAction::AutoAdvance || live->endAction() == Cue::EndAction::Black))
        m_endTimer->start(live->autoAdvanceMs());
}

void ShowManager::notifyLiveEnded()
{
    emit liveEnded();
}

void ShowManager::notifyLiveMediaEnded()
{
    const Cue *live = liveCue();
    if (live && live->advanceOnMediaEnd()
        && (live->endAction() == Cue::EndAction::AutoAdvance || live->endAction() == Cue::EndAction::Black)) {
        m_endTimer->stop();
        emit liveEnded();
    }
}

void ShowManager::setCanvasSize(int w, int h)
{
    m_canvasW = w;
    m_canvasH = h;
    for (Cue *c : std::as_const(m_cues))
        c->graph()->setOutputCanvasSize(w, h);
}

QQuickImageProvider *ShowManager::createImageProvider() const
{
    return GraphRuntime::createImageProvider(m_thumbs);
}

void ShowManager::updateRuntimeActivity()
{
    for (Cue *c : std::as_const(m_cues)) {
        const bool active = c->id() == m_liveId || c->id() == m_stagingId
            || (m_inCue && c->id() == m_editedId);
        c->runtime()->setActive(active);
    }
}

QJsonObject ShowManager::toJson() const
{
    QJsonArray cues;
    for (const Cue *c : m_cues)
        cues.append(c->toJson());
    return {
        {QStringLiteral("show"), QJsonObject{
            {QStringLiteral("defaultTransition"), m_default.toJson()},
            {QStringLiteral("state"), QJsonObject{
                {QStringLiteral("liveCueId"), m_liveId},
                {QStringLiteral("stagingCueId"), m_stagingId},
                {QStringLiteral("editedCueId"), m_editedId},
            }},
        }},
        {QStringLiteral("cues"), cues},
    };
}

bool ShowManager::fromJson(const QJsonObject &root)
{
    const QJsonArray cueArr = root.value(QStringLiteral("cues")).toArray();
    if (cueArr.isEmpty()) {
        reset();
        return false;
    }

    QList<Cue *> loaded;
    int maxSerial = 0;
    bool ok = true;
    for (const QJsonValue &v : cueArr) {
        const QJsonObject o = v.toObject();
        const int serial = o.value(QStringLiteral("serial")).toInt(0);
        if (serial <= 0 || std::any_of(loaded.cbegin(), loaded.cend(),
                                       [serial](const Cue *c) { return c->serial() == serial; })) {
            ok = false;
            break;
        }
        Cue *c = makeCue(o.value(QStringLiteral("id")).toString(), serial, false);
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

    m_undo->clear();
    for (Cue *c : std::as_const(m_cues))
        emit cueAboutToBeRemoved(c);
    qDeleteAll(m_cues);
    m_cues = loaded;
    m_nextSerial = maxSerial + 1;

    const QJsonObject show = root.value(QStringLiteral("show")).toObject();
    m_default = TransitionSpec::fromJson(show.value(QStringLiteral("defaultTransition")).toObject());
    const QJsonObject state = show.value(QStringLiteral("state")).toObject();
    m_liveId = state.value(QStringLiteral("liveCueId")).toString();
    m_stagingId = state.value(QStringLiteral("stagingCueId")).toString();
    m_editedId = state.value(QStringLiteral("editedCueId")).toString();
    m_inCue = true;
    fixRoles();

    emit cuesReset();
    for (Cue *c : std::as_const(m_cues))
        emit cueAdded(c);
    emit liveCueChanged();
    emit stagingCueChanged();
    emit editedCueChanged();
    emit defaultTransitionChanged();
    updateRuntimeActivity();
    emit changed();
    return true;
}

void ShowManager::fixRoles()
{
    if (!cue(m_liveId))
        m_liveId = m_cues.first()->id();
    if (!cue(m_stagingId) || m_stagingId == m_liveId)
        m_stagingId.clear();
    if (!cue(m_editedId))
        m_editedId = m_liveId;
}
