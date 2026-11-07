#include "app/show/Cue.h"

#include "app/graph/GraphRuntime.h"
#include "core/graph/ClipGraph.h"
#include "core/graph/ClipGraphModels.h"

QJsonObject TransitionSpec::toJson() const
{
    return {{QStringLiteral("mode"), mode}, {QStringLiteral("durationMs"), durationMs}};
}

TransitionSpec TransitionSpec::fromJson(const QJsonObject &o)
{
    TransitionSpec s;
    s.mode = o.value(QStringLiteral("mode")).toInt(0);
    s.durationMs = o.value(QStringLiteral("durationMs")).toInt(1000);
    return s;
}

Cue::Cue(const QString &id, int serial, std::shared_ptr<NodeThumbStore> thumbs, bool withDefaults,
         QObject *parent)
    : QObject(parent), m_id(id), m_serial(serial)
{
    m_graph = new ClipGraph(this);
    m_graph->setIdBase(quint64(serial) << 40);
    m_controller = new ClipGraphController(m_graph, this);
    m_runtime = new GraphRuntime(m_graph, this, std::move(thumbs));
    if (withDefaults) {
        m_graph->addNode(ClipGraph::NodeType::Output);
        m_graph->addNode(ClipGraph::NodeType::MasterAudioOutput);
    }
}

void Cue::clearGraph()
{
    m_graph->clear();
    m_graph->addNode(ClipGraph::NodeType::Output);
    m_graph->addNode(ClipGraph::NodeType::MasterAudioOutput);
}

#define PRISM_CUE_SETTER(Type, member, Name)            \
    void Cue::set##Name(Type value)                     \
    {                                                   \
        if (member == value)                            \
            return;                                     \
        member = value;                                 \
        emit metaChanged();                             \
    }

PRISM_CUE_SETTER(const QString &, m_name, Name)
PRISM_CUE_SETTER(const QString &, m_color, Color)
PRISM_CUE_SETTER(const QString &, m_notes, Notes)
PRISM_CUE_SETTER(EndAction, m_endAction, EndAction)
PRISM_CUE_SETTER(int, m_autoAdvanceMs, AutoAdvanceMs)
PRISM_CUE_SETTER(bool, m_advanceOnMediaEnd, AdvanceOnMediaEnd)

#undef PRISM_CUE_SETTER

void Cue::setThumbnail(const QString &path)
{
    m_thumbnail = path;
    ++m_thumbRev;
    emit thumbnailChanged();
}

CueMeta Cue::meta() const
{
    return {m_name, m_color, m_notes, m_incoming, int(m_endAction), m_autoAdvanceMs, m_advanceOnMediaEnd};
}

void Cue::setMeta(const CueMeta &m)
{
    setName(m.name);
    setColor(m.color);
    setNotes(m.notes);
    setIncoming(m.incoming);
    setEndAction(EndAction(m.endAction));
    setAutoAdvanceMs(m.autoAdvanceMs);
    setAdvanceOnMediaEnd(m.advanceOnMediaEnd);
}

void Cue::setIncoming(const std::optional<TransitionSpec> &spec)
{
    if (m_incoming == spec)
        return;
    m_incoming = spec;
    emit metaChanged();
}

QString Cue::endActionName(EndAction action)
{
    switch (action) {
    case EndAction::Hold: return QStringLiteral("hold");
    case EndAction::Loop: return QStringLiteral("loop");
    case EndAction::AutoAdvance: return QStringLiteral("autoAdvance");
    case EndAction::Black: return QStringLiteral("black");
    }
    return QStringLiteral("hold");
}

Cue::EndAction Cue::endActionFromName(const QString &name)
{
    for (EndAction a : {EndAction::Loop, EndAction::AutoAdvance, EndAction::Black}) {
        if (endActionName(a) == name)
            return a;
    }
    return EndAction::Hold;
}

QJsonObject Cue::toJson() const
{
    QJsonObject o{
        {QStringLiteral("id"), m_id},
        {QStringLiteral("serial"), m_serial},
        {QStringLiteral("name"), m_name},
        {QStringLiteral("color"), m_color},
        {QStringLiteral("notes"), m_notes},
        {QStringLiteral("thumbnail"), m_thumbnail},
        {QStringLiteral("endAction"), endActionName(m_endAction)},
        {QStringLiteral("autoAdvanceMs"), m_autoAdvanceMs},
        {QStringLiteral("advanceOnMediaEnd"), m_advanceOnMediaEnd},
        {QStringLiteral("graph"), m_graph->toJson()},
    };
    if (m_incoming)
        o.insert(QStringLiteral("transition"), m_incoming->toJson());
    return o;
}

bool Cue::loadJson(const QJsonObject &o)
{
    m_name = o.value(QStringLiteral("name")).toString();
    m_color = o.value(QStringLiteral("color")).toString();
    m_notes = o.value(QStringLiteral("notes")).toString();
    m_thumbnail = o.value(QStringLiteral("thumbnail")).toString();
    m_endAction = endActionFromName(o.value(QStringLiteral("endAction")).toString());
    m_autoAdvanceMs = o.value(QStringLiteral("autoAdvanceMs")).toInt(0);
    m_advanceOnMediaEnd = o.value(QStringLiteral("advanceOnMediaEnd")).toBool(false);
    m_incoming.reset();
    if (o.contains(QStringLiteral("transition")))
        m_incoming = TransitionSpec::fromJson(o.value(QStringLiteral("transition")).toObject());
    const bool ok = m_graph->fromJson(o.value(QStringLiteral("graph")).toObject());
    emit metaChanged();
    return ok;
}
