#include "app/BpmClock.h"
#include "app/graph/ModulatorEngine.h"

#include <QtTest>
#include <cmath>

using Kind = ModulatorEngine::Kind;

namespace {

constexpr NodeId kId = 1;

QVariantMap lfo(int shape, double rateHz = 1.0) {
    return {{"kind", int(Kind::Lfo)}, {"shape", shape}, {"rateHz", rateHz}};
}

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

double tickAt(ModulatorEngine &e, double t, double beat = 0.0, const ModulatorEngine::InputResolver &r = {}) {
    e.tick({t, beat}, r);
    return e.value(kId);
}

} // namespace

class TestModulators : public QObject {
    Q_OBJECT
private slots:
    void controlInScalesOscInputThroughResolver() {
        ModulatorEngine e;
        e.setNode(kId, {{"kind", int(Kind::ControlIn)}, {"in", QStringLiteral("osc:/fader/1")},
                        {"min", 10.0}, {"max", 20.0}});
        QString asked;
        const auto resolver = [&](const QString &spec) {
            asked = spec;
            return 0.5;
        };
        QVERIFY(near(tickAt(e, 0.0, 0.0, resolver), 15.0));
        QCOMPARE(asked, QStringLiteral("osc:/fader/1"));
        const auto over = [](const QString &) { return 2.0; };
        QVERIFY(near(tickAt(e, 0.1, 0.0, over), 20.0));
        QVERIFY(near(tickAt(e, 0.2), 10.0));
    }

    void lfoSineAtOneHertz() {
        ModulatorEngine e;
        e.setNode(kId, lfo(0));
        QVERIFY(near(tickAt(e, 0.0), 0.5));
        QVERIFY(near(tickAt(e, 0.25), 1.0));
        QVERIFY(near(tickAt(e, 0.5), 0.5));
        QVERIFY(near(tickAt(e, 0.75), 0.0));
    }

    void lfoRangeAndPhase() {
        ModulatorEngine e;
        QVariantMap p = lfo(0);
        p["min"] = 10.0;
        p["max"] = 20.0;
        p["phase"] = 0.25;
        e.setNode(kId, p);
        QVERIFY(near(tickAt(e, 0.0), 20.0));
    }

    void lfoShapes() {
        ModulatorEngine e;
        e.setNode(kId, lfo(1));
        QVERIFY(near(tickAt(e, 0.0), 0.5));
        QVERIFY(near(tickAt(e, 0.25), 1.0));
        QVERIFY(near(tickAt(e, 0.75), 0.0));
        e.setNode(kId, lfo(2));
        QVERIFY(near(tickAt(e, 0.25), 0.25));
        e.setNode(kId, lfo(3));
        QVERIFY(near(tickAt(e, 0.25), 1.0));
        QVERIFY(near(tickAt(e, 0.75), 0.0));
        QVariantMap pulse = lfo(4);
        pulse["width"] = 0.2;
        e.setNode(kId, pulse);
        QVERIFY(near(tickAt(e, 0.1), 1.0));
        QVERIFY(near(tickAt(e, 0.3), 0.0));
    }

    void lfoBeatSyncedFollowsBpmClock() {
        qint64 now = 0;
        BpmClock clock(nullptr, [&now] { return now; });
        QCOMPARE(clock.bpm(), 120.0);

        ModulatorEngine e;
        QVariantMap p = lfo(0);
        p["sync"] = true;
        p["rateBeats"] = 1.0;
        e.setNode(kId, p);

        now = 125;
        QVERIFY(near(clock.beats(), 0.25));
        QVERIFY(near(tickAt(e, now / 1000.0, clock.beats()), 1.0));
        now = 250;
        QVERIFY(near(tickAt(e, now / 1000.0, clock.beats()), 0.5));
        now = 375;
        QVERIFY(near(tickAt(e, now / 1000.0, clock.beats()), 0.0));

        p["rateBeats"] = 4.0;
        e.setNode(kId, p);
        now = 500;
        QVERIFY(near(tickAt(e, now / 1000.0, clock.beats()), 0.5 + 0.5 * std::sin(2 * M_PI * 0.25)));
    }

