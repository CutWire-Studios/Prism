#include "core/audio/AudioRack.h"

#include "core/audio/dsp/PedalCatalog.h"
#include "core/audio/dsp/WavReader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace prism::audiofx {

namespace {

constexpr float kMaxLaneGain = 4.0f;
const QVector<float> kDefaultSteps = {1.0f, 0.25f, 0.75f, 0.0f, 0.5f, 1.0f, 0.25f, 0.5f};

std::string_view sv(const QString &s, QByteArray &storage)
{
    storage = s.toUtf8();
    return {storage.constData(), size_t(storage.size())};
}

const PedalSpec *specFor(const QString &type)
{
    QByteArray b;
    return pedalSpec(sv(type, b));
}

const PedalSpec *modSpecFor(const QString &type)
{
    QByteArray b;
    return modulatorSpec(sv(type, b));
}

QMap<QString, float> defaultKnobs(const PedalSpec &spec)
{
    QMap<QString, float> knobs;
    for (const KnobSpec &k : spec.knobs)
        knobs.insert(QString::fromUtf8(k.id), k.defaultValue);
    return knobs;
}

QVector<float> defaultCrossovers(int lanes)
{
    QVector<float> out;
    for (int i = 0; i < lanes - 1; ++i) {
        const double t = lanes == 2 ? 0.5 : double(i) / double(lanes - 2);
        out.push_back(float(std::round(200.0 * std::pow(25.0, t))));
    }
    return out;
}

template <typename Fn>
void walk(const std::vector<RackItem> &items, int depth, Fn &&fn)
{
    for (const RackItem &item : items) {
        fn(item, depth);
        if (const RackSplit *split = item.split())
            for (const RackLane &lane : split->lanes)
                walk(lane.items, depth + 1, fn);
    }
}

QSet<QString> idsUnder(const std::vector<RackItem> &items)
{
    QSet<QString> ids;
    walk(items, 0, [&](const RackItem &item, int) { ids.insert(item.id()); });
    return ids;
}

QString uniqueId(const AudioRack &rack, const QString &prefix)
{
    QSet<QString> used = idsUnder(rack.items);
    for (const RackModulator &m : rack.modulators)
        used.insert(m.id);
    for (const RackRoute &r : rack.routes)
        used.insert(r.id);
    for (int n = 1;; ++n) {
        const QString id = prefix + QString::number(n);
        if (!used.contains(id))
            return id;
    }
}

// Null when the slot does not exist. *depth is the number of splits enclosing the list.
std::vector<RackItem> *listAt(AudioRack &rack, const RackSlot &slot, int *depth)
{
    if (slot.split.isEmpty()) {
        *depth = 0;
        return &rack.items;
    }
    int found = -1;
    std::vector<RackItem> *result = nullptr;
    struct Search
    {
        const RackSlot &slot;
        int &found;
        std::vector<RackItem> *&result;
        void run(std::vector<RackItem> &items, int d)
        {
            for (RackItem &item : items) {
                RackSplit *split = item.split();
                if (!split)
                    continue;
                if (split->id == slot.split && slot.lane >= 0 && slot.lane < int(split->lanes.size())) {
                    found = d + 1;
                    result = &split->lanes[size_t(slot.lane)].items;
                    return;
                }
                for (RackLane &lane : split->lanes) {
                    run(lane.items, d + 1);
                    if (result)
                        return;
                }
            }
        }
    };
    Search{slot, found, result}.run(rack.items, 0);
    *depth = found;
    return result;
}

struct Location
{
    std::vector<RackItem> *list = nullptr;
    size_t index = 0;
};

Location locate(std::vector<RackItem> &items, const QString &id)
{
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].id() == id)
            return {&items, i};
        if (RackSplit *split = items[i].split())
            for (RackLane &lane : split->lanes)
                if (Location l = locate(lane.items, id); l.list)
                    return l;
    }
    return {};
}

int splitDepthBelow(const RackItem &item)
{
    const RackSplit *split = item.split();
    if (!split)
        return 0;
    int deepest = 0;
    for (const RackLane &lane : split->lanes)
        for (const RackItem &child : lane.items)
            deepest = std::max(deepest, splitDepthBelow(child));
    return deepest + 1;
}

