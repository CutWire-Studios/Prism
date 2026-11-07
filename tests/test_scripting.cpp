#include "app/graph/GraphRuntime.h"
#include "core/graph/ClipGraph.h"
#include "core/graph/ClipGraphModels.h"
#include "core/scripting/ScriptRuntime.h"

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUndoStack>

using NT = ClipGraph::NodeType;

namespace {

QJsonObject outputOf(const std::shared_ptr<ScriptOutput> &out)
{
    QMutexLocker lock(&out->mutex);
    return QJsonDocument::fromJson(out->json.toUtf8()).object();
}

NodeId addScript(ClipGraph &g, const QString &code, int runOn = 3)
{
    return g.addNode(NT::Script, {}, {{"luaCode", code}, {"runOn", runOn}});
}

NodeId addOpacity(ClipGraph &g, double opacity = 0.5)
{
    return g.addNode(NT::Process, {}, {{"effect", 6}, {"params", QVariantMap{{"opacity", opacity}}}});
}

double opacityOf(const ClipGraph &g, NodeId id)
{
    return g.node(id)->params.value("params").toMap().value("opacity").toDouble();
}

} // namespace

class TestScripting : public QObject {
    Q_OBJECT

private slots:
    void sandboxRemovesDangerousLibraries()
    {
        auto out = std::make_shared<ScriptOutput>();
        ScriptRuntime rt(out);
        rt.setScript(R"(return { os = tostring(os), io = tostring(io), req = tostring(require),
            pkg = tostring(package), dbg = tostring(debug), df = tostring(dofile),
            lf = tostring(loadfile), year = prism.date("%Y") })");
        rt.runNow();
        QVERIFY2(rt.lastError().isEmpty(), qPrintable(rt.lastError()));
        const QJsonObject o = outputOf(out);
        for (const char *k : {"os", "io", "req", "pkg", "dbg", "df", "lf"})
            QCOMPARE(o.value(QLatin1String(k)).toString(), QStringLiteral("nil"));
        QCOMPARE(o.value("year").toString().size(), 4);

