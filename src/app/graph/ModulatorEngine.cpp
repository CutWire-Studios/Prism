#include "app/graph/ModulatorEngine.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

using Kind = ModulatorEngine::Kind;
using Shape = ModulatorEngine::Shape;
using MathOp = ModulatorEngine::MathOp;

double num(const QVariantMap &p, const char *key, double def) {
    const QVariant v = p.value(QLatin1String(key));
    return v.isValid() ? v.toDouble() : def;
}

bool flag(const QVariantMap &p, const char *key, bool def) {
    const QVariant v = p.value(QLatin1String(key));
    return v.isValid() ? v.toBool() : def;
}

double finiteOr0(double v) { return std::isfinite(v) ? v : 0.0; }

double frac(double v) { return v - std::floor(v); }

double hash01(qint64 seed, qint64 n) {
    quint64 z = quint64(seed) * 0x9E3779B97F4A7C15ull + quint64(n) * 0xBF58476D1CE4E5B9ull + 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) * (1.0 / 9007199254740992.0);
}

// One-pole smoothing factor for a time constant; zero or negative time passes the input through.
double onePole(double dt, double ms) {
    if (ms <= 0.0)
        return 1.0;
    return 1.0 - std::exp(-dt / (ms * 0.001));
}

double cyclesOf(const QVariantMap &p, const ModulatorClock &clk) {
    if (flag(p, "sync", false)) {
        const double beats = std::max(1e-6, num(p, "rateBeats", 1.0));
        return clk.beat / beats;
    }
    return clk.time * num(p, "rateHz", 1.0);
}

} // namespace

void ModulatorEngine::setNode(NodeId id, const QVariantMap &params) {
    Node &n = m_nodes[id];
    const int kind = params.value(QStringLiteral("kind")).toInt();
    if (kind != n.kind) {
        n = Node();
        n.kind = kind;
    }
    n.p = params;
}

void ModulatorEngine::removeNode(NodeId id) { m_nodes.erase(id); }

void ModulatorEngine::clear() {
    m_nodes.clear();
    m_hasTime = false;
}

double ModulatorEngine::value(NodeId id) const {
    const auto it = m_nodes.find(id);
    return it == m_nodes.end() ? 0.0 : it->second.value;
}

QList<NodeId> ModulatorEngine::ids() const {
    QList<NodeId> out;
    for (const auto &[id, n] : m_nodes)
        out.append(id);
    return out;
}

void ModulatorEngine::tick(const ModulatorClock &clk, const InputResolver &resolve, const SocketResolver &socket) {
    const double dt = m_hasTime ? std::max(0.0, clk.time - m_lastTime) : 0.0;
    m_hasTime = true;
    m_lastTime = clk.time;

    const InputFn input = [&](NodeId id, const char *key, const QString &specIn) -> double {
        if (key && socket) {
            if (const std::optional<double> v = socket(id, QLatin1String(key)))
                return finiteOr0(*v);
        }
        const QString spec = specIn.trimmed();
        if (spec.isEmpty())
            return 0.0;
        bool isNumber = false;
        const double literal = spec.toDouble(&isNumber);
        if (isNumber)
            return literal;
        if (spec.startsWith(QLatin1String("osc:")) && resolve)
            return finiteOr0(resolve(spec));
        return 0.0;
    };

    for (auto &[id, n] : m_nodes)
        n.value = finiteOr0(evaluate(id, n, clk, dt, input));
}