void insertAt(std::vector<RackItem> &list, RackItem item, int index)
{
    const size_t at = index < 0 ? list.size() : std::min(size_t(index), list.size());
    list.insert(list.begin() + std::ptrdiff_t(at), std::move(item));
}

void dropReferences(AudioRack &rack, const QSet<QString> &gone)
{
    rack.routes.erase(std::remove_if(rack.routes.begin(), rack.routes.end(),
                                     [&](const RackRoute &r) { return gone.contains(r.to); }),
                      rack.routes.end());
    for (RackModulator &m : rack.modulators)
        if (gone.contains(m.source))
            m.source = QStringLiteral("input");
}

QJsonObject knobsToJson(const QMap<QString, float> &knobs)
{
    QJsonObject o;
    for (auto it = knobs.begin(); it != knobs.end(); ++it)
        o.insert(it.key(), double(it.value()));
    return o;
}

QMap<QString, float> knobsFromJson(const QJsonObject &o)
{
    QMap<QString, float> knobs;
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.value().isDouble())
            knobs.insert(it.key(), float(it.value().toDouble()));
    return knobs;
}

QJsonArray floatsToJson(const QVector<float> &values)
{
    QJsonArray a;
    for (float v : values)
        a.append(double(v));
    return a;
}

QVector<float> floatsFromJson(const QJsonArray &a)
{
    QVector<float> out;
    for (const QJsonValue &v : a)
        out.push_back(float(v.toDouble()));
    return out;
}

QString modeName(RackSplit::Mode mode)
{
    return mode == RackSplit::Mode::Bands ? QStringLiteral("bands") : QStringLiteral("parallel");
}

QJsonArray itemsToJson(const std::vector<RackItem> &items);

QJsonObject itemToJson(const RackItem &item)
{
    QJsonObject o;
    if (const RackPedal *p = item.pedal()) {
        o["id"] = p->id;
        o["type"] = p->type;
        o["knobs"] = knobsToJson(p->knobs);
        if (p->bypass)
            o["bypass"] = true;
        if (!p->irPath.isEmpty())
            o["ir"] = p->irPath;
        return o;
    }
    const RackSplit &s = *item.split();
    o["id"] = s.id;
    o["type"] = QStringLiteral("split");
    o["mode"] = modeName(s.mode);
    if (s.crossfade)
        o["crossfade"] = true;
    o["blend"] = double(s.blend);
    if (s.mode == RackSplit::Mode::Bands)
        o["crossovers"] = floatsToJson(s.crossovers);
    QJsonArray lanes;
    for (const RackLane &lane : s.lanes) {
        QJsonObject l;
        l["gain"] = double(lane.gain);
        l["items"] = itemsToJson(lane.items);
        lanes.append(l);
    }
    o["lanes"] = lanes;
    return o;
}

QJsonArray itemsToJson(const std::vector<RackItem> &items)
{
    QJsonArray a;
    for (const RackItem &item : items)
        a.append(itemToJson(item));
    return a;
}

std::vector<RackItem> itemsFromJson(const QJsonArray &a);

RackItem itemFromJson(const QJsonObject &o)
{
    RackItem item;
    if (o["type"].toString() != QLatin1String("split")) {
        RackPedal p;
        p.id = o["id"].toString();
        p.type = o["type"].toString();
        p.knobs = knobsFromJson(o["knobs"].toObject());
        p.bypass = o["bypass"].toBool();
        p.irPath = o["ir"].toString();
        item.v = std::move(p);
        return item;
    }
    RackSplit s;
    s.id = o["id"].toString();
    s.mode = o["mode"].toString() == QLatin1String("bands") ? RackSplit::Mode::Bands : RackSplit::Mode::Parallel;
    s.crossfade = o["crossfade"].toBool();
    s.blend = float(o["blend"].toDouble(0.5));
    s.crossovers = floatsFromJson(o["crossovers"].toArray());
    for (const QJsonValue &lv : o["lanes"].toArray()) {
        const QJsonObject lo = lv.toObject();
        RackLane lane;
        lane.gain = float(lo["gain"].toDouble(1.0));
        lane.items = itemsFromJson(lo["items"].toArray());
        s.lanes.push_back(std::move(lane));
    }
    item.v = std::move(s);
    return item;
}