    void randomIsSeededAndStepped() {
        QVariantMap p{{"kind", int(Kind::Random)}, {"rateHz", 1.0}, {"seed", 42}, {"min", 0.0}, {"max", 1.0}};
        ModulatorEngine a, b;
        a.setNode(kId, p);
        b.setNode(kId, p);
        const double first = tickAt(a, 0.1);
        QCOMPARE(tickAt(b, 0.1), first);
        QCOMPARE(tickAt(a, 0.9), first);
        QVERIFY(tickAt(a, 1.1) != first);
        QVERIFY(first >= 0.0 && first <= 1.0);

        p["seed"] = 43;
        ModulatorEngine c;
        c.setNode(kId, p);
        QVERIFY(tickAt(c, 0.1) != first);

        p["seed"] = 42;
        p["smooth"] = true;
        ModulatorEngine s;
        s.setNode(kId, p);
        const double at0 = tickAt(s, 0.0);
        QVERIFY(near(at0, first));
        const double mid = tickAt(s, 0.5);
        QVERIFY(mid != at0);
    }

    void envelopeFollowerAttackRelease() {
        ModulatorEngine e;
        e.setNode(kId, {{"kind", int(Kind::Envelope)}, {"in", "1"}, {"attackMs", 10.0}, {"releaseMs", 100.0}});
        QCOMPARE(tickAt(e, 0.0), 0.0);
        const double up = tickAt(e, 0.01);
        QVERIFY(near(up, 1.0 - std::exp(-1.0)));

        e.setNode(kId, {{"kind", int(Kind::Envelope)}, {"in", "0"}, {"attackMs", 10.0}, {"releaseMs", 100.0}});
        const double down = tickAt(e, 0.02);
        QVERIFY(near(down, up * std::exp(-0.1)));
        QVERIFY(down > 0.0);

        e.setNode(kId, {{"kind", int(Kind::Envelope)}, {"in", "1"}, {"attackMs", 0.0}, {"releaseMs", 100.0}});
        QVERIFY(near(tickAt(e, 0.03), 1.0));
    }

    void connectedSocketOverridesSpec() {
        ModulatorEngine e;
        e.setNode(kId, {{"kind", int(Kind::Envelope)}, {"in", "0.1"}, {"attackMs", 0.0}, {"releaseMs", 0.0}});
        QString askedKey;
        const auto socket = [&](NodeId id, const QString &key) -> std::optional<double> {
            askedKey = key;
            return id == kId ? std::optional<double>(0.8) : std::nullopt;
        };
        e.tick({0.0, 0.0}, {}, socket);
        e.tick({0.1, 0.0}, {}, socket);
        QVERIFY(near(e.value(kId), 0.8));
        QCOMPARE(askedKey, QStringLiteral("in"));
        e.tick({0.2, 0.0}, {}, [](NodeId, const QString &) { return std::optional<double>(); });
        QVERIFY(near(e.value(kId), 0.1));
    }

    void mathSocketsAreIndependent() {
        ModulatorEngine e;
        e.setNode(kId, {{"kind", int(Kind::Math)}, {"op", 2}, {"in", "3"}, {"b", "5"}});
        e.tick({0.0, 0.0}, {}, [](NodeId, const QString &key) {
            return key == QLatin1String("b") ? std::optional<double>(2.0) : std::nullopt;
        });
        QVERIFY(near(e.value(kId), 6.0));
    }

    void controlInIgnoresSocketResolver() {
        ModulatorEngine e;
        e.setNode(kId, {{"kind", int(Kind::ControlIn)}, {"in", "0.5"}});
        e.tick({0.0, 0.0}, {}, [](NodeId, const QString &) { return std::optional<double>(1.0); });
        QVERIFY(near(e.value(kId), 0.5));
    }

    void unconnectedSpecOnlyAcceptsLiteralOrOsc() {
        ModulatorEngine e;
        e.setNode(kId, {{"kind", int(Kind::Smooth)}, {"in", "low"}, {"timeMs", 0.0}});
        const auto resolver = [](const QString &) { return 0.8; };
        QVERIFY(near(tickAt(e, 0.0, 0.0, resolver), 0.0));
        e.setNode(kId, {{"kind", int(Kind::Smooth)}, {"in", "osc:/x"}, {"timeMs", 0.0}});
        QVERIFY(near(tickAt(e, 0.1, 0.0, resolver), 0.8));
    }

