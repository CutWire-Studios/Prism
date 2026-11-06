#include "core/graph/ClipGraph.h"
#include "core/graph/ProcessEffects.h"
#include "core/graph/AudioEffects.h"
#include "core/audio/AudioRack.h"
#include "core/sources/SourceDescriptor.h"

#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>

class TestProcessNodes : public QObject {
    Q_OBJECT

    ClipGraph *m_graph = nullptr;

private slots:
    void init() {
        m_graph = new ClipGraph;
    }

    void cleanup() {
        delete m_graph;
        m_graph = nullptr;
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
        state["graphVersion"] = ClipGraph::kGraphVersion;
        state["nextId"] = 6;
        state["inputNodes"] = QJsonArray{input};
        state["processNodes"] = QJsonArray{crop, flip, seg};
        state["outputNode"] = output;
        state["connections"] = connections;
        return state;
    }

    void verifyStream(const ClipGraph::ResolvedStream &stream) {
        QCOMPARE(stream.layers.size(), 1);
        const ClipGraph::ResolvedLayer &l = stream.layers.first();
        QCOMPARE(l.inputNodeId, NodeId(1));
        // Crop and Flip are now ordered decorator effects (not draw-time folds), so
        // the resolved layer keeps identity placement and records every effect, in
        // upstream→downstream order: Crop(0), FlipH(1), Segment(3).
        QCOMPARE(l.cropX, 0.f);
        QCOMPARE(l.cropW, 1.f);
        QVERIFY(!l.flipH);
        QVERIFY(!l.flipV);
        QCOMPARE(l.sourceEffects.size(), 3);
        QCOMPARE(l.sourceEffects.at(0).effectId, 0);
        QCOMPARE(l.sourceEffects.at(1).effectId, 1);
        QCOMPARE(l.sourceEffects.at(2).effectId, 3);
        const QJsonObject cropParams = l.sourceEffects.at(0).params;
        QCOMPARE(cropParams["x"].toDouble(), 0.25);
        QCOMPARE(cropParams["y"].toDouble(), 0.1);
        QCOMPARE(cropParams["w"].toDouble(), 0.5);
        QCOMPARE(cropParams["h"].toDouble(), 0.8);
    }

    void legacyLoadAndFold() {
        QVERIFY(m_graph->fromJson(makeLegacyGraph()));
        verifyStream(m_graph->evaluateVideoInput(4));
    }

    void saveWritesParamsAndLegacyMirror() {
        QVERIFY(m_graph->fromJson(makeLegacyGraph()));
        const QJsonObject saved = m_graph->toJson();
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
        QVERIFY(m_graph->fromJson(makeLegacyGraph()));
        const QJsonObject saved = m_graph->toJson();

        ClipGraph graph2;
        QVERIFY(graph2.fromJson(saved));
        verifyStream(graph2.evaluateVideoInput(4));
    }

    void unknownEffectIsSkipped() {
        QJsonObject state = makeLegacyGraph();
        QJsonArray procs = state["processNodes"].toArray();
        QJsonObject bogus = procs.at(1).toObject();
        bogus["effect"] = 99;
        procs.replace(1, bogus);
        state["processNodes"] = procs;

        QVERIFY(m_graph->fromJson(state));
        // FlipH(3) is gone, so the chain is broken at node 3; evaluating the
        // remaining downstream Segment(4) yields no layers but must not crash.
        const ClipGraph::ResolvedStream broken = m_graph->evaluateVideoInput(4);
        QVERIFY(broken.layers.isEmpty());
        // The upstream part still evaluates.
        const ClipGraph::ResolvedStream upstream = m_graph->evaluateVideoInput(2);
        QCOMPARE(upstream.layers.size(), 1);
        QCOMPARE(upstream.layers.first().sourceEffects.size(), 1);
        QCOMPARE(upstream.layers.first().sourceEffects.first().effectId, 0);
        QCOMPARE(upstream.layers.first().sourceEffects.first().params["x"].toDouble(), 0.25);
    }