std::vector<RackItem> itemsFromJson(const QJsonArray &a)
{
    std::vector<RackItem> items;
    for (const QJsonValue &v : a)
        if (v.isObject())
            items.push_back(itemFromJson(v.toObject()));
    return items;
}

QJsonObject graphItem(const AudioRack &rack, const RackItem &item);

QJsonArray graphChain(const AudioRack &rack, const std::vector<RackItem> &items)
{
    QJsonArray a;
    for (const RackItem &item : items)
        a.append(graphItem(rack, item));
    return a;
}

QJsonObject graphItem(const AudioRack &rack, const RackItem &item)
{
    QJsonObject o;
    o["id"] = item.id();
    if (const RackPedal *p = item.pedal()) {
        o["type"] = p->type;
        o["knobs"] = knobsToJson(p->knobs);
        if (p->bypass)
            o["bypass"] = 1;
        if (!p->irPath.isEmpty())
            o["ir"] = p->irPath;
        QJsonObject mod;
        for (const RackRoute &r : rack.routes) {
            if (r.to != p->id)
                continue;
            QJsonArray list = mod[r.knob].toArray();
            list.append(QJsonObject{{"from", r.from}, {"depth", double(r.depth)}});
            mod[r.knob] = list;
        }
        if (!mod.isEmpty())
            o["mod"] = mod;
        return o;
    }
    const RackSplit &s = *item.split();
    o["type"] = QStringLiteral("split");
    o["mode"] = modeName(s.mode);
    if (s.crossfade)
        o["crossfade"] = true;
    o["blend"] = double(s.blend);
    if (s.mode == RackSplit::Mode::Bands)
        o["crossovers"] = floatsToJson(s.crossovers);
    QJsonArray lanes;
    for (const RackLane &lane : s.lanes)
        lanes.append(QJsonObject{{"gain", double(lane.gain)}, {"chain", graphChain(rack, lane.items)}});
    o["lanes"] = lanes;
    return o;
}

std::shared_ptr<const IrData> loadIrFile(const std::string &path, std::string *error)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = "cannot open impulse response '" + path + "'";
        return nullptr;
    }
    const QByteArray bytes = file.readAll();
    auto ir = std::make_shared<IrData>();
    if (!readWav(reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()), ir.get(), error))
        return nullptr;
    return ir;
}

QJsonArray shapeItems(const std::vector<RackItem> &items)
{
    QJsonArray a;
    for (const RackItem &item : items) {
        if (const RackPedal *p = item.pedal()) {
            QJsonArray knobIds;
            for (auto it = p->knobs.begin(); it != p->knobs.end(); ++it)
                knobIds.append(it.key());
            a.append(QJsonArray{p->id, p->type, p->irPath, knobIds});
            continue;
        }
        const RackSplit &s = *item.split();
        QJsonArray lanes;
        for (const RackLane &lane : s.lanes)
            lanes.append(shapeItems(lane.items));
        a.append(QJsonArray{s.id, modeName(s.mode), s.crossfade, int(s.crossovers.size()), lanes});
    }
    return a;
}

} // namespace

QJsonObject toJson(const AudioRack &rack)
{
    QJsonArray mods;
    for (const RackModulator &m : rack.modulators) {
        QJsonObject o{{"id", m.id}, {"type", m.type}, {"knobs", knobsToJson(m.knobs)}};
        if (m.type == QLatin1String("steps"))
            o["steps"] = floatsToJson(m.steps);
        if (m.type == QLatin1String("envelope"))
            o["source"] = m.source;
        mods.append(o);
    }
    QJsonArray routes;
    for (const RackRoute &r : rack.routes)
        routes.append(QJsonObject{{"id", r.id}, {"from", r.from}, {"to", r.to}, {"knob", r.knob},
                                  {"depth", double(r.depth)}});
    return {{"items", itemsToJson(rack.items)}, {"modulators", mods}, {"routes", routes}};
}