std::optional<std::pair<double, double>> ModulatorEngine::outputRange(const QVariantMap &p) {
    const auto sorted = [](double a, double b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
    switch (Kind(p.value(QStringLiteral("kind")).toInt())) {
    case Kind::Lfo:
    case Kind::Random:
    case Kind::ControlIn: return sorted(num(p, "min", 0.0), num(p, "max", 1.0));
    case Kind::MapRange:  return sorted(num(p, "outMin", 0.0), num(p, "outMax", 1.0));
    case Kind::Trigger:
    case Kind::Toggle:    return std::make_pair(0.0, 1.0);
    case Kind::Counter:   return std::make_pair(0.0, double(std::max(1, int(num(p, "steps", 4))) - 1));
    case Kind::Envelope:
    case Kind::Smooth:
    case Kind::Math:      break;
    }
    return std::nullopt;
}

double ModulatorEngine::evaluate(NodeId id, Node &n, const ModulatorClock &clk, double dt, const InputFn &input) const {
    const QVariantMap &p = n.p;
    const bool hasSocket = Kind(n.kind) != Kind::ControlIn;
    const auto in = [&] { return input(id, hasSocket ? "in" : nullptr, p.value(QStringLiteral("in")).toString()); };

    switch (Kind(n.kind)) {
    case Kind::Lfo: {
        const double cycles = cyclesOf(p, clk) + num(p, "phase", 0.0);
        const double ph = frac(cycles);
        double u = 0.0;
        switch (Shape(p.value(QStringLiteral("shape")).toInt())) {
        case Shape::Sine:     u = 0.5 + 0.5 * std::sin(2.0 * M_PI * ph); break;
        case Shape::Triangle: u = ph < 0.25 ? 0.5 + 2.0 * ph : ph < 0.75 ? 1.5 - 2.0 * ph : 2.0 * ph - 1.5; break;
        case Shape::Saw:      u = ph; break;
        case Shape::Square:   u = ph < 0.5 ? 1.0 : 0.0; break;
        case Shape::Pulse:    u = ph < std::clamp(num(p, "width", 0.5), 0.0, 1.0) ? 1.0 : 0.0; break;
        case Shape::Random:   u = hash01(p.value(QStringLiteral("seed")).toLongLong(), qint64(std::floor(cycles))); break;
        }
        const double lo = num(p, "min", 0.0), hi = num(p, "max", 1.0);
        return lo + (hi - lo) * u;
    }
    case Kind::Envelope: {
        const double x = in();
        const double a = onePole(dt, x > n.value ? num(p, "attackMs", 10.0) : num(p, "releaseMs", 200.0));
        return n.value + (x - n.value) * a;
    }
    case Kind::MapRange: {
        const double x = in();
        const double inMin = num(p, "inMin", 0.0), inMax = num(p, "inMax", 1.0);
        double t = inMax == inMin ? 0.0 : (x - inMin) / (inMax - inMin);
        if (flag(p, "clamp", true))
            t = std::clamp(t, 0.0, 1.0);
        switch (p.value(QStringLiteral("curve")).toInt()) {
        case 1: t = t * t; break;
        case 2: t = 1.0 - (1.0 - t) * (1.0 - t); break;
        default: break;
        }
        const double outMin = num(p, "outMin", 0.0), outMax = num(p, "outMax", 1.0);
        return outMin + (outMax - outMin) * t;
    }
    case Kind::Smooth: {
        const double x = in();
        return n.value + (x - n.value) * onePole(dt, num(p, "timeMs", 200.0));
    }
    case Kind::Random: {
        const double cycles = cyclesOf(p, clk);
        const qint64 seed = p.value(QStringLiteral("seed")).toLongLong();
        const qint64 step = qint64(std::floor(cycles));
        double u = hash01(seed, step);
        if (flag(p, "smooth", false)) {
            const double f = frac(cycles);
            const double s = f * f * (3.0 - 2.0 * f);
            u += (hash01(seed, step + 1) - u) * s;
        }
        const double lo = num(p, "min", 0.0), hi = num(p, "max", 1.0);
        return lo + (hi - lo) * u;
    }
    case Kind::Trigger:
    case Kind::Counter:
    case Kind::Toggle: {
        const double x = in();
        const double thr = num(p, "threshold", 0.5);
        const bool rising = x >= thr && n.prevIn < thr;
        n.prevIn = x;
        if (Kind(n.kind) == Kind::Trigger) {
            if (rising)
                n.triggerUntil = clk.time + num(p, "durationMs", 100.0) * 0.001;
            return clk.time < n.triggerUntil ? 1.0 : 0.0;
        }
        if (Kind(n.kind) == Kind::Counter) {
            if (rising)
                n.count = (n.count + 1) % std::max(1, int(num(p, "steps", 4)));
            return n.count;
        }
        if (rising)
            n.toggled = !n.toggled;
        return n.toggled ? 1.0 : 0.0;
    }
    case Kind::Math: {
        const double a = in();
        const double b = input(id, "b", p.value(QStringLiteral("b")).toString());
        switch (MathOp(p.value(QStringLiteral("op")).toInt())) {
        case MathOp::Add: return a + b;
        case MathOp::Sub: return a - b;
        case MathOp::Mul: return a * b;
        case MathOp::Div: return b == 0.0 ? 0.0 : a / b;
        case MathOp::Min: return std::min(a, b);
        case MathOp::Max: return std::max(a, b);
        case MathOp::Pow: return std::pow(a, b);
        case MathOp::Abs: return std::abs(a);
        }
        return 0.0;
    }
    case Kind::ControlIn: {
        double t = in();
        if (flag(p, "clamp", true))
            t = std::clamp(t, 0.0, 1.0);
        const double lo = num(p, "min", 0.0), hi = num(p, "max", 1.0);
        return lo + (hi - lo) * t;
    }
    }
    return 0.0;
}