    // Input(1) ┐
    // Input(2) ┼─► Layer(10, 2 slots) ─► Output(5)
    // The Layer node must flatten to ONE composite ResolvedLayer carrying both
    // sub-layers, so a downstream process would act on the composite.
    void layerNodeFlattensToComposite() {
        auto input = [](qint64 id) {
            QJsonObject src;
            src["kind"] = (int)SourceDescriptor::Kind::Canvas;
            src["displayName"] = QStringLiteral("Clip %1").arg(id);
            QJsonObject o;
            o["id"] = id;
            o["source"] = src;
            o["hasAudio"] = false;
            return o;
        };
        auto slot = []() {
            QJsonObject s;
            s["baseX"] = 0.0; s["baseY"] = 0.0; s["baseW"] = 1.0; s["baseH"] = 1.0;
            s["visible"] = true; s["name"] = QString();
            return s;
        };
        QJsonObject layer;
        layer["id"] = 10;
        layer["posX"] = 0.0; layer["posY"] = 0.0;
        layer["canvasW"] = 1920; layer["canvasH"] = 1080;
        layer["slots"] = QJsonArray{slot(), slot()};

        auto chain = [](qint64 from, qint64 to, int toPort) {
            QJsonObject c;
            c["from"] = from; c["to"] = to;
            c["kind"] = 0; c["toPortIndex"] = toPort;
            return c;
        };

        QJsonObject state;
        state["graphVersion"] = ClipGraph::kGraphVersion;
        state["nextId"] = 11;
        state["inputNodes"] = QJsonArray{input(1), input(2)};
        state["layerNodes"] = QJsonArray{layer};
        state["outputNode"] = QJsonObject{{"id", 5}};
        state["connections"] = QJsonArray{chain(1, 10, 0), chain(2, 10, 1), chain(10, 5, -1)};

        QVERIFY(m_graph->fromJson(state));

        const ClipGraph::ResolvedStream stream = m_graph->evaluateVideoInput(10);
        QCOMPARE(stream.layers.size(), 1);
        const ClipGraph::ResolvedLayer &l = stream.layers.first();
        QVERIFY(l.composite != nullptr);
        QCOMPARE(l.layerNodeId, NodeId(10));
        QCOMPARE(stream.canvasWidth, 1920);
        QCOMPARE(stream.canvasHeight, 1080);
        QCOMPARE(l.composite->canvasWidth, 1920);
        QCOMPARE(l.composite->canvasHeight, 1080);
        QCOMPARE(l.composite->layers.size(), 2);
        QSet<NodeId> ids;
        for (const ClipGraph::ResolvedLayer &sl : l.composite->layers) ids.insert(sl.inputNodeId);
        QVERIFY(ids.contains(NodeId(1)));
        QVERIFY(ids.contains(NodeId(2)));
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
        QVERIFY(ProcessEffects::byName(QStringLiteral("crop")));
        QVERIFY(ProcessEffects::byName(QStringLiteral("chroma_key")));
        QCOMPARE(ProcessEffects::byName(QStringLiteral("nope")), nullptr);
    }

    void addProcessNodeAndConnect() {
        SourceDescriptor src;
        src.kind = SourceDescriptor::Kind::Canvas;
        src.displayName = QStringLiteral("Clip");
        const NodeId inId = m_graph->addInputNode(src);
        QVERIFY(inId != 0);
        const NodeId cropId = m_graph->addNode(ClipGraph::NodeType::Process, {},
                                               {{QStringLiteral("effect"), 0}});
        QVERIFY(cropId != 0);
        QVERIFY(m_graph->connectNodes(inId, cropId, 0));
        QVERIFY(m_graph->connectNodes(cropId, m_graph->outputNodeId(), 0));
        QVERIFY(m_graph->setEffectParams(cropId, {{QStringLiteral("x"), 0.25}}));
        const ClipGraph::ResolvedStream stream = m_graph->evaluateVideoInput(cropId);
        QCOMPARE(stream.layers.size(), 1);
        QCOMPARE(stream.layers.first().sourceEffects.size(), 1);
        QCOMPARE(stream.layers.first().sourceEffects.first().effectId, 0);
        QCOMPARE(stream.layers.first().sourceEffects.first().params.value(QStringLiteral("x")).toDouble(), 0.25);
    }

