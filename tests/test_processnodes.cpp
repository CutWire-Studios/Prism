#include "ui/nodes/ClipNodeEditor.h"
#include "ui/nodes/ProcessEffects.h"
#include "core/sources/SourceDescriptor.h"

#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>

class TestProcessNodes : public QObject {
    Q_OBJECT

    QWidget *m_parent = nullptr;
    ClipNodeEditor *m_editor = nullptr;

private slots:
    void init() {
        m_parent = new QWidget;
        m_editor = new ClipNodeEditor(m_parent);
    }

    void cleanup() {
        delete m_parent;
        m_parent = nullptr;
        m_editor = nullptr;
    }

    // Input(1) -> Crop(2) -> FlipH(3) -> Segment(4) -> Output(5), with the
    // process nodes in the pre-refactor save format (effect int + cropX/Y/W/H,
    // no "params").
    QJsonObject makeLegacyGraph() const {
        QJsonObject src;
        src["kind"] = (int)SourceDescriptor::Kind::Canvas;
        src["displayName"] = "Clip";
        QJsonObject input;
        input["id"] = 1;
        input["source"] = src;
        input["hasAudio"] = false;

        auto proc = [](qint64 id, int effect) {
            QJsonObject p;
            p["id"] = id;
            p["effect"] = effect;
            p["posX"] = 0.0; p["posY"] = 0.0;
            return p;
        };
        QJsonObject crop = proc(2, 0);
        crop["cropX"] = 0.25; crop["cropY"] = 0.1;
        crop["cropW"] = 0.5;  crop["cropH"] = 0.8;
        QJsonObject flip = proc(3, 1);
        flip["cropX"] = 0.0; flip["cropY"] = 0.0;
        flip["cropW"] = 1.0; flip["cropH"] = 1.0;
        QJsonObject seg = proc(4, 3);
        seg["cropX"] = 0.0; seg["cropY"] = 0.0;
        seg["cropW"] = 1.0; seg["cropH"] = 1.0;

        QJsonObject output;
        output["id"] = 5;

        auto conn = [](qint64 from, qint64 to) {
            QJsonObject c;
            c["from"] = from; c["to"] = to;
            c["kind"] = 0; c["toPortIndex"] = -1;
            return c;
        };
        QJsonArray connections{conn(1, 2), conn(2, 3), conn(3, 4), conn(4, 5)};

        QJsonObject state;
        state["graphVersion"] = 3;
        state["nextId"] = 6;
        state["inputNodes"] = QJsonArray{input};
        state["processNodes"] = QJsonArray{crop, flip, seg};
        state["outputNode"] = output;
        state["connections"] = connections;
        return state;
    }

    void verifyStream(const ResolvedStream &stream) {
        QCOMPARE(stream.layers.size(), 1);
        const ResolvedLayer &l = stream.layers.first();
        QCOMPARE(l.inputNodeId, (NodeId)1);
        QCOMPARE(l.cropX, 0.25f);
        QCOMPARE(l.cropY, 0.1f);
        QCOMPARE(l.cropW, 0.5f);
        QCOMPARE(l.cropH, 0.8f);
        QVERIFY(l.flipH);
        QVERIFY(!l.flipV);
        QCOMPARE(l.sourceEffects.size(), 1);
        QCOMPARE(l.sourceEffects.first().effectId, 3);
    }

    void legacyLoadAndFold() {
        m_editor->restoreState(makeLegacyGraph());
        verifyStream(m_editor->evaluateVideoInput(4));
    }

    void saveWritesParamsAndLegacyMirror() {
        m_editor->restoreState(makeLegacyGraph());
        const QJsonObject saved = m_editor->saveState();
        const QJsonArray procs = saved["processNodes"].toArray();
        QCOMPARE(procs.size(), 3);

        QJsonObject cropObj;
        for (const auto &v : procs)
            if (v.toObject()["effect"].toInt() == 0) cropObj = v.toObject();
        QVERIFY(cropObj.contains("params"));
        const QJsonObject p = cropObj["params"].toObject();
        QCOMPARE(p["x"].toDouble(), 0.25);
        QCOMPARE(p["y"].toDouble(), 0.1);
        QCOMPARE(p["w"].toDouble(), 0.5);
        QCOMPARE(p["h"].toDouble(), 0.8);
        QCOMPARE(cropObj["cropX"].toDouble(), 0.25);
        QCOMPARE(cropObj["cropW"].toDouble(), 0.5);
    }

    void newFormatRoundTrip() {
        m_editor->restoreState(makeLegacyGraph());
        const QJsonObject saved = m_editor->saveState();

        auto *parent2 = new QWidget;
        auto *editor2 = new ClipNodeEditor(parent2);
        editor2->restoreState(saved);
        verifyStream(editor2->evaluateVideoInput(4));
        delete parent2;
    }

    void unknownEffectIsSkipped() {
        QJsonObject state = makeLegacyGraph();
        QJsonArray procs = state["processNodes"].toArray();
        QJsonObject bogus = procs.at(1).toObject();
        bogus["effect"] = 99;
        procs.replace(1, bogus);
        state["processNodes"] = procs;

        m_editor->restoreState(state);
        // FlipH(3) is gone, so the chain is broken at node 3; evaluating the
        // remaining downstream Segment(4) yields no layers but must not crash.
        const ResolvedStream broken = m_editor->evaluateVideoInput(4);
        QVERIFY(broken.layers.isEmpty());
        // The upstream part still evaluates.
        const ResolvedStream upstream = m_editor->evaluateVideoInput(2);
        QCOMPARE(upstream.layers.size(), 1);
        QCOMPARE(upstream.layers.first().cropX, 0.25f);
    }

    void registryIsConsistent() {
        for (const ProcessEffectDescriptor &d : ProcessEffects::all()) {
            QVERIFY(d.id >= 0);
            QVERIFY(!d.name.isEmpty());
            QVERIFY(!d.menuLabel.isEmpty());
            QVERIFY(d.fold || d.isDecorator);
            QCOMPARE(ProcessEffects::byId(d.id), &d);
        }
        QCOMPARE(ProcessEffects::byId(99), nullptr);
    }
};

QTEST_MAIN(TestProcessNodes)
#include "test_processnodes.moc"