    void smoothLagsTowardsInput() {
        ModulatorEngine e;
        e.setNode(kId, {{"kind", int(Kind::Smooth)}, {"in", "10"}, {"timeMs", 100.0}});
        tickAt(e, 0.0);
        QVERIFY(near(tickAt(e, 0.1), 10.0 * (1.0 - std::exp(-1.0))));
    }

    void mapRange() {
        auto map = [](const char *in, bool clamp, int curve) {
            return QVariantMap{{"kind", int(Kind::MapRange)}, {"in", in}, {"inMin", 0.0}, {"inMax", 10.0},
                               {"outMin", 0.0}, {"outMax", 100.0}, {"clamp", clamp}, {"curve", curve}};
        };
        ModulatorEngine e;
        e.setNode(kId, map("5", true, 0));
        QVERIFY(near(tickAt(e, 0.0), 50.0));
        e.setNode(kId, map("20", true, 0));
        QVERIFY(near(tickAt(e, 0.1), 100.0));
        e.setNode(kId, map("20", false, 0));
        QVERIFY(near(tickAt(e, 0.2), 200.0));
        e.setNode(kId, map("5", true, 1));
        QVERIFY(near(tickAt(e, 0.3), 25.0));
        e.setNode(kId, map("5", true, 2));
        QVERIFY(near(tickAt(e, 0.4), 75.0));
    }

    void outputRangeFollowsKindParams() {
        const auto range = [](const QVariantMap &p) { return ModulatorEngine::outputRange(p); };
        QCOMPARE(range({{"kind", int(Kind::Lfo)}, {"min", 5.0}, {"max", -1.0}}), std::make_optional(std::make_pair(-1.0, 5.0)));
        QCOMPARE(range({{"kind", int(Kind::MapRange)}, {"outMin", 0.0}, {"outMax", 100.0}}), std::make_optional(std::make_pair(0.0, 100.0)));
        QCOMPARE(range({{"kind", int(Kind::Counter)}, {"steps", 8}}), std::make_optional(std::make_pair(0.0, 7.0)));
        QCOMPARE(range({{"kind", int(Kind::Toggle)}}), std::make_optional(std::make_pair(0.0, 1.0)));
        QVERIFY(!range({{"kind", int(Kind::Smooth)}}).has_value());
    }

    void triggerPulsesForDuration() {
        ModulatorEngine e;
        auto trig = [](const char *in) {
            return QVariantMap{{"kind", int(Kind::Trigger)}, {"in", in}, {"threshold", 0.5}, {"durationMs", 100.0}};
        };
        e.setNode(kId, trig("0"));
        QCOMPARE(tickAt(e, 1.0), 0.0);
        e.setNode(kId, trig("1"));
        QCOMPARE(tickAt(e, 1.01), 1.0);
        QCOMPARE(tickAt(e, 1.09), 1.0);
        QCOMPARE(tickAt(e, 1.2), 0.0);
        QCOMPARE(tickAt(e, 1.3), 0.0);
    }

    void counterAndToggleCountRisingEdgesOnly() {
        double level = 0.0;
        const auto resolver = [&level](const QString &) { return level; };
        ModulatorEngine e;
        e.setNode(kId, {{"kind", int(Kind::Counter)}, {"in", "osc:src"}, {"steps", 3}});
        e.setNode(2, {{"kind", int(Kind::Toggle)}, {"in", "osc:src"}});

        QList<double> counts, toggles;
        double t = 0.0;
        for (double v : {0.0, 1.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0}) {
            level = v;
            e.tick({t += 0.1, 0.0}, resolver);
            counts.append(e.value(kId));
            toggles.append(e.value(2));
        }
        QCOMPARE(counts, (QList<double>{0, 1, 1, 1, 2, 2, 0, 0, 1}));
        QCOMPARE(toggles, (QList<double>{0, 1, 1, 1, 0, 0, 1, 1, 0}));
    }