    void refuseOutputWhenAbWired() {
        SourceDescriptor src;
        src.kind = SourceDescriptor::Kind::Canvas;
        src.displayName = QStringLiteral("Clip");
        const NodeId inId = m_graph->addInputNode(src);
        QVERIFY(inId != 0);
        const NodeId abId = m_graph->addNode(ClipGraph::NodeType::AbSelect);
        QVERIFY(abId != 0);
        QVERIFY(m_graph->connectNodes(inId, abId, 0, 0));
        QVERIFY(m_graph->connectNodes(abId, m_graph->outputNodeId(), 8));

        const NodeId cropId = m_graph->addNode(ClipGraph::NodeType::Process, {},
                                               {{QStringLiteral("effect"), 0}});
        QVERIFY(m_graph->connectNodes(inId, cropId, 0));
        QVERIFY(!m_graph->connectNodes(cropId, m_graph->outputNodeId(), 0));
    }

    void audioEffectRegistryIsConsistent() {
        for (const AudioEffectDescriptor &d : AudioEffects::all()) {
            QVERIFY(d.id >= 0);
            QVERIFY(!d.name.isEmpty());
            QVERIFY(!d.menuLabel.isEmpty());
            QCOMPARE(AudioEffects::byId(d.id), &d);
        }
        QCOMPARE(AudioEffects::byId(99), nullptr);
        QCOMPARE(AudioEffects::all().size(), 1);

        int audioFxCount = 0;
        for (const AudioEffectDescriptor &d : AudioEffects::all()) {
            if (d.id == AudioEffects::kAudioFxEffectId) {
                ++audioFxCount;
                QCOMPARE(d.name, QStringLiteral("Audio FX"));
                QVERIFY(prism::audiofx::fromJson(d.defaultParams).empty());
            }
        }
        QCOMPARE(audioFxCount, 1);
    }

    void audioOutputSingletonOnAdd() {
        const NodeId first = m_graph->addNode(ClipGraph::NodeType::MasterAudioOutput);
        QVERIFY(first != 0);
        QCOMPARE(m_graph->audioOutputNodeId(), first);
        QCOMPARE(m_graph->addNode(ClipGraph::NodeType::MasterAudioOutput), NodeId(0));
        QCOMPARE(m_graph->audioOutputNodeId(), first);
    }

    void audioFileRoundTripInGraph() {
        QJsonObject src;
        src["kind"] = (int)SourceDescriptor::Kind::AudioFile;
        src["path"] = "/tmp/test.wav";
        src["displayName"] = "Test Audio";
        QJsonObject input;
        input["id"] = 1;
        input["source"] = src;
        input["hasAudio"] = true;
        input["audioOnly"] = true;

        QJsonObject state;
        state["graphVersion"] = ClipGraph::kGraphVersion;
        state["nextId"] = 2;
        state["inputNodes"] = QJsonArray{input};
        state["outputNode"] = QJsonObject{{"id", 2}};
        state["connections"] = QJsonArray{};

        QVERIFY(m_graph->fromJson(state));
        const QJsonObject saved = m_graph->toJson();
        const QJsonObject savedInput = saved["inputNodes"].toArray().first().toObject();
        QCOMPARE(savedInput["source"].toObject()["kind"].toInt(),
                 (int)SourceDescriptor::Kind::AudioFile);
        QVERIFY(savedInput["audioOnly"].toBool());
    }
};

QTEST_MAIN(TestProcessNodes)
#include "test_processnodes.moc"