AudioRack fromJson(const QJsonObject &json)
{
    AudioRack rack;
    rack.items = itemsFromJson(json["items"].toArray());
    for (const QJsonValue &v : json["modulators"].toArray()) {
        const QJsonObject o = v.toObject();
        RackModulator m;
        m.id = o["id"].toString();
        m.type = o["type"].toString();
        m.knobs = knobsFromJson(o["knobs"].toObject());
        m.steps = floatsFromJson(o["steps"].toArray());
        m.source = o["source"].toString(QStringLiteral("input"));
        rack.modulators.push_back(std::move(m));
    }
    for (const QJsonValue &v : json["routes"].toArray()) {
        const QJsonObject o = v.toObject();
        RackRoute r;
        r.id = o["id"].toString();
        r.from = o["from"].toString();
        r.to = o["to"].toString();
        r.knob = o["knob"].toString();
        r.depth = float(o["depth"].toDouble(0.3));
        rack.routes.push_back(std::move(r));
    }
    return rack;
}

std::string graphJson(const AudioRack &rack)
{
    QJsonObject graph{{"version", 1}};
    if (!rack.modulators.isEmpty()) {
        QJsonArray mods;
        for (const RackModulator &m : rack.modulators) {
            QJsonObject o{{"id", m.id}, {"type", m.type}, {"knobs", knobsToJson(m.knobs)}};
            if (m.type == QLatin1String("steps"))
                o["steps"] = floatsToJson(m.steps.isEmpty() ? kDefaultSteps : m.steps);
            if (m.type == QLatin1String("envelope") && !m.source.isEmpty() && m.source != QLatin1String("input"))
                o["source"] = m.source;
            mods.append(o);
        }
        graph["modulators"] = mods;
    }
    graph["chain"] = graphChain(rack, rack.items);
    return QJsonDocument(graph).toJson(QJsonDocument::Compact).toStdString();
}

std::shared_ptr<const AudioGraphDesc> buildGraph(const AudioRack &rack, QString *error)
{
    std::string message;
    auto graph = parseAudioGraph(graphJson(rack), {}, {}, loadIrFile, &message);
    if (!graph && error)
        *error = QString::fromStdString(message);
    return graph;
}

QString shapeSignature(const AudioRack &rack)
{
    QJsonArray mods;
    for (const RackModulator &m : rack.modulators) {
        QJsonArray knobIds;
        for (auto it = m.knobs.begin(); it != m.knobs.end(); ++it)
            knobIds.append(it.key());
        mods.append(QJsonArray{m.id, m.type, m.source, int(m.steps.size()), knobIds});
    }
    QJsonArray routes;
    for (const RackRoute &r : rack.routes)
        routes.append(QJsonArray{r.from, r.to, r.knob});
    const QJsonArray all{shapeItems(rack.items), mods, routes};
    return QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact));
}

QVector<LiveValue> liveValues(const AudioRack &rack)
{
    QVector<LiveValue> out;
    walk(rack.items, 0, [&](const RackItem &item, int) {
        if (const RackPedal *p = item.pedal()) {
            if (const PedalSpec *spec = specFor(p->type))
                for (size_t i = 0; i < spec->knobs.size(); ++i) {
                    const KnobSpec &k = spec->knobs[i];
                    out.push_back({LiveValue::Kind::Knob, p->id, int(i), -1,
                                   p->knobs.value(QString::fromUtf8(k.id), k.defaultValue)});
                }
            out.push_back({LiveValue::Kind::Bypass, p->id, 0, -1, p->bypass ? 1.0f : 0.0f});
            return;
        }
        const RackSplit &s = *item.split();
        for (int i = 0; i < int(s.lanes.size()); ++i)
            out.push_back({LiveValue::Kind::LaneGain, s.id, i, -1, s.lanes[size_t(i)].gain});
        out.push_back({LiveValue::Kind::Knob, s.id, kSplitBlendKnob, -1, s.blend});
        if (s.mode == RackSplit::Mode::Bands)
            for (int i = 0; i < s.crossovers.size(); ++i)
                out.push_back({LiveValue::Kind::Knob, s.id, kSplitBlendKnob + 1 + i, -1, s.crossovers[i]});
    });
    for (const RackRoute &r : rack.routes) {
        const RackPedal *target = findPedal(rack, r.to);
        const PedalSpec *spec = target ? specFor(target->type) : nullptr;
        if (!spec)
            continue;
        QByteArray b;
        const int knob = knobIndex(*spec, sv(r.knob, b));
        int mod = -1;
        for (int i = 0; i < rack.modulators.size(); ++i)
            if (rack.modulators[i].id == r.from)
                mod = i;
        if (knob >= 0 && mod >= 0)
            out.push_back({LiveValue::Kind::RouteDepth, r.to, knob, mod, r.depth});
    }
    for (const RackModulator &m : rack.modulators) {
        if (const PedalSpec *spec = modSpecFor(m.type))
            for (size_t i = 0; i < spec->knobs.size(); ++i) {
                const KnobSpec &k = spec->knobs[i];
                out.push_back({LiveValue::Kind::ModKnob, m.id, int(i), -1,
                               m.knobs.value(QString::fromUtf8(k.id), k.defaultValue)});
            }
        for (int i = 0; i < m.steps.size(); ++i)
            out.push_back({LiveValue::Kind::Step, m.id, i, -1, m.steps[i]});
    }
    return out;
}

