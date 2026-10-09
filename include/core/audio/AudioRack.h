#pragma once

#include "core/audio/dsp/AudioGraph.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace prism::audiofx {

struct RackPedal
{
    QString id;
    QString type;
    // Keyed by PedalSpec knob id; a missing knob is at its spec default.
    QMap<QString, float> knobs;
    bool bypass = false;
    // Convolution only: absolute path of a WAV impulse response.
    QString irPath;
};

struct RackItem;

struct RackLane
{
    float gain = 1.0f;
    std::vector<RackItem> items;
};

struct RackSplit
{
    enum class Mode { Parallel, Bands };

    QString id;
    Mode mode = Mode::Parallel;
    bool crossfade = false;
    float blend = 0.5f;
    // Bands only: lanes.size() - 1 ascending frequencies.
    QVector<float> crossovers;
    std::vector<RackLane> lanes;
};

struct RackItem
{
    std::variant<RackPedal, RackSplit> v;

    bool isSplit() const { return std::holds_alternative<RackSplit>(v); }
    RackPedal *pedal() { return std::get_if<RackPedal>(&v); }
    const RackPedal *pedal() const { return std::get_if<RackPedal>(&v); }
    RackSplit *split() { return std::get_if<RackSplit>(&v); }
    const RackSplit *split() const { return std::get_if<RackSplit>(&v); }
    const QString &id() const { return isSplit() ? std::get<RackSplit>(v).id : std::get<RackPedal>(v).id; }
};

struct RackModulator
{
    QString id;
    QString type; // lfo | envelope | steps
    QMap<QString, float> knobs;
    QVector<float> steps; // steps only, each 0..1
    QString source = QStringLiteral("input"); // envelope only: "input" or a pedal/split id
};

struct RackRoute
{
    QString id;
    QString from; // modulator id
    QString to;   // pedal id
    QString knob; // knob id of the pedal
    float depth = 0.3f; // fraction of the knob's range, -1..1
};

struct AudioRack
{
    std::vector<RackItem> items;
    QVector<RackModulator> modulators;
    QVector<RackRoute> routes;

    bool empty() const { return items.empty(); }
};

// Where an item goes: the top level when `split` is empty, otherwise a lane of that split.
// index < 0 appends.
struct RackSlot
{
    QString split;
    int lane = 0;
    int index = -1;
};

QJsonObject toJson(const AudioRack &rack);
AudioRack fromJson(const QJsonObject &json);

// The "graph" object parseAudioGraph reads.
std::string graphJson(const AudioRack &rack);
// Null with *error set when the manifest is rejected (unknown type, convolution without a readable IR).
std::shared_ptr<const AudioGraphDesc> buildGraph(const AudioRack &rack, QString *error = nullptr);

// Changes when the graph's structure changes, not when only a literal value does.
QString shapeSignature(const AudioRack &rack);

// A value the running GraphProcessor can take without a rebuild. `node` is a pedal or split id for
// Knob, Bypass, RouteDepth and LaneGain, and a modulator id for ModKnob and Step. `index` is the knob
// index (a split's blend is 0, its crossovers follow), lane index or step index. `mod` is the
// modulator's position in AudioRack::modulators (RouteDepth only), which is also its position in the
// built graph.
struct LiveValue
{
    enum class Kind { Knob, Bypass, RouteDepth, LaneGain, ModKnob, Step };

    Kind kind = Kind::Knob;
    QString node;
    int index = 0;
    int mod = -1;
    float value = 0.0f;
};

QVector<LiveValue> liveValues(const AudioRack &rack);

RackPedal *findPedal(AudioRack &rack, const QString &id);
const RackPedal *findPedal(const AudioRack &rack, const QString &id);
RackSplit *findSplit(AudioRack &rack, const QString &id);
const RackSplit *findSplit(const AudioRack &rack, const QString &id);
int nodeCount(const AudioRack &rack);

// Every op returns false and leaves the rack untouched when it would break a limit or the target
// does not exist. `newId` receives the generated id.
bool addPedal(AudioRack &rack, const QString &type, const RackSlot &slot = {}, QString *newId = nullptr);
bool addSplit(AudioRack &rack, RackSplit::Mode mode, int lanes = 2, const RackSlot &slot = {},
              QString *newId = nullptr);
bool addLane(AudioRack &rack, const QString &splitId);
bool removeLane(AudioRack &rack, const QString &splitId, int lane);
bool removeItem(AudioRack &rack, const QString &id);
bool moveItem(AudioRack &rack, const QString &id, const RackSlot &to);
bool addModulator(AudioRack &rack, const QString &type, QString *newId = nullptr);
bool removeModulator(AudioRack &rack, const QString &id);
// One route per modulator and knob: routing again sets its depth. Rejects toggle/choice knobs.
bool addRoute(AudioRack &rack, const QString &from, const QString &to, const QString &knob, float depth = 0.3f,
              QString *newId = nullptr);
bool removeRoute(AudioRack &rack, const QString &routeId);

} // namespace prism::audiofx
