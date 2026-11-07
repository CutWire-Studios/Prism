#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>
#include <functional>
#include <map>
#include <optional>

#include "core/graph/ClipGraph.h"

/// Time base handed to ModulatorEngine::tick. Tests inject fixed values.
struct ModulatorClock {
    double time = 0.0;   // seconds, monotonic
    double beat = 0.0;   // absolute beats (beat count plus phase) from the BpmClock
};

/// Evaluates Modulator nodes. Each node owns a float output ("value"); parameters read it through
/// the `input` of a wired socket. Kinds with inputs (all but Lfo, Random and ControlIn) have a
/// socket "in" (Math also "b"). A connected socket is answered by the SocketResolver. An
/// unconnected one falls back to the param spec: empty (0), a number literal, or "osc:<address>"
/// (the latest value received on that OSC address, via the InputResolver). Anything else is 0.
/// ControlIn has no socket and always reads its "in" spec. Modulator-to-modulator wiring has one
/// tick of lag when the source is evaluated later.
///
/// Params by kind (Kind enum order is persisted):
///   Lfo       shape, sync, rateHz, rateBeats, phase, width, seed, min, max
///   Envelope  in, attackMs, releaseMs
///   MapRange  in, inMin, inMax, outMin, outMax, clamp, curve
///   Smooth    in, timeMs
///   Random    smooth, sync, rateHz, rateBeats, seed, min, max
///   Trigger   in, threshold, durationMs
///   Counter   in, threshold, steps   (output cycles 0..steps-1)
///   Toggle    in, threshold
///   Math      in (a), b, op
///   ControlIn in, min, max, clamp   (in is read as 0..1 and scaled to min..max)
class ModulatorEngine {
public:
    enum class Kind { Lfo, Envelope, MapRange, Smooth, Random, Trigger, Counter, Toggle, Math, ControlIn };
    enum class Shape { Sine, Triangle, Saw, Square, Pulse, Random };
    enum class MathOp { Add, Sub, Mul, Div, Min, Max, Pow, Abs };

    using InputResolver = std::function<double(const QString &spec)>;
    using SocketResolver = std::function<std::optional<double>(NodeId, const QString &key)>;

    /// Creates or updates a node. Runtime state survives parameter edits of the same kind.
    void setNode(NodeId id, const QVariantMap &params);
    void removeNode(NodeId id);
    void clear();

    void tick(const ModulatorClock &clock, const InputResolver &resolve = {}, const SocketResolver &socket = {});

    /// Output range implied by a node's params, or nullopt when it follows its inputs
    /// (Envelope, Smooth, Math).
    static std::optional<std::pair<double, double>> outputRange(const QVariantMap &params);

    bool contains(NodeId id) const { return m_nodes.count(id) > 0; }
    double value(NodeId id) const;
    QList<NodeId> ids() const;

private:
    struct Node {
        QVariantMap p;
        int kind = 0;
        double value = 0.0;
        double prevIn = 0.0;
        double triggerUntil = 0.0;
        int count = 0;
        bool toggled = false;
    };

    using InputFn = std::function<double(NodeId, const char *key, const QString &spec)>;
    double evaluate(NodeId id, Node &n, const ModulatorClock &clk, double dt, const InputFn &input) const;

    std::map<NodeId, Node> m_nodes;
    double m_lastTime = 0.0;
    bool m_hasTime = false;
};