RackPedal *findPedal(AudioRack &rack, const QString &id)
{
    const Location l = locate(rack.items, id);
    return l.list ? (*l.list)[l.index].pedal() : nullptr;
}

const RackPedal *findPedal(const AudioRack &rack, const QString &id)
{
    return findPedal(const_cast<AudioRack &>(rack), id);
}

RackSplit *findSplit(AudioRack &rack, const QString &id)
{
    const Location l = locate(rack.items, id);
    return l.list ? (*l.list)[l.index].split() : nullptr;
}

const RackSplit *findSplit(const AudioRack &rack, const QString &id)
{
    return findSplit(const_cast<AudioRack &>(rack), id);
}

int nodeCount(const AudioRack &rack)
{
    int count = 0;
    walk(rack.items, 0, [&](const RackItem &, int) { ++count; });
    return count;
}

bool addPedal(AudioRack &rack, const QString &type, const RackSlot &slot, QString *newId)
{
    const PedalSpec *spec = specFor(type);
    int depth = 0;
    std::vector<RackItem> *list = listAt(rack, slot, &depth);
    if (!spec || !list || nodeCount(rack) >= kMaxGraphNodes)
        return false;
    RackPedal pedal;
    pedal.id = uniqueId(rack, QStringLiteral("p"));
    pedal.type = type;
    pedal.knobs = defaultKnobs(*spec);
    if (newId)
        *newId = pedal.id;
    insertAt(*list, RackItem{std::move(pedal)}, slot.index);
    return true;
}

bool addSplit(AudioRack &rack, RackSplit::Mode mode, int lanes, const RackSlot &slot, QString *newId)
{
    int depth = 0;
    std::vector<RackItem> *list = listAt(rack, slot, &depth);
    if (!list || depth >= kMaxSplitDepth || nodeCount(rack) >= kMaxGraphNodes)
        return false;
    RackSplit split;
    split.id = uniqueId(rack, QStringLiteral("s"));
    split.mode = mode;
    split.lanes.resize(size_t(std::clamp(lanes, 2, kMaxSplitLanes)));
    if (mode == RackSplit::Mode::Bands)
        split.crossovers = defaultCrossovers(int(split.lanes.size()));
    if (newId)
        *newId = split.id;
    insertAt(*list, RackItem{std::move(split)}, slot.index);
    return true;
}

bool addLane(AudioRack &rack, const QString &splitId)
{
    RackSplit *split = findSplit(rack, splitId);
    if (!split || int(split->lanes.size()) >= kMaxSplitLanes)
        return false;
    split->lanes.emplace_back();
    if (split->mode == RackSplit::Mode::Bands)
        split->crossovers = defaultCrossovers(int(split->lanes.size()));
    if (split->lanes.size() > 2)
        split->crossfade = false;
    return true;
}

