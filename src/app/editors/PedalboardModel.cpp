#include "editors/PedalboardModel.h"

#include "app/AppController.h"
#include "core/audio/dsp/PedalCatalog.h"
#include "core/graph/ClipGraph.h"

#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QVector>

#include <algorithm>

using namespace prism::audiofx;

namespace {

QString scaleName(KnobSpec::Scale s)
{
    switch (s) {
    case KnobSpec::Scale::Log: return QStringLiteral("log");
    case KnobSpec::Scale::Toggle: return QStringLiteral("toggle");
    case KnobSpec::Scale::Choice: return QStringLiteral("choice");
    default: return QStringLiteral("linear");
    }
}

QString specLabel(const PedalSpec *spec, const QString &fallback)
{
    return spec ? QString::fromUtf8(spec->label) : fallback;
}

QVariantMap knobMap(const KnobSpec &k, double value, bool modulated)
{
    QStringList options;
    for (const char *o : k.options)
        options << QString::fromUtf8(o);
    return {{"id", QString::fromUtf8(k.id)},
            {"label", QString::fromUtf8(k.label)},
            {"min", k.min},
            {"max", k.max},
            {"def", k.defaultValue},
            {"scale", scaleName(k.scale)},
            {"unit", QString::fromUtf8(k.unit)},
            {"options", options},
            {"value", value},
            {"modulated", modulated},
            {"continuous", k.continuous()}};
}

QVariantList floatList(const QVector<float> &v)
{
    QVariantList out;
    for (float f : v)
        out.append(double(f));
    return out;
}

void collectNodes(const std::vector<RackItem> &items, QVariantList &out)
{
    for (const RackItem &item : items) {
        if (const RackPedal *p = item.pedal()) {
            const PedalSpec *spec = pedalSpec(p->type.toStdString());
            out.append(QVariantMap{{"id", p->id}, {"label", specLabel(spec, p->type) + QStringLiteral(" (") + p->id + ')'}});
        } else if (const RackSplit *s = item.split()) {
            out.append(QVariantMap{{"id", s->id}, {"label", QStringLiteral("Split (") + s->id + ')'}});
            for (const RackLane &lane : s->lanes)
                collectNodes(lane.items, out);
        }
    }
}

QVariantList itemList(const AudioRack &rack, const std::vector<RackItem> &items);

QVariantMap pedalMap(const AudioRack &rack, const RackPedal &pedal)
{
    const PedalSpec *spec = pedalSpec(pedal.type.toStdString());
    QVariantList knobs;
    if (spec) {
        for (const KnobSpec &k : spec->knobs) {
            const QString knobId = QString::fromUtf8(k.id);
            bool modulated = false;
            for (const RackRoute &r : rack.routes)
                if (r.to == pedal.id && r.knob == knobId)
                    modulated = true;
            knobs.append(knobMap(k, pedal.knobs.value(knobId, k.defaultValue), modulated));
        }
    }
    return {{"kind", QStringLiteral("pedal")},
            {"id", pedal.id},
            {"type", pedal.type},
            {"label", specLabel(spec, pedal.type)},
            {"category", spec ? QString::fromUtf8(spec->category) : QString()},
            {"bypass", pedal.bypass},
            {"irPath", pedal.irPath},
            {"knobs", knobs}};
}

QVariantMap splitMap(const AudioRack &rack, const RackSplit &split)
{
    const bool bands = split.mode == RackSplit::Mode::Bands;
    QVariantList crossovers;
    const int n = int(split.crossovers.size());
    for (int i = 0; i < n; ++i) {
        QString label = n == 1 ? QObject::tr("Low/High")
                      : n == 2 ? (i == 0 ? QObject::tr("Low/Mid") : QObject::tr("Mid/High"))
                               : QStringLiteral("X%1").arg(i + 1);
        crossovers.append(QVariantMap{{"label", label}, {"value", double(split.crossovers[i])}});
    }
    QVariantList lanes;
    for (const RackLane &lane : split.lanes)
        lanes.append(QVariantMap{{"gain", double(lane.gain)}, {"items", itemList(rack, lane.items)}});
    return {{"kind", QStringLiteral("split")},
            {"id", split.id},
            {"bands", bands},
            {"crossfade", split.crossfade},
            {"blend", double(split.blend)},
            {"crossovers", crossovers},
            {"lanes", lanes}};
}

QVariantList itemList(const AudioRack &rack, const std::vector<RackItem> &items)
{
    QVariantList out;
    for (const RackItem &item : items) {
        if (const RackPedal *p = item.pedal())
            out.append(pedalMap(rack, *p));
        else if (const RackSplit *s = item.split())
            out.append(splitMap(rack, *s));
    }
    return out;
}

QVariantList modulatorList(const AudioRack &rack)
{
    QVariantList out;
    for (const RackModulator &mod : rack.modulators) {
        const PedalSpec *spec = modulatorSpec(mod.type.toStdString());
        QVariantList knobs;
        if (spec)
            for (const KnobSpec &k : spec->knobs)
                knobs.append(knobMap(k, mod.knobs.value(QString::fromUtf8(k.id), k.defaultValue), false));
        QVariantList routes;
        for (const RackRoute &r : rack.routes) {
            if (r.from != mod.id)
                continue;
            QString pedalName = r.to;
            QString knob = r.knob;
            if (const RackPedal *p = findPedal(rack, r.to)) {
                const PedalSpec *ps = pedalSpec(p->type.toStdString());
                pedalName = specLabel(ps, p->type);
                if (ps)
                    if (const KnobSpec *k = findKnob(*ps, r.knob.toStdString()))
                        knob = QString::fromUtf8(k->label);
            }
            routes.append(QVariantMap{{"id", r.id},
                                      {"label", QStringLiteral("%1 · %2").arg(pedalName, knob)},
                                      {"target", r.to},
                                      {"knob", r.knob},
                                      {"depth", double(r.depth)}});
        }
        out.append(QVariantMap{{"id", mod.id},
                               {"type", mod.type},
                               {"label", specLabel(spec, mod.type)},
                               {"knobs", knobs},
                               {"steps", floatList(mod.steps)},
                               {"source", mod.source},
                               {"routes", routes}});
    }
    return out;
}

ClipGraphController *controller()
{
    auto *app = AppController::instance();
    return app ? app->graphController() : nullptr;
}

} // namespace

