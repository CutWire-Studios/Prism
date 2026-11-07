#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>
#include <functional>
#include <map>

#include "core/graph/ClipGraph.h"

/// Time base handed to ModulatorEngine::tick. Tests inject fixed values.
struct ModulatorClock {
    double time = 0.0;   // seconds, monotonic
    double beat = 0.0;   // absolute beats (beat count plus phase) from the BpmClock
};

/// Evaluates Modulator nodes. Each node owns a float output ("value") that parameters read through
/// the `mod(<nodeId>)` expression function. Input specs (params "in" and "b") are, in order: empty
/// (0), a number literal, "mod:<nodeId>" (another modulator, one tick of lag when it is evaluated
/// later), or anything else, which is passed to the InputResolver (audio band names such as "low",
/// "mid", "high", "level", "beat", or "<analyzerId>:<band>").
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
class ModulatorEngine {
public:
    enum class Kind { Lfo, Envelope, MapRange, Smooth, Random, Trigger, Counter, Toggle, Math };
    enum class Shape { Sine, Triangle, Saw, Square, Pulse, Random };
    enum class MathOp { Add, Sub, Mul, Div, Min, Max, Pow, Abs };

    using InputResolver = std::function<double(const QString &spec)>;

    /// Creates or updates a node. Runtime state survives parameter edits of the same kind.
    void setNode(NodeId id, const QVariantMap &params);
    void removeNode(NodeId id);
    void clear();

    void tick(const ModulatorClock &clock, const InputResolver &resolve = {});

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

    double evaluate(Node &n, const ModulatorClock &clk, double dt,
                    const std::function<double(const QString &)> &input) const;

    std::map<NodeId, Node> m_nodes;
    double m_lastTime = 0.0;
    bool m_hasTime = false;
};