bool removeLane(AudioRack &rack, const QString &splitId, int lane)
{
    RackSplit *split = findSplit(rack, splitId);
    if (!split || split->lanes.size() <= 2 || lane < 0 || lane >= int(split->lanes.size()))
        return false;
    const QSet<QString> gone = idsUnder(split->lanes[size_t(lane)].items);
    split->lanes.erase(split->lanes.begin() + lane);
    if (split->mode == RackSplit::Mode::Bands)
        split->crossovers.resize(int(split->lanes.size()) - 1);
    dropReferences(rack, gone);
    return true;
}

bool removeItem(AudioRack &rack, const QString &id)
{
    const Location l = locate(rack.items, id);
    if (!l.list)
        return false;
    QSet<QString> gone;
    {
        std::vector<RackItem> one{(*l.list)[l.index]};
        gone = idsUnder(one);
    }
    l.list->erase(l.list->begin() + std::ptrdiff_t(l.index));
    dropReferences(rack, gone);
    return true;
}

bool moveItem(AudioRack &rack, const QString &id, const RackSlot &to)
{
    const Location from = locate(rack.items, id);
    int depth = 0;
    std::vector<RackItem> *target = listAt(rack, to, &depth);
    if (!from.list || !target)
        return false;
    RackItem item = (*from.list)[from.index];
    if (!to.split.isEmpty()) {
        std::vector<RackItem> one{item};
        if (idsUnder(one).contains(to.split))
            return false;
    }
    if (depth + splitDepthBelow(item) > kMaxSplitDepth)
        return false;

    from.list->erase(from.list->begin() + std::ptrdiff_t(from.index));
    target = listAt(rack, to, &depth);
    int index = to.index;
    // The removal shifted later slots of the same list down by one.
    if (target == from.list && index > int(from.index))
        --index;
    insertAt(*target, std::move(item), index);
    return true;
}

bool addModulator(AudioRack &rack, const QString &type, QString *newId)
{
    const PedalSpec *spec = modSpecFor(type);
    if (!spec)
        return false;
    RackModulator m;
    m.id = uniqueId(rack, QStringLiteral("m"));
    m.type = type;
    m.knobs = defaultKnobs(*spec);
    if (type == QLatin1String("steps"))
        m.steps = kDefaultSteps;
    if (newId)
        *newId = m.id;
    rack.modulators.push_back(std::move(m));
    return true;
}

bool removeModulator(AudioRack &rack, const QString &id)
{
    const auto it = std::find_if(rack.modulators.begin(), rack.modulators.end(),
                                 [&](const RackModulator &m) { return m.id == id; });
    if (it == rack.modulators.end())
        return false;
    rack.modulators.erase(it);
    rack.routes.erase(std::remove_if(rack.routes.begin(), rack.routes.end(),
                                     [&](const RackRoute &r) { return r.from == id; }),
                      rack.routes.end());
    return true;
}

bool addRoute(AudioRack &rack, const QString &from, const QString &to, const QString &knob, float depth,
              QString *newId)
{
    const bool haveMod = std::any_of(rack.modulators.begin(), rack.modulators.end(),
                                     [&](const RackModulator &m) { return m.id == from; });
    const RackPedal *pedal = findPedal(rack, to);
    const PedalSpec *spec = pedal ? specFor(pedal->type) : nullptr;
    QByteArray b;
    const KnobSpec *k = spec ? findKnob(*spec, sv(knob, b)) : nullptr;
    if (!haveMod || !k || !k->continuous())
        return false;
    const QString knobId = QString::fromUtf8(k->id);
    depth = std::clamp(depth, -1.0f, 1.0f);
    for (RackRoute &r : rack.routes)
        if (r.from == from && r.to == to && r.knob == knobId) {
            r.depth = depth;
            if (newId)
                *newId = r.id;
            return true;
        }
    RackRoute route{uniqueId(rack, QStringLiteral("r")), from, to, knobId, depth};
    if (newId)
        *newId = route.id;
    rack.routes.push_back(std::move(route));
    return true;
}

bool removeRoute(AudioRack &rack, const QString &routeId)
{
    const auto it = std::find_if(rack.routes.begin(), rack.routes.end(),
                                 [&](const RackRoute &r) { return r.id == routeId; });
    if (it == rack.routes.end())
        return false;
    rack.routes.erase(it);
    return true;
}

} // namespace prism::audiofx