PedalboardModel::PedalboardModel(QObject *parent)
    : QObject(parent)
{
    auto row = [this](const QString &group, const QString &label, const QString &kind, const QString &arg) {
        m_palette.append(QVariantMap{{"group", group}, {"label", label}, {"kind", kind}, {"arg", arg}});
    };

    QStringList order;
    QMap<QString, QVector<const PedalSpec *>> byCategory;
    for (const PedalSpec &spec : pedalSpecs()) {
        QString title = QString::fromUtf8(spec.category);
        if (!title.isEmpty())
            title[0] = title[0].toUpper();
        if (!byCategory.contains(title))
            order << title;
        byCategory[title].append(&spec);
    }
    for (const QString &title : std::as_const(order))
        for (const PedalSpec *spec : byCategory[title])
            row(title, QString::fromUtf8(spec->label), QStringLiteral("pedal"), QString::fromUtf8(spec->type));
    row(tr("Split"), tr("Split (parallel)"), QStringLiteral("parallel"), QString());
    row(tr("Split"), tr("Split (bands)"), QStringLiteral("bands"), QString());
    for (const PedalSpec &spec : modulatorSpecs())
        row(tr("Modulators"), QString::fromUtf8(spec.label), QStringLiteral("mod"), QString::fromUtf8(spec.type));

    rebuild();
}

void PedalboardModel::bind(quint64 nodeId)
{
    m_node = 0;
    m_original.clear();
    if (auto *app = AppController::instance(); app && app->clipGraph()) {
        if (const ClipGraph::Node *n = app->clipGraph()->node(nodeId)) {
            m_original = n->params.value("params").toMap();
            m_rack = fromJson(QJsonObject::fromVariantMap(m_original));
            m_node = nodeId;
        }
    }
    m_selSplit.clear();
    m_selLane = 0;
    setStatus({});
    rebuild();
    emit targetChanged();
    emit boardChanged();
}

void PedalboardModel::commit()
{
    auto *app = AppController::instance();
    if (!m_node || !app || !app->clipGraph() || !controller())
        return;
    const QVariantMap current = toJson(m_rack).toVariantMap();
    app->clipGraph()->setEffectParams(m_node, m_original, false);
    controller()->setParams(m_node, {{"params", current}});
    m_node = 0;
}

void PedalboardModel::cancel()
{
    auto *app = AppController::instance();
    if (m_node && app && app->clipGraph())
        app->clipGraph()->setEffectParams(m_node, m_original, false);
    m_node = 0;
}

void PedalboardModel::applyLive()
{
    auto *app = AppController::instance();
    if (m_node && app && app->clipGraph())
        app->clipGraph()->setEffectParams(m_node, toJson(m_rack).toVariantMap(), false);
    emit edited();
}

void PedalboardModel::structural()
{
    applyLive();
    rebuild();
    emit boardChanged();
}

bool PedalboardModel::applyOp(bool ok)
{
    if (!ok) {
        setStatus(tr("Not possible: limit reached (4 lanes per split, 2 levels of nesting, 64 nodes)."));
        return false;
    }
    setStatus({});
    structural();
    return true;
}

void PedalboardModel::setStatus(const QString &text)
{
    if (m_status == text)
        return;
    m_status = text;
    emit statusChanged();
}

void PedalboardModel::rebuild()
{
    if (!findSplit(m_rack, m_selSplit))
        m_selSplit.clear();
    QVariantList sources;
    sources.append(QVariantMap{{"id", QStringLiteral("input")}, {"label", tr("Input")}});
    collectNodes(m_rack.items, sources);
    m_board = {{"items", itemList(m_rack, m_rack.items)},
               {"modulators", modulatorList(m_rack)},
               {"sources", sources}};
}