    void mathOps() {
        auto math = [](int op, const char *a, const char *b) {
            return QVariantMap{{"kind", int(Kind::Math)}, {"op", op}, {"in", a}, {"b", b}};
        };
        ModulatorEngine e;
        double t = 0.0;
        auto run = [&](int op, const char *a, const char *b) {
            e.setNode(kId, math(op, a, b));
            return tickAt(e, t += 0.1);
        };
        QVERIFY(near(run(0, "6", "4"), 10.0));
        QVERIFY(near(run(1, "6", "4"), 2.0));
        QVERIFY(near(run(2, "6", "4"), 24.0));
        QVERIFY(near(run(3, "6", "4"), 1.5));
        QVERIFY(near(run(3, "6", "0"), 0.0));
        QVERIFY(near(run(4, "6", "4"), 4.0));
        QVERIFY(near(run(5, "6", "4"), 6.0));
        QVERIFY(near(run(6, "6", "4"), 1296.0));
        QVERIFY(near(run(6, "-8", "0.5"), 0.0));
        QVERIFY(near(run(7, "-3", "99"), 3.0));
    }

    void modulatorsCanReadEachOther() {
        ModulatorEngine e;
        e.setNode(1, {{"kind", int(Kind::MapRange)}, {"in", "5"}, {"inMax", 10.0}, {"outMax", 1.0}});
        e.setNode(2, {{"kind", int(Kind::Math)}, {"op", 2}, {"in", "mod:1"}, {"b", "4"}});
        e.tick({0.0, 0.0});
        QCOMPARE(e.value(2), 0.0);
        const auto socket = [&](NodeId id, const QString &key) {
            return id == 2 && key == QLatin1String("in") ? std::optional<double>(e.value(1)) : std::nullopt;
        };
        e.tick({0.1, 0.0}, {}, socket);
        QVERIFY(near(e.value(2), 2.0));
    }

    void bpmClockTapTempo() {
        qint64 now = 0;
        BpmClock clock(nullptr, [&now] { return now; });
        for (int i = 0; i < 4; ++i) {
            now = i * 400;
            clock.tap();
        }
        QVERIFY(near(clock.bpm(), 150.0));

        for (int i = 0; i < 4; ++i) {
            now = 10000 + i * 500;
            clock.tap();
        }
        QVERIFY(near(clock.bpm(), 120.0));

        now = 20000;
        clock.tap();
        QVERIFY2(near(clock.bpm(), 120.0), "a single tap after a gap must not change the tempo");
        now = 20750;
        clock.tap();
        QVERIFY(near(clock.bpm(), 80.0));
    }

    void bpmClockUsesOnlyLastFourTaps() {
        qint64 now = 0;
        BpmClock clock(nullptr, [&now] { return now; });
        for (qint64 t : {0, 1000, 2000, 2500, 3000, 3500}) {
            now = t;
            clock.tap();
        }
        QVERIFY(near(clock.bpm(), 120.0));
    }

    void bpmClockBeatPhase() {
        qint64 now = 0;
        BpmClock clock(nullptr, [&now] { return now; });
        now = 2750;
        QVERIFY(near(clock.beats(), 5.5));
        QCOMPARE(clock.beatCount(), 5);
        QVERIFY(near(clock.beatPhase(), 0.5));
        QVERIFY(near(clock.barPhase(), (5.5 / 4.0) - 1.0));

        clock.setBpm(60.0);
        now = 3750;
        QVERIFY(near(clock.beats(), 6.5));
        clock.setBpm(5000.0);
        QCOMPARE(clock.bpm(), BpmClock::kMaxBpm);
    }

    void bpmClockNudgeShiftsPhaseTemporarily() {
        qint64 now = 0;
        BpmClock clock(nullptr, [&now] { return now; });
        clock.nudge(1);
        now = 250;
        const double nudged = clock.beats();
        QVERIFY(near(nudged, 0.5 * (1.0 + BpmClock::kNudgeRate)));
        now = 750;
        QVERIFY(near(clock.beats() - nudged, 1.0));
    }
};

QTEST_MAIN(TestModulators)
#include "test_modulators.moc"