        for (const char *code : {"os.execute('true')", "io.open('/etc/passwd')", "require('os')"}) {
            rt.setScript(QString::fromLatin1(code));
            rt.runNow();
            QVERIFY2(!rt.lastError().isEmpty(), code);
        }
        rt.setScript("return load(string.dump(function() end)) == nil");
        rt.runNow();
        QVERIFY(rt.lastError().isEmpty());
    }

    void instructionLimitAbortsInfiniteLoop()
    {
        auto out = std::make_shared<ScriptOutput>();
        ScriptRuntime rt(out);
        QSignalSpy finished(&rt, &ScriptRuntime::executionFinished);
        rt.setInstructionLimit(200000);
        rt.setScript("while true do end");
        QElapsedTimer t;
        t.start();
        rt.runNow();
        QVERIFY(t.elapsed() < 5000);
        QVERIFY(rt.lastError().contains("instruction limit"));
        QCOMPARE(finished.last().at(0).toBool(), false);

        rt.setScript("return { ok = true }");
        rt.runNow();
        QVERIFY(rt.lastError().isEmpty());
        QCOMPARE(finished.last().at(0).toBool(), true);
        QVERIFY(outputOf(out).value("ok").toBool());
    }

    void printIsCapturedIntoRingBuffer()
    {
        ScriptRuntime rt(std::make_shared<ScriptOutput>());
        QSignalSpy logged(&rt, &ScriptRuntime::logChanged);
        rt.setScript("print('a', 1); prism.log('b')");
        rt.runNow();
        QCOMPARE(rt.log(), (QStringList{"a\t1", "b"}));
        QCOMPARE(logged.count(), 1);

        rt.setScript("for i = 1, 300 do print(i) end");
        rt.runNow();
        QCOMPARE(rt.log().size(), int(ScriptRuntime::kLogLines));
        QCOMPARE(rt.log().last(), QStringLiteral("300"));
    }

    void runOnModesReplaceTrigger()
    {
        auto out = std::make_shared<ScriptOutput>();
        ScriptRuntime rt(out);

        rt.applySettings("", int(ScriptTriggerMode::Interval), 40, "12:00:00");
        QTRY_VERIFY(out->version.load() >= 3);
        QVERIFY(outputOf(out).value("pulse").toBool());
        QVERIFY(outputOf(out).value("tick").toDouble() >= 3);

        rt.applySettings("", int(ScriptTriggerMode::Manual), 40, "12:00:00");
        const uint v = out->version.load();
        QTest::qWait(250);
        QCOMPARE(out->version.load(), v);
        rt.runNow();
        QCOMPARE(out->version.load(), v + 1);

        rt.applySettings("return { x = input.v }", int(ScriptTriggerMode::OnInputChange), 40, "12:00:00");
        const uint v2 = out->version.load();
        rt.setInput(R"({"v": 7})");
        QCOMPARE(out->version.load(), v2 + 1);
        QCOMPARE(outputOf(out).value("x").toInt(), 7);

        auto timed = std::make_shared<ScriptOutput>();
        ScriptRuntime tod(timed);
        tod.applySettings("", int(ScriptTriggerMode::TimeOfDay), 40, "00:00:00");
        QTRY_COMPARE(timed->version.load(), 1u);
        QTest::qWait(1300);
        QCOMPARE(timed->version.load(), 1u);

        auto later = std::make_shared<ScriptOutput>();
        ScriptRuntime notYet(later);
        notYet.applySettings("", int(ScriptTriggerMode::TimeOfDay), 40, "23:59:59");
        if (QTime::currentTime() < QTime(23, 59, 59)) {
            QTest::qWait(1200);
            QCOMPARE(later->version.load(), 0u);
        }

        auto inactive = std::make_shared<ScriptOutput>();
        ScriptRuntime paused(inactive);
        paused.setActive(false);
        paused.applySettings("", int(ScriptTriggerMode::Interval), 20, "12:00:00");
        QTest::qWait(200);
        QCOMPARE(inactive->version.load(), 0u);
        paused.setActive(true);
        QTRY_VERIFY(inactive->version.load() >= 1);
    }

    void errorStateSetsAndClears()
    {
        ClipGraph g;
        GraphRuntime rt(&g);
        const NodeId id = addScript(g, "error('boom')");
        QSignalSpy status(&rt, &GraphRuntime::scriptStatusChanged);
        QVERIFY(rt.runScript(id));
        QTRY_VERIFY(rt.scriptError(id).contains("boom"));
        QVERIFY(status.count() >= 1);

        g.setParams(id, {{"luaCode", "return { ok = 1 }"}});
        QVERIFY(rt.runScript(id));
        QTRY_VERIFY(rt.scriptError(id).isEmpty());

        g.setParams(id, {{"luaCode", "print('hello'); return {}"}});
        QVERIFY(rt.runScript(id));
        QTRY_COMPARE(rt.scriptLog(id), QStringList{"hello"});
    }

    void paramGetAndSetStayInOwnScene()
    {
        ClipGraph g1;
        ClipGraph g2;
        g2.setIdBase(quint64(1) << 40);
        ClipGraphController ctl(&g1);
        GraphRuntime rt1(&g1);
        GraphRuntime rt2(&g2);

        const NodeId fx = addOpacity(g1, 0.5);
        const NodeId other = addOpacity(g2, 0.5);
        QVERIFY(fx && other && fx != other);
        const NodeId script = addScript(g1, QString("local v = prism.param(%1, 'params.opacity'); "
                                                    "prism.param(%1, 'params.opacity', v * 2); "
                                                    "return { v = v }").arg(fx));
        const int undoCount = ctl.undoStack()->count();
        QVERIFY(rt1.runScript(script));
        QTRY_COMPARE(opacityOf(g1, fx), 1.0);
        QVERIFY(rt1.scriptError(script).isEmpty());
        QCOMPARE(ctl.undoStack()->count(), undoCount);

        g1.setParams(script, {{"luaCode", QString("prism.param(%1, 'params.opacity', 0.9)").arg(other)}});
        QVERIFY(rt1.runScript(script));
        QTRY_VERIFY(rt1.scriptError(script).contains("not in this scene"));
        QTest::qWait(100);
        QCOMPARE(opacityOf(g2, other), 0.5);

        g1.setParams(script, {{"luaCode", QString("return { v = prism.param(%1, 'params.opacity') }").arg(other)}});
        QVERIFY(rt1.runScript(script));
        QTRY_VERIFY(rt1.scriptError(script).contains("not in this scene"));

        g1.setParams(script, {{"luaCode", "local b = prism.audio.bands(); "
                                          "assert(type(b) == 'table' and b.low == 0)"}});
        QVERIFY(rt1.runScript(script));
        QTRY_VERIFY(rt1.scriptError(script).isEmpty());
        QTest::qWait(100);
        QVERIFY(rt1.scriptError(script).isEmpty());
    }

    void expressionsDriveParamsAndKeepLastValueOnError()
    {
        ClipGraph g;
        GraphRuntime rt(&g);
        const NodeId fx = addOpacity(g, 0.5);

        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity",
                                                      "0.75 / 3"}}}});
        QTRY_VERIFY(std::abs(opacityOf(g, fx) - 0.25) < 1e-9);
        QVERIFY(rt.expressionError(fx, "params.opacity").isEmpty());

        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "1 +"}}}});
        QTRY_VERIFY(!rt.expressionError(fx, "params.opacity").isEmpty());
        QVERIFY(std::abs(opacityOf(g, fx) - 0.25) < 1e-9);

        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "nosuchfn()"}}}});
        QTRY_VERIFY(rt.expressionError(fx, "params.opacity").contains("nosuchfn"));
        QVERIFY(std::abs(opacityOf(g, fx) - 0.25) < 1e-9);

        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "math.min(1, t) * 0 + 0.6"}}}});
        QTRY_VERIFY(std::abs(opacityOf(g, fx) - 0.6) < 1e-9);
        QVERIFY(rt.expressionError(fx, "params.opacity").isEmpty());

        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "while true do end"}}}});
        QTRY_VERIFY(!rt.expressionError(fx, "params.opacity").isEmpty());

        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "0.1"}}}});
        QTRY_VERIFY(std::abs(opacityOf(g, fx) - 0.1) < 1e-9);
        rt.setActive(false);
        QTest::qWait(150);
        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "0.9"}}}});
        QTest::qWait(250);
        QVERIFY(std::abs(opacityOf(g, fx) - 0.1) < 1e-9);
        rt.setActive(true);
        QTRY_VERIFY(std::abs(opacityOf(g, fx) - 0.9) < 1e-9);
    }

    void expressionWritesDoNotEmitStructuralChange()
    {
        ClipGraph g;
        GraphRuntime rt(&g);
        const NodeId fx = addOpacity(g, 0.5);
        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "0.3"}}}});
        QSignalSpy changed(&g, &ClipGraph::changed);
        QSignalSpy driven(&g, &ClipGraph::paramsDriven);
        QTRY_VERIFY(std::abs(opacityOf(g, fx) - 0.3) < 1e-9);
        QVERIFY(driven.count() > 0);
        QCOMPARE(changed.count(), 0);
    }

    void expressionResultTypeFollowsSlider()
    {
        ClipGraph g;
        GraphRuntime rt(&g);
        const NodeId fx = addOpacity(g, 0.5);
        const auto stored = [&] { return g.node(fx)->params.value("params").toMap().value("opacity"); };

        // A slider at 1 stores an int; a float slider must still get fractions.
        g.setParams(fx, {{"params", QVariantMap{{"opacity", 1}}}});
        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "0.25"}}},
                         {"expressionInts", QVariantMap{{"params.opacity", false}}}});
        QTRY_VERIFY(std::abs(stored().toDouble() - 0.25) < 1e-9);
        QCOMPARE(stored().typeId(), int(QMetaType::Double));

        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "2.6"}}},
                         {"expressionInts", QVariantMap{{"params.opacity", true}}}});
        QTRY_COMPARE(stored().toInt(), 3);
        QCOMPARE(stored().typeId(), int(QMetaType::Int));

        // Expressions saved without the flag keep following the stored type.
        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "4.4"}}}, {"expressionInts", QVariantMap{}}});
        QTRY_COMPARE(stored().toInt(), 4);
        QCOMPARE(stored().typeId(), int(QMetaType::Int));
    }

    void expressionHelpersMapAndClamp()
    {
        ClipGraph g;
        GraphRuntime rt(&g);
        const NodeId fx = addOpacity(g, 0.5);

        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "map(5, 0, 10, 0, 0.8)"}}}});
        QTRY_VERIFY(std::abs(opacityOf(g, fx) - 0.4) < 1e-9);
        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "map(20, 0, 10, 0, 0.8)"}}}});
        QTRY_VERIFY(std::abs(opacityOf(g, fx) - 0.8) < 1e-9);
        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "clamp(2, 0, 1) * lerp(0, 0.5, 0.5)"}}}});
        QTRY_VERIFY(std::abs(opacityOf(g, fx) - 0.25) < 1e-9);
        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "invlerp(2, 6, 3) + smoothstep(0, 1, 0.5)"}}}});
        QTRY_VERIFY(std::abs(opacityOf(g, fx) - 0.75) < 1e-9);
    }

    void expressionReadsWiredSocketAsInput()
    {
        ClipGraph g;
        GraphRuntime rt(&g);
        const NodeId fx = addOpacity(g, 0.5);
        const NodeId script = addScript(g, "return { v = 4 }");
        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "input.v / 8"}}}});

        QTRY_VERIFY(rt.expressionError(fx, "params.opacity").contains("opacity"));
        QVERIFY(!rt.expressionInput(fx, "params.opacity").isValid());

        const QVector<ClipGraph::Port> ins = g.inputPorts(fx);
        QCOMPARE(ins.last().kind, ClipGraph::PortKind::ParamIn);
        QVERIFY(g.connectPorts(script, g.outputPorts(script).first().index, fx, ins.last().index));
        QVERIFY(rt.runScript(script));
        QTRY_VERIFY(std::abs(opacityOf(g, fx) - 0.5) < 1e-9 && rt.expressionError(fx, "params.opacity").isEmpty()
                    && rt.expressionInput(fx, "params.opacity").toMap().value("v").toInt() == 4);
    }

    void audioAnalyzerAndScriptSettingsPersist()
    {
        ClipGraph g;
        const NodeId an = g.addNode(NT::AudioAnalyzer, {}, {{"fftSize", 4096}, {"smoothing", 0.5}});
        const NodeId sc = g.addNode(NT::Script, {}, {{"luaCode", "return {}"}, {"runOn", 2},
                                                     {"runTime", "08:30:00"}, {"intervalMs", 250}});
        const NodeId fx = addOpacity(g);
        g.setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "0.5"}}}});

        ClipGraph loaded;
        QVERIFY(loaded.fromJson(g.toJson()));
        QCOMPARE(loaded.node(an)->type, NT::AudioAnalyzer);
        QCOMPARE(loaded.node(an)->params.value("fftSize").toInt(), 4096);
        QCOMPARE(loaded.node(sc)->type, NT::Script);
        QCOMPARE(loaded.node(sc)->params.value("runOn").toInt(), 2);
        QCOMPARE(loaded.node(sc)->params.value("runTime").toString(), QStringLiteral("08:30:00"));
        QCOMPARE(loaded.node(sc)->params.value("intervalMs").toInt(), 250);
        QCOMPARE(loaded.node(fx)->params.value("expressions").toMap().value("params.opacity").toString(),
                 QStringLiteral("0.5"));
    }

    void deletingRuntimeWhileScriptRunsDoesNotBlock()
    {
        {
            auto *g = new ClipGraph;
            auto *rt = new GraphRuntime(g);
            addScript(*g, "while true do end", int(ScriptTriggerMode::Interval));
            const NodeId fx = addOpacity(*g);
            g->setParams(fx, {{"expressions", QVariantMap{{"params.opacity", "0.5"}}}});
            QTest::qWait(150);

            QElapsedTimer t;
            t.start();
            rt->setActive(false);
            QVERIFY(t.elapsed() < 200);
            rt->setActive(true);
            QTest::qWait(50);
            t.restart();
            delete rt;
            delete g;
            QVERIFY2(t.elapsed() < 500, "deleting a runtime must not wait for its scripts");
        }
        QVERIFY(GraphRuntime::waitForRetiredScripts(10000));
    }

    void removingScriptNodeWhileRunningDoesNotBlock()
    {
        ClipGraph g;
        GraphRuntime rt(&g);
        const NodeId id = addScript(g, "while true do end", int(ScriptTriggerMode::Interval));
        QTest::qWait(100);
        QElapsedTimer t;
        t.start();
        g.removeNode(id);
        QVERIFY(t.elapsed() < 500);
        QVERIFY(GraphRuntime::waitForRetiredScripts(10000));
    }
};

QTEST_MAIN(TestScripting)
#include "test_scripting.moc"