RackSlot PedalboardModel::targetSlot() const
{
    RackSlot slot;
    if (const RackSplit *s = findSplit(m_rack, m_selSplit); s && m_selLane >= 0 && m_selLane < int(s->lanes.size())) {
        slot.split = m_selSplit;
        slot.lane = m_selLane;
    }
    return slot;
}

void PedalboardModel::setTarget(const QString &split, int lane)
{
    if (m_selSplit == split && (split.isEmpty() || m_selLane == lane))
        return;
    m_selSplit = split;
    m_selLane = lane;
    emit targetChanged();
}

bool PedalboardModel::addFromPalette(const QString &kind, const QString &arg, const QString &irPath)
{
    const RackSlot slot = targetSlot();
    if (kind == QLatin1String("pedal")) {
        QString id;
        if (!addPedal(m_rack, arg, slot, &id))
            return applyOp(false);
        if (!irPath.isEmpty())
            if (RackPedal *p = findPedal(m_rack, id))
                p->irPath = irPath;
        return applyOp(true);
    }
    if (kind == QLatin1String("mod"))
        return applyOp(prism::audiofx::addModulator(m_rack, arg));
    return applyOp(addSplit(m_rack, kind == QLatin1String("bands") ? RackSplit::Mode::Bands : RackSplit::Mode::Parallel,
                            2, slot));
}

bool PedalboardModel::addLane(const QString &splitId)
{
    return applyOp(prism::audiofx::addLane(m_rack, splitId));
}

bool PedalboardModel::removeLane(const QString &splitId, int lane)
{
    if (m_selSplit == splitId) {
        m_selSplit.clear();
        emit targetChanged();
    }
    return applyOp(prism::audiofx::removeLane(m_rack, splitId, lane));
}

bool PedalboardModel::removeItem(const QString &id)
{
    return applyOp(prism::audiofx::removeItem(m_rack, id));
}

bool PedalboardModel::moveItem(const QString &id, const QString &split, int lane, int index)
{
    return applyOp(prism::audiofx::moveItem(m_rack, id, RackSlot{split, lane, index}));
}

bool PedalboardModel::removeModulator(const QString &id)
{
    return applyOp(prism::audiofx::removeModulator(m_rack, id));
}

bool PedalboardModel::addRoute(const QString &from, const QString &pedal, const QString &knob)
{
    return applyOp(prism::audiofx::addRoute(m_rack, from, pedal, knob));
}

bool PedalboardModel::removeRoute(const QString &routeId)
{
    return applyOp(prism::audiofx::removeRoute(m_rack, routeId));
}

void PedalboardModel::setPedalKnob(const QString &id, const QString &knob, double value)
{
    if (RackPedal *p = findPedal(m_rack, id)) {
        p->knobs[knob] = float(value);
        applyLive();
    }
}

void PedalboardModel::setPedalBypass(const QString &id, bool bypass)
{
    if (RackPedal *p = findPedal(m_rack, id)) {
        p->bypass = bypass;
        applyLive();
    }
}

void PedalboardModel::setPedalIr(const QString &id, const QString &path)
{
    if (RackPedal *p = findPedal(m_rack, id)) {
        p->irPath = path;
        structural();
    }
}

void PedalboardModel::setSplitBlend(const QString &id, double value)
{
    if (RackSplit *s = findSplit(m_rack, id)) {
        s->blend = float(value);
        applyLive();
    }
}

void PedalboardModel::setSplitCrossfade(const QString &id, bool on)
{
    if (RackSplit *s = findSplit(m_rack, id)) {
        s->crossfade = on;
        applyLive();
    }
}

void PedalboardModel::setCrossover(const QString &id, int index, double hz)
{
    if (RackSplit *s = findSplit(m_rack, id); s && index >= 0 && index < s->crossovers.size()) {
        s->crossovers[index] = float(hz);
        std::sort(s->crossovers.begin(), s->crossovers.end());
        applyLive();
    }
}

void PedalboardModel::setLaneGain(const QString &id, int lane, double gain)
{
    if (RackSplit *s = findSplit(m_rack, id); s && lane >= 0 && lane < int(s->lanes.size())) {
        s->lanes[size_t(lane)].gain = float(gain);
        applyLive();
    }
}

void PedalboardModel::setModKnob(const QString &id, const QString &knob, double value)
{
    for (RackModulator &m : m_rack.modulators)
        if (m.id == id) {
            m.knobs[knob] = float(value);
            applyLive();
            return;
        }
}

void PedalboardModel::setStep(const QString &id, int step, double value)
{
    for (RackModulator &m : m_rack.modulators)
        if (m.id == id && step >= 0 && step < m.steps.size()) {
            m.steps[step] = float(value);
            applyLive();
            return;
        }
}

void PedalboardModel::setModSource(const QString &id, const QString &source)
{
    for (RackModulator &m : m_rack.modulators)
        if (m.id == id) {
            m.source = source;
            structural();
            return;
        }
}

void PedalboardModel::setRouteDepth(const QString &routeId, double depth)
{
    for (RackRoute &r : m_rack.routes)
        if (r.id == routeId) {
            r.depth = float(depth);
            applyLive();
            return;
        }
}
