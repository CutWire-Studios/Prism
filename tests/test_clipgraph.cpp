#include "core/graph/ClipGraph.h"
#include "core/graph/ClipGraphModels.h"
#include "core/graph/AudioEffects.h"
#include "core/graph/ProcessEffects.h"

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using NT = ClipGraph::NodeType;
using EK = ClipGraph::EdgeKind;

namespace {

QString dump(const QJsonObject &o) {
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

QString firstDiff(const QJsonValue &a, const QJsonValue &b, const QString &path = QStringLiteral("$")) {
    if (a.type() != b.type())
        return path + QStringLiteral(": type differs");
    if (a.isObject()) {
        const QJsonObject oa = a.toObject(), ob = b.toObject();
        for (auto it = oa.begin(); it != oa.end(); ++it) {
            if (!ob.contains(it.key())) return path + "." + it.key() + ": missing on right";
            const QString d = firstDiff(it.value(), ob.value(it.key()), path + "." + it.key());
            if (!d.isEmpty()) return d;
        }
        for (auto it = ob.begin(); it != ob.end(); ++it)
            if (!oa.contains(it.key())) return path + "." + it.key() + ": missing on left";
        return {};
    }
    if (a.isArray()) {
        const QJsonArray aa = a.toArray(), ab = b.toArray();
        if (aa.size() != ab.size())
            return path + QStringLiteral(": size %1 vs %2").arg(aa.size()).arg(ab.size());
        for (int i = 0; i < aa.size(); ++i) {
            const QString d = firstDiff(aa[i], ab[i], path + QStringLiteral("[%1]").arg(i));
            if (!d.isEmpty()) return d;
        }
        return {};
    }
    return a == b ? QString() : path + QStringLiteral(": ") + QString::fromUtf8(QJsonDocument(QJsonArray{a}).toJson(QJsonDocument::Compact))
        + " vs " + QString::fromUtf8(QJsonDocument(QJsonArray{b}).toJson(QJsonDocument::Compact));
}

// nextId is dropped because the first load keeps whatever the file said while a reload derives it.
QJsonObject withoutVolatile(QJsonObject state, bool devices) {
    state.remove("nextId");
    if (devices) {
        QJsonArray masters = state["masterAudioNodes"].toArray();
        for (int i = 0; i < masters.size(); ++i) {
            QJsonObject m = masters[i].toObject();
            m.remove("devicePorts");
            masters[i] = m;
        }
        state["masterAudioNodes"] = masters;
    }
    return state;
}

QJsonObject clipJson(qint64 id, SourceDescriptor::Kind kind, const QString &name, bool hasAudio,
                     double x, double y) {
    QJsonObject src;
    src["kind"] = int(kind);
    src["displayName"] = name;
    QJsonObject o;
    o["id"] = id;
    o["posX"] = x;
    o["posY"] = y;
    o["source"] = src;
    o["hasAudio"] = hasAudio;
    return o;
}

QJsonObject connJson(qint64 from, qint64 to, EK kind, int toPortIndex) {
    return QJsonObject{{"from", from}, {"to", to}, {"kind", int(kind)}, {"toPortIndex", toPortIndex}};
}

// Clips 1-2 feed a Layer through a Flip; clips 3-4 are unwired. Clip 1's audio and a
// mic go through a mixer to the audio output. Layer -> Output.
QJsonObject realisticState() {
    QJsonArray inputs{
        clipJson(1, SourceDescriptor::Kind::Canvas, "Cam 1", true, 20, 20),
        clipJson(2, SourceDescriptor::Kind::Canvas, "Cam 2", false, 20, 200),
        clipJson(3, SourceDescriptor::Kind::Canvas, "Cam 3", false, 20, 400),
        clipJson(4, SourceDescriptor::Kind::Canvas, "Cam 4", false, 20, 600),
    };

    QJsonObject flip{{"id", 10}, {"posX", 200.5}, {"posY", 20.0}, {"effect", 4},
                     {"params", QJsonObject{{"direction", 0}}}};

    QJsonArray layerSlots{
        QJsonObject{{"baseX", 0.0}, {"baseY", 0.0}, {"baseW", 0.5}, {"baseH", 1.0},
                    {"visible", true}, {"name", "left"}},
        QJsonObject{{"baseX", 0.5}, {"baseY", 0.0}, {"baseW", 0.5}, {"baseH", 1.0},
                    {"visible", false}, {"name", "right"}},
    };
    QJsonObject layer{{"id", 11}, {"posX", 400.0}, {"posY", 50.0}, {"canvasW", 1920},
                      {"canvasH", 1080}, {"slots", layerSlots}};

    QJsonObject mixer{{"id", 13}, {"posX", 600.0}, {"posY", 300.0},
                      {"slots", QJsonArray{QJsonObject{{"volume", 70}, {"muted", false}, {"name", "music"}},
                                           QJsonObject{{"volume", 100}, {"muted", true}, {"name", ""}}}}};

    QJsonObject mic{{"id", 14}, {"posX", 20.0}, {"posY", 800.0}, {"deviceId", "mic-1"},
                    {"deviceLabel", "USB Mic"}, {"volume", 80}, {"muted", false}};
    QJsonObject master{{"id", 15}, {"posX", 800.0}, {"posY", 300.0},
                       {"devicePorts", QJsonArray{QJsonObject{{"deviceId", ""}, {"deviceLabel", "Default"}},
                                                  QJsonObject{{"deviceId", "hdmi"}, {"deviceLabel", "HDMI"}}}}};
    QJsonObject output{{"id", 20}, {"posX", 700.0}, {"posY", 50.0}};

    QJsonArray conns{
        connJson(1, 10, EK::Chain, 0),
        connJson(10, 11, EK::Chain, 0),
        connJson(2, 11, EK::Chain, 1),
        connJson(11, 20, EK::Chain, 0),
        connJson(1, 13, EK::StreamToMixer, 0),
        connJson(14, 13, EK::StreamToMixer, 1),
        connJson(13, 15, EK::MixerToOutput, 1),
    };

    QJsonObject state;
    state["graphVersion"] = ClipGraph::kGraphVersion;
    state["inputNodes"] = inputs;
    state["processNodes"] = QJsonArray{flip};
    state["layerNodes"] = QJsonArray{layer};
    state["audioMixerNodes"] = QJsonArray{mixer};
    state["masterAudioInputNodes"] = QJsonArray{mic};
    state["masterAudioNodes"] = QJsonArray{master};
    state["outputNode"] = output;
    state["connections"] = conns;
    return state;
}

}  // namespace

class TestClipGraph : public QObject {
    Q_OBJECT

    QVariantMap canvas(const QString &name, bool audio = false) const {
        return {{"source", QVariantMap{{"kind", int(SourceDescriptor::Kind::Canvas)},
                                       {"displayName", name}}},
                {"hasAudio", audio}};
    }

    struct Chain {
        quint64 a = 0, p1 = 0, p2 = 0, p3 = 0, out = 0;
    };

    // a -> p1 -> p2 -> p3 -> Output, all video; p1..p3 are Flip processes.
    Chain buildChain(ClipGraphController &c) const {
        Chain k;
        k.a = c.addNode(int(NT::Input), 0, 0, canvas("a"));
        k.p1 = c.addNode(int(NT::Process), 200, 0, {{"effect", 4}});
        k.p2 = c.addNode(int(NT::Process), 400, 0, {{"effect", 4}});
        k.p3 = c.addNode(int(NT::Process), 600, 0, {{"effect", 4}});
        k.out = c.graph()->outputNodeId();
        ClipGraph *g = c.graph();
        g->connectNodes(k.a, k.p1);
        g->connectNodes(k.p1, k.p2);
        g->connectNodes(k.p2, k.p3);
        g->connectNodes(k.p3, k.out);
        return k;
    }

    static QVariantList ids(std::initializer_list<quint64> list) {
        QVariantList out;
        for (quint64 id : list) out.append(QVariant::fromValue<quint64>(id));
        return out;
    }

private slots:
    void jsonMatchesStoredFixture() {
        const QJsonObject input = QJsonDocument::fromJson(QByteArray(R"json({
            "graphVersion": 6,
            "inputNodes": [
                {"id": 1, "posX": 20, "posY": 20, "hasAudio": false,
                 "source": {"kind": 5, "displayName": "Cam 1"}}
            ],
            "processNodes": [
                {"id": 2, "posX": 200.5, "posY": 20, "effect": 4, "params": {"direction": 0}}
            ],
            "outputNode": {"id": 3, "posX": 700, "posY": 50},
            "connections": [
                {"from": 1, "to": 2, "kind": 0, "toPortIndex": 0},
                {"from": 2, "to": 3, "kind": 0, "toPortIndex": -1}
            ]
        })json")).object();
        const QJsonObject expectedGraph = QJsonDocument::fromJson(QByteArray(R"json({
            "graphVersion": 6,
            "nextId": 4,
            "inputNodes": [
                {"id": 1, "posX": 20, "posY": 20, "hasAudio": false, "audioOnly": false,
                 "repeat": false,
                 "settings": {"startTime": 0, "endTime": -1, "cropX": 0, "cropY": 0,
                              "cropW": 1, "cropH": 1, "speed": 1},
                 "source": {"kind": 5, "path": "", "displayName": "Cam 1", "color": "\u0023ffffffff",
                            "cameraIndex": 0, "screenIndex": 0, "windowIndex": 0,
                            "captureId": "", "slideshowIntervalMs": 3000,
                            "slideshowEffect": 0, "slideshowTransitionMs": 800,
                            "canvasWidth": 1280, "canvasHeight": 720, "canvasFill": 0,
                            "shaderCode": "", "preset": "", "obsSceneName": "", "textTemplate": "",
                            "textStyle": "", "shapeStyle": "", "svgTemplateId": "",
                            "svgParams": "", "sceneId": "", "webrtcRelayUrl": ""}}
            ],
            "processNodes": [
                {"id": 2, "posX": 200.5, "posY": 20, "effect": 4, "params": {"direction": 0}}
            ],
            "audioEffectNodes": [],
            "layerNodes": [],
            "audioMixerNodes": [],
            "scriptNodes": [],
            "masterAudioNodes": [],
            "masterAudioInputNodes": [],
            "masterAudioCaptureNodes": [],
            "outputNode": {"id": 3, "posX": 700, "posY": 50},
            "connections": [
                {"from": 1, "to": 2, "kind": 0, "toPortIndex": 0},
                {"from": 2, "to": 3, "kind": 0, "toPortIndex": 0}
            ]
        })json")).object();
        const QJsonObject expectedSnapshot = QJsonDocument::fromJson(QByteArray(R"json({
            "output": "3",
            "nodes": [
                {"id": "1", "type": "input", "x": 20, "y": 20, "name": "Cam 1", "kind": "canvas"},
                {"id": "2", "type": "process", "x": 200.5, "y": 20, "effect": 4, "name": "Flip",
                 "params": {"direction": 0}},
                {"id": "3", "type": "output", "x": 700, "y": 50}
            ],
            "connections": [
                {"from": "1", "to": "2", "kind": 0, "kindName": "chain", "toPort": 0},
                {"from": "2", "to": "3", "kind": 0, "kindName": "chain", "toPort": 0}
            ]
        })json")).object();
        QVERIFY(!input.isEmpty() && !expectedGraph.isEmpty() && !expectedSnapshot.isEmpty());

        ClipGraph graph;
        QVERIFY(graph.fromJson(input));
        const QJsonObject out = graph.toJson();
        QVERIFY2(out == expectedGraph, qPrintable("graph vs fixture: " + firstDiff(out, expectedGraph)));
        const QJsonObject snap = graph.snapshot();
        QVERIFY2(snap == expectedSnapshot,
                 qPrintable("snapshot vs fixture: " + firstDiff(snap, expectedSnapshot)));

        ClipGraph again;
        QVERIFY(again.fromJson(expectedGraph));
        QVERIFY2(again.toJson() == expectedGraph,
                 qPrintable("reload vs fixture: " + firstDiff(again.toJson(), expectedGraph)));
    }

    void jsonRoundTripFromRawState() {
        ClipGraph graph;
        QVERIFY(graph.fromJson(realisticState()));
        QCOMPARE(graph.nodeIds().size(), 10);

        const QJsonObject out = graph.toJson();
        QVERIFY(out["nextId"].toInteger() > 20);
        QCOMPARE(out["inputNodes"].toArray().size(), 4);
        QCOMPARE(out["processNodes"].toArray().size(), 1);
        QCOMPARE(out["layerNodes"].toArray().size(), 1);
        QCOMPARE(out["audioMixerNodes"].toArray().size(), 1);
        QCOMPARE(out["masterAudioInputNodes"].toArray().size(), 1);
        QCOMPARE(out["masterAudioNodes"].toArray().size(), 1);
        QCOMPARE(out["outputNode"].toObject()["id"].toInt(), 20);

        ClipGraph second;
        QVERIFY(second.fromJson(out));
        const QJsonObject resaved = second.toJson();
        QVERIFY2(withoutVolatile(resaved, false) == withoutVolatile(out, false),
                 qPrintable("reload vs first save: " + firstDiff(resaved, out)));
        QVERIFY2(second.snapshot() == graph.snapshot(),
                 qPrintable("snapshot: " + firstDiff(second.snapshot(), graph.snapshot())));
    }

    void rejectsLegacyVersion() {
        ClipGraph graph;
        QVERIFY(!graph.fromJson(QJsonObject{{"graphVersion", 1}}));
        QVERIFY(graph.isEmptyGraph());
    }

    void rejectsPreviousVersion() {
        ClipGraph graph;
        QJsonObject old = realisticState();
        old["graphVersion"] = ClipGraph::kGraphVersion - 1;
        QVERIFY(!graph.fromJson(old));
        QVERIFY(graph.isEmptyGraph());
    }

    void rejectsVersion5Literal() {
        ClipGraph graph;
        QVERIFY(!graph.fromJson(QJsonObject{{"graphVersion", 5}}));
        QVERIFY(graph.isEmptyGraph());
    }

    void blendHasExtraInputPort() {
        ClipGraph graph;
        const NodeId blend = graph.addNode(NT::Process, {}, {{"effect", 55}});
        QVERIFY(blend != 0);
        const auto ins = graph.inputPorts(blend);
        QCOMPARE(ins.size(), 2);
        QCOMPARE(ins[0].label, QStringLiteral("in"));
        QCOMPARE(ins[1].label, QStringLiteral("b"));
        QVERIFY(ins[1].kind == ClipGraph::PortKind::ChainIn);
        QVERIFY(ins[1].slotted);

        const NodeId sw = graph.addNode(NT::Process, {}, {{"effect", 60}});
        QCOMPARE(graph.inputPorts(sw).size(), 4);
        QCOMPARE(graph.inputPorts(sw)[3].label, QStringLiteral("in3"));

        const NodeId flip = graph.addNode(NT::Process, {}, {{"effect", 4}});
        QCOMPARE(graph.inputPorts(flip).size(), 1);
    }

    void extraInputEdgesValidateAndRoundTrip() {
        ClipGraph graph;
        const NodeId a = graph.addNode(NT::Input, {}, canvas("a"));
        const NodeId b = graph.addNode(NT::Input, {}, canvas("b"));
        const NodeId blend = graph.addNode(NT::Process, {}, {{"effect", 55}});
        QVERIFY(graph.canConnectPorts(a, 0, blend, 0));
        QVERIFY(graph.canConnectPorts(b, 0, blend, 1));
        QVERIFY(!graph.canConnectPorts(b, 0, blend, 2));
        QVERIFY(graph.connectPorts(a, 0, blend, 0));
        QVERIFY(graph.connectPorts(b, 0, blend, 1));
        QCOMPARE(graph.producerForInput(blend, 0), a);
        QCOMPARE(graph.producerForInput(blend, 1), b);
        QVERIFY(graph.canConnectNodes(b, blend, int(EK::Chain), 1));

        const ClipGraph::ResolvedStream rs = graph.evaluateVideoInput(blend);
        QCOMPARE(rs.layers.size(), 1);
        QCOMPARE(rs.layers.first().sourceEffects.size(), 1);
        const SourceEffectRef &ref = rs.layers.first().sourceEffects.first();
        QCOMPARE(ref.aux.size(), 1);
        QVERIFY(ref.aux.first());
        QCOMPARE(ref.aux.first()->stream.layers.first().inputNodeId, b);

        const QJsonObject saved = graph.toJson();
        int slots1 = 0;
        for (const QJsonValue &v : saved["connections"].toArray())
            if (v.toObject()["to"].toInteger() == qint64(blend) && v.toObject()["toPortIndex"].toInt() == 1)
                ++slots1;
        QCOMPARE(slots1, 1);

        ClipGraph second;
        QVERIFY(second.fromJson(saved));
        QCOMPARE(second.producerForInput(blend, 0), a);
        QCOMPARE(second.producerForInput(blend, 1), b);

        QVERIFY(graph.disconnectNodes(b, blend, 1));
        QCOMPARE(graph.producerForInput(blend, 1), NodeId(0));
        QCOMPARE(graph.producerForInput(blend, 0), a);
    }

    void trailingSlotsAndDefaultNames() {
        ClipGraph graph;
        QVERIFY(graph.fromJson(realisticState()));
        const auto *layer = graph.node(11);
        QVERIFY(layer);
        QCOMPARE(layer->params["slots"].toList().size(), 3);   // two wired + trailing empty
        QCOMPARE(graph.inputPorts(11).size(), 3);

        QCOMPARE(graph.node(13)->params["slots"].toList().size(), 3);
    }

    void edgeValidationPerKind() {
        ClipGraph g;
        const NodeId clip = g.addNode(NT::Input, {10, 10}, canvas("clip", true));
        const NodeId silent = g.addNode(NT::Input, {10, 10}, canvas("silent", false));
        const NodeId text = g.addNode(NT::Input, {10, 10},
            {{"source", QVariantMap{{"kind", int(SourceDescriptor::Kind::Text)}, {"displayName", "t"}}}});
        const NodeId proc = g.addNode(NT::Process, {10, 10}, {{"effect", 4}});
        const NodeId layer = g.addNode(NT::Layer);
        const NodeId out = g.outputNodeId();
        const NodeId mixer = g.addNode(NT::AudioMixer);
        const NodeId master = g.addNode(NT::MasterAudioOutput);
        const NodeId mic = g.addNode(NT::MasterAudioInput);
        const NodeId fx = g.addNode(NT::AudioEffect, {}, {{"effect", AudioEffects::kAudioFxEffectId}});
        const NodeId script = g.addNode(NT::Script);
        const NodeId audioAnalyzer = g.addNode(NT::AudioAnalyzer);
        QVERIFY(clip && silent && text && proc && layer && out && mixer && master && mic
                && fx && script && audioAnalyzer);

        // Chain
        QVERIFY(g.canConnectNodes(clip, proc, int(EK::Chain)));
        QVERIFY(g.canConnectNodes(clip, layer, int(EK::Chain), 0));
        QVERIFY(!g.canConnectNodes(clip, layer, int(EK::Chain), -1));
        QVERIFY(!g.canConnectNodes(clip, layer, int(EK::Chain), 5));
        QVERIFY(g.canConnectNodes(layer, out, int(EK::Chain)));
        QVERIFY(!g.canConnectNodes(out, layer, int(EK::Chain)));
        // Port-based chain refuses Layer -> Process, kind-based (programmatic) does not.
        QVERIFY(!g.canConnectPorts(layer, 0, proc, 0));
        QVERIFY(g.canConnectNodes(layer, proc, int(EK::Chain)));
        QVERIFY(g.canConnectPorts(clip, 0, proc, 0));

        // ScriptToData
        QVERIFY(g.canConnectNodes(script, text, int(EK::ScriptToData)));
        QVERIFY(!g.canConnectNodes(script, clip, int(EK::ScriptToData)));

        // ClipToShaderAudio: no node exposes a ShaderAudioIn port
        QVERIFY(!g.canConnectNodes(clip, text, int(EK::ClipToShaderAudio)));

        // ClipToAudioAnalyzer
        QVERIFY(g.canConnectNodes(clip, audioAnalyzer, int(EK::ClipToAudioAnalyzer)));
        QVERIFY(g.canConnectNodes(mic, audioAnalyzer, int(EK::ClipToAudioAnalyzer)));
        QVERIFY(!g.canConnectNodes(silent, audioAnalyzer, int(EK::ClipToAudioAnalyzer)));

        // Audio routing kinds
        QVERIFY(g.canConnectNodes(clip, mixer, int(EK::StreamToMixer), 0));
        QVERIFY(g.canConnectNodes(fx, mixer, int(EK::StreamToMixer), 0));
        QVERIFY(!g.canConnectNodes(silent, mixer, int(EK::StreamToMixer), 0));
        QVERIFY(g.canConnectNodes(mixer, master, int(EK::MixerToOutput), 0));
        QVERIFY(!g.canConnectNodes(mixer, master, int(EK::MixerToOutput), 9));
        QVERIFY(g.canConnectNodes(clip, fx, int(EK::AudioEffectChain)));
        QVERIFY(g.canConnectNodes(mic, fx, int(EK::AudioEffectChain)));
        QVERIFY(g.canConnectNodes(mic, master, int(EK::InputToMaster), 0));
        QVERIFY(!g.canConnectNodes(clip, master, int(EK::InputToMaster), 0));

        // Kinds with no port mapping are never connectable
        QVERIFY(!g.canConnectNodes(clip, layer, int(EK::TransformToContext), 0));
        QVERIFY(!g.canConnectNodes(clip, layer, int(EK::AudioToController), 0));
        QVERIFY(!g.canConnectNodes(clip, layer, 3, 0));
        QVERIFY(!g.canConnectNodes(clip, layer, 4, 0));

        // Inference prefers the video chain; self and unknown nodes are refused
        QVERIFY(g.canConnectNodes(clip, layer, -1, 0));
        QVERIFY(!g.canConnectNodes(clip, clip));
        QVERIFY(!g.canConnectNodes(clip, 9999));
    }

    void portCompatibilityMatrix() {
        using PK = ClipGraph::PortKind;
        QVERIFY(ClipGraph::portsCompatible(PK::ChainOut, PK::ChainIn));
        QVERIFY(ClipGraph::portsCompatible(PK::ChainIn, PK::ChainOut));
        QVERIFY(!ClipGraph::portsCompatible(PK::ChainOut, PK::ChainOut));
        QVERIFY(ClipGraph::portsCompatible(PK::AudioControllerOut, PK::AudioMixerIn));
        QVERIFY(ClipGraph::portsCompatible(PK::AudioEffectOut, PK::MasterAudioIn));
        QVERIFY(ClipGraph::portsCompatible(PK::AudioMixerOut, PK::MasterAudioIn));
        QVERIFY(!ClipGraph::portsCompatible(PK::AudioMixerOut, PK::AudioMixerIn));
        QVERIFY(ClipGraph::portsCompatible(PK::ScriptOut, PK::DataIn));
        QVERIFY(!ClipGraph::portsCompatible(PK::ScriptOut, PK::AudioIn));
        QVERIFY(ClipGraph::isSingleConnection(PK::ChainIn));
        QVERIFY(ClipGraph::isSingleConnection(PK::ChainOut));
        QVERIFY(!ClipGraph::isSingleConnection(PK::AudioControllerOut));
        QVERIFY(!ClipGraph::isSingleConnection(PK::DataIn));
        QVERIFY(!ClipGraph::isSingleConnection(PK::ScriptOut));
    }

    void chainOutputIsSingleAndReplaces() {
        ClipGraph g;
        const NodeId a = g.addNode(NT::Input, {}, canvas("a"));
        const NodeId p1 = g.addNode(NT::Process, {}, {{"effect", 4}});
        const NodeId p2 = g.addNode(NT::Process, {}, {{"effect", 4}});
        QVERIFY(g.connectNodes(a, p1));
        QVERIFY(g.connectNodes(a, p2));
        QCOMPARE(g.edges().size(), 1);
        QCOMPARE(g.edges().first().to, p2);
    }

    void layerEvaluationAndSlots() {
        ClipGraph g;
        const NodeId a = g.addNode(NT::Input, {}, canvas("alpha"));
        const NodeId b = g.addNode(NT::Input, {}, canvas("beta"));
        const NodeId flip = g.addNode(NT::Process, {}, {{"effect", 4}, {"params", QVariantMap{{"direction", 0}}}});
        const NodeId layer = g.addNode(NT::Layer);
        QVERIFY(g.connectNodes(a, flip));
        QVERIFY(g.connectNodes(flip, layer, int(EK::Chain), 0));
        QVERIFY(g.connectNodes(b, layer, int(EK::Chain), 1));
        QVERIFY(g.connectNodes(layer, g.outputNodeId()));

        QCOMPARE(g.outputSingleProducer(), layer);

        const auto stream = g.evaluateVideoInput(layer);
        QCOMPARE(stream.canvasWidth, 1280);
        QCOMPARE(stream.layers.size(), 1);
        const auto &composite = stream.layers.first();
        QCOMPARE(composite.layerNodeId, layer);
        QVERIFY(composite.composite);
        QCOMPARE(composite.composite->layers.size(), 2);
        // bottom -> top: slot 1 (beta) first, slot 0 (alpha through flip) last
        QCOMPARE(composite.composite->layers[0].inputNodeId, b);
        QCOMPARE(composite.composite->layers[1].inputNodeId, a);
        QCOMPARE(composite.composite->layers[1].sourceEffects.size(), 1);
        QCOMPARE(composite.composite->layers[1].sourceEffects[0].effectId, 4);

        // slot names defaulted from the source, trailing empty slot added
        const auto views = g.layerSlotViews(layer);
        QCOMPARE(views.size(), 2);
        QCOMPARE(views[0].index, 1);
        QCOMPARE(views[0].name, QStringLiteral("beta"));
        QCOMPARE(views[0].sourceNode, b);
        QCOMPARE(views[1].name, QStringLiteral("alpha"));
        QCOMPARE(g.inputPorts(layer).size(), 3);

        QVERIFY(g.setLayerSlotRect(layer, 0, 0.25f, 0.5f, 0.5f, 0.25f));
        QVERIFY(g.setLayerSlotVisible(layer, 0, false));
        const auto moved = g.layerSlotViews(layer);
        QCOMPARE(moved[1].rect, QRectF(0.25, 0.5, 0.5, 0.25));
        QVERIFY(!moved[1].visible);
        const auto restacked = g.evaluateVideoInput(layer);
        QVERIFY(!restacked.layers.first().composite->layers[1].visible);
        QCOMPARE(restacked.layers.first().composite->layers[1].baseX, 0.25f);

        int w = 0, h = 0;
        QVERIFY(g.setLayerCanvasSize(layer, 640, 360));
        QVERIFY(g.layerCanvasSize(layer, w, h));
        QCOMPARE(w, 640);
        QCOMPARE(h, 360);
        QVERIFY(!g.setLayerSlotRect(layer, 99, 0, 0, 1, 1));

        // Disconnecting the first input compacts the remaining slot downwards
        QVERIFY(g.disconnectNodes(flip, layer));
        QCOMPARE(g.inputPorts(layer).size(), 2);
        QCOMPARE(g.edges().last().kind, EK::Chain);
        QCOMPARE(g.producerForInput(layer, 0), b);
    }

    void idBaseKeepsIdsAboveBase() {
        const quint64 base = quint64(3) << 40;
        ClipGraph g;
        g.setIdBase(base);
        const NodeId a = g.addNode(NT::Input, {}, canvas("a"));
        QCOMPARE(a, base + 1);
        QVERIFY(g.outputNodeId() > base);

        ClipGraph again;
        again.setIdBase(base);
        QVERIFY(again.fromJson(g.toJson()));
        QVERIFY(again.hasNode(a));
        QCOMPARE(again.addNode(NT::Process, {}, {{"effect", 4}}), again.toJson()["nextId"].toInteger() - 1);
        QVERIFY(again.nodeIds().last() > base);

        ClipGraph empty;
        empty.setIdBase(base);
        QVERIFY(empty.fromJson(QJsonObject{{"graphVersion", ClipGraph::kGraphVersion}}));
        QCOMPARE(empty.addNode(NT::Input, {}, canvas("b")), base + 1);
    }

    void audioRouting() {
        ClipGraph g;
        const NodeId clip = g.addNode(NT::Input, {}, canvas("song", true));
        const NodeId fx = g.addNode(NT::AudioEffect, {}, {{"effect", AudioEffects::kAudioFxEffectId}});
        const NodeId mixer = g.addNode(NT::AudioMixer);
        const NodeId master = g.addNode(NT::MasterAudioOutput);
        QVERIFY(g.addNode(NT::MasterAudioOutput) == 0);   // only one allowed

        QVERIFY(g.setParams(master, {{"devicePorts", QVariantList{
            QVariantMap{{"deviceId", ""}, {"deviceLabel", "Default"}},
            QVariantMap{{"deviceId", "hdmi"}, {"deviceLabel", "HDMI"}}}}}));
        QVERIFY(g.connectNodes(clip, fx, int(EK::AudioEffectChain)));
        QVERIFY(g.connectNodes(fx, mixer, int(EK::StreamToMixer), 0));
        QVERIFY(g.connectNodes(mixer, master, int(EK::MixerToOutput), 1));

        ClipGraph::ResolvedAudioRoute route;
        QVERIFY(g.resolveAudioStreamRoute(clip, route));
        QCOMPARE(route.effects.size(), 1);
        QCOMPARE(route.effects[0].nodeId, int(fx));

        QVERIFY(g.setParams(fx, {{"bypass", true}}));
        QVERIFY(g.resolveAudioStreamRoute(clip, route));
        QVERIFY(route.effects.isEmpty());
        QCOMPARE(route.mixerNodeId, mixer);
        QVERIFY(g.setParams(fx, {{"bypass", false}}));
        QVERIFY(g.resolveAudioStreamRoute(clip, route));
        QCOMPARE(route.effects.size(), 1);
        QCOMPARE(route.mixerNodeId, mixer);
        QCOMPARE(route.mixerSlotIndex, 0);
        QCOMPARE(route.outputNodeId, master);
        QCOMPARE(route.outputPortIndex, 1);
        QCOMPARE(route.outputDeviceId, QStringLiteral("hdmi"));

        // slot name picked up from the source, volume/mute settable
        int volume = 0; bool muted = true; QString name;
        QVERIFY(g.mixerSlotSettings(mixer, 0, volume, muted, name));
        QVERIFY(name.isEmpty());   // effect producers don't name the slot
        QVariantList slotList = g.node(mixer)->params["slots"].toList();
        QVariantMap s0 = slotList[0].toMap();
        s0["volume"] = 35;
        s0["muted"] = true;
        slotList[0] = s0;
        QVERIFY(g.setParams(mixer, {{"slots", slotList}}));

        int vol = 0, delay = -1, mode = -1;
        bool mute = false, routed = false;
        QString device;
        QVERIFY(g.audioSettingsForClip(clip, vol, mute, routed, mode, delay, device));
        QVERIFY(routed);
        QCOMPARE(vol, 35);
        QVERIFY(mute);
        QCOMPARE(device, QStringLiteral("hdmi"));
        QVERIFY(!g.audioSettingsForClip(master, vol, mute, routed, mode, delay, device));

        QString dev;
        QVERIFY(g.masterAudioOutputDeviceForPort(master, 1, dev));
        QCOMPARE(dev, QStringLiteral("hdmi"));
        QVERIFY(g.masterAudioOutputDevice(master, dev));
        QVERIFY(dev.isEmpty());
        QCOMPARE(g.audioOutputNodeId(), master);
        QCOMPARE(g.allAudioMixerNodeIds(), QVector<NodeId>{mixer});

        // Dropping a device port drops its edge
        QVERIFY(g.setParams(master, {{"devicePorts", QVariantList{
            QVariantMap{{"deviceId", ""}, {"deviceLabel", "Default"}}}}}));
        QVERIFY(!g.resolveAudioStreamRoute(clip, route));
    }

    void inputVolumeAndMuteReachDeckGain() {
        ClipGraph g;
        const NodeId clip = g.addNode(NT::Input, {}, canvas("song", true));
        const NodeId mixer = g.addNode(NT::AudioMixer);
        const NodeId master = g.addNode(NT::MasterAudioOutput);
        QVERIFY(g.connectNodes(clip, mixer, int(EK::StreamToMixer), 0));
        QVERIFY(g.connectNodes(mixer, master, int(EK::MixerToOutput), 0));

        int volume = 0, mode = 0, delay = 0;
        bool muted = true, routed = false;
        QString device;
        QVERIFY(g.audioSettingsForClip(clip, volume, muted, routed, mode, delay, device));
        QVERIFY(routed);
        QCOMPARE(volume, 100);
        QVERIFY(!muted);

        QVERIFY(g.setParams(clip, {{"audioVolume", 50}}));
        QVERIFY(g.audioSettingsForClip(clip, volume, muted, routed, mode, delay, device));
        QCOMPARE(volume, 50);

        QVERIFY(g.setParams(clip, {{"audioMuted", true}}));
        QVERIFY(g.audioSettingsForClip(clip, volume, muted, routed, mode, delay, device));
        QVERIFY(muted);

        QVariantList mixSlots = g.node(mixer)->params.value("slots").toList();
        QVariantMap entry = mixSlots[0].toMap();
        entry["volume"] = 50;
        mixSlots[0] = entry;
        QVERIFY(g.setParams(mixer, {{"slots", mixSlots}}));
        QVERIFY(g.setParams(clip, {{"audioMuted", false}}));
        QVERIFY(g.audioSettingsForClip(clip, volume, muted, routed, mode, delay, device));
        QCOMPARE(volume, 25);

        const NodeId mic = g.addNode(NT::MasterAudioInput);
        QVERIFY(g.setParams(mic, {{"volume", 40}, {"muted", true}}));
        ClipGraph::MasterAudioSettings s;
        QVERIFY(g.masterAudioInputSettings(mic, s));
        QCOMPARE(s.volume, 40);
        QVERIFY(s.muted);
    }

    void outputRowsAddAndRemove() {
        ClipGraph g;
        const NodeId clip = g.addNode(NT::Input, {}, canvas("song", true));
        const NodeId master = g.addNode(NT::MasterAudioOutput);
        QCOMPARE(g.node(master)->params.value("devicePorts").toList().size(), 1);
        QCOMPARE(g.node(master)->params.value("devicePorts").toList()[0].toMap().value("deviceId").toString(),
                 QStringLiteral("Main"));

        const QVariantMap second{{"deviceId", "Output 2"}, {"deviceLabel", "Output 2"}};
        const QVariantList first = g.node(master)->params.value("devicePorts").toList();
        QVERIFY(g.setParams(master, {{"devicePorts", first + QVariantList{second}}}));
        QCOMPARE(g.node(master)->params.value("devicePorts").toList().size(), 2);
        QCOMPARE(g.inputPorts(master).size(), 2);

        const NodeId mixer = g.addNode(NT::AudioMixer);
        QVERIFY(g.connectNodes(clip, mixer, int(EK::StreamToMixer), 0));
        QVERIFY(g.connectNodes(mixer, master, int(EK::MixerToOutput), 1));
        ClipGraph::ResolvedAudioRoute route;
        QVERIFY(g.resolveAudioStreamRoute(clip, route));
        QCOMPARE(route.outputPortIndex, 1);
        QString device;
        QVERIFY(g.masterAudioOutputDeviceForPort(master, 1, device));
        QCOMPARE(device, QStringLiteral("Output 2"));

        // Removing the first row shifts the connected one up
        QVERIFY(g.setParams(master, {{"devicePorts", QVariantList{second}}}));
        QCOMPARE(g.inputPorts(master).size(), 1);
        QVERIFY(g.resolveAudioStreamRoute(clip, route));
        QCOMPARE(route.outputPortIndex, 0);

        // The last row cannot be removed
        QVERIFY(g.setParams(master, {{"devicePorts", QVariantList{}}}));
        QCOMPARE(g.node(master)->params.value("devicePorts").toList().size(), 1);
    }

    void pedalboardWetDry() {
        ClipGraph g;
        const NodeId clip = g.addNode(NT::Input, {}, canvas("song", true));
        const NodeId fx = g.addNode(NT::AudioEffect, {}, {{"effect", AudioEffects::kAudioFxEffectId}});
        const NodeId master = g.addNode(NT::MasterAudioOutput);
        QVERIFY(g.connectNodes(clip, fx, int(EK::AudioEffectChain)));
        QVERIFY(g.connectNodes(fx, master, int(EK::InputToMaster), 0));
        ClipGraph::ResolvedAudioRoute route;
        QVERIFY(g.resolveAudioStreamRoute(clip, route));
        QCOMPARE(route.effects[0].wet, 1.0f);
        QVERIFY(g.setParams(fx, {{"wet", 0.25}}));
        QVERIFY(g.resolveAudioStreamRoute(clip, route));
        QCOMPARE(route.effects[0].wet, 0.25f);
    }

    void dragConnectInsertsMixer() {
        ClipGraph g;
        const NodeId mic = g.addNode(NT::MasterAudioInput, {}, {{"deviceId", "m"}, {"deviceLabel", "Desk Mic"}});
        const NodeId cap = g.addNode(NT::MasterAudioCapture);
        const NodeId master = g.addNode(NT::MasterAudioOutput, {400, 100});

        QVERIFY(g.canConnectPorts(mic, 0, master, 0));
        QVERIFY(g.connectPorts(mic, 0, master, 0));
        const auto mixers = g.allAudioMixerNodeIds();
        QCOMPARE(mixers.size(), 1);
        const NodeId mixer = mixers.first();

        ClipGraph::ResolvedAudioRoute route;
        QVERIFY(g.resolveAudioStreamRoute(mic, route));
        QCOMPARE(route.mixerNodeId, mixer);
        QCOMPARE(route.outputNodeId, master);

        // A second stream into the same output reuses the mixer
        QVERIFY(g.connectPorts(cap, 0, master, 0));
        QCOMPARE(g.allAudioMixerNodeIds().size(), 1);
        QVERIFY(g.resolveAudioStreamRoute(cap, route));
        QCOMPARE(route.mixerNodeId, mixer);
        QCOMPARE(route.mixerSlotIndex, 1);

        ClipGraph::MasterAudioSettings micSettings;
        QVERIFY(g.masterAudioInputSettings(mic, micSettings));
        QCOMPARE(micSettings.deviceLabel, QStringLiteral("Desk Mic"));
        QCOMPARE(micSettings.routedMasterOutputId, master);
        QVERIFY(!g.masterAudioCaptureSettings(mic, micSettings));
        QVERIFY(g.isLiveAudioAnalyzerProducer(mic));

        // slot names defaulted from the producers
        int v; bool m; QString n;
        QVERIFY(g.mixerSlotSettings(mixer, 0, v, m, n));
        QCOMPARE(n, QStringLiteral("Desk Mic"));
        QVERIFY(g.mixerSlotSettings(mixer, 1, v, m, n));
        QCOMPARE(n, QStringLiteral("System Audio"));
    }

    void scriptQueries() {
        ClipGraph g;
        const NodeId clip = g.addNode(NT::Input, {}, {
            {"source", QVariantMap{{"kind", int(SourceDescriptor::Kind::VideoFile)},
                                   {"path", "/tmp/x.mp4"}}},
            {"hasAudio", true}});
        const NodeId audioAnalyzer = g.addNode(NT::AudioAnalyzer);
        const NodeId s1 = g.addNode(NT::Script);
        const NodeId s2 = g.addNode(NT::Script);
        const NodeId text = g.addNode(NT::Input, {}, {
            {"source", QVariantMap{{"kind", int(SourceDescriptor::Kind::Text)}}}});

        QVERIFY(g.connectNodes(clip, audioAnalyzer, int(EK::ClipToAudioAnalyzer)));
        QString path;
        QVERIFY(g.audioSourceForAudioAnalyzer(audioAnalyzer, path));
        QCOMPARE(path, QStringLiteral("/tmp/x.mp4"));
        QCOMPARE(g.audioSourceNodeIdForAudioAnalyzer(audioAnalyzer), clip);
        QVERIFY(g.hasAudioAnalyzerConsumer(clip));
        QVERIFY(!g.hasAudioAnalyzerConsumer(text));

        QVERIFY(g.connectNodes(s1, text, int(EK::ScriptToData)));
        QVERIFY(g.connectNodes(s2, text, int(EK::ScriptToData)));
        QCOMPARE(g.scriptProducersForData(text), (QVector<NodeId>{s1, s2}));
        QCOMPARE(g.dataScriptNodeId(text), s1);
    }

    void removalRules() {
        ClipGraph g;
        const NodeId a = g.addNode(NT::Input, {}, canvas("a"));
        const NodeId out = g.outputNodeId();
        QVERIFY(out != 0);
        QVERIFY(!g.removeNode(out));          // other nodes remain
        QVERIFY(g.removeNode(a));
        QVERIFY(g.isEmptyGraph());
        QVERIFY(g.removeNode(out));
        QCOMPARE(g.outputNodeId(), NodeId(0));
        QVERIFY(!g.removeNode(12345));
    }

    void insertIntoEdgeSplices() {
        ClipGraph g;
        const NodeId a = g.addNode(NT::Input, {}, canvas("a"));
        const NodeId layer = g.addNode(NT::Layer);
        const NodeId p = g.addNode(NT::Process, {}, {{"effect", 4}});
        QVERIFY(g.connectNodes(a, layer, int(EK::Chain), 0));
        const EdgeId e = g.edges().first().id;
        QVERIFY(g.insertIntoEdge(p, e));
        QCOMPARE(g.edges().size(), 2);
        QCOMPARE(g.producerForInput(layer, 0), p);
        QCOMPARE(g.producerForInput(p, 0), a);
    }

    void bypassedProcessAddsNoSourceEffect() {
        ClipGraph g;
        const NodeId a = g.addNode(NT::Input, {}, canvas("a"));
        const NodeId p = g.addNode(NT::Process, {}, {{"effect", 4}});
        QVERIFY(g.connectNodes(a, p));
        QCOMPARE(g.evaluateVideoInput(p).layers.first().sourceEffects.size(), 1);
        QVERIFY(g.setParams(p, {{"bypass", true}}));
        QCOMPARE(g.evaluateVideoInput(p).layers.first().sourceEffects.size(), 0);

        ClipGraph g2;
        QVERIFY(g2.fromJson(g.toJson()));
        QVERIFY(g2.node(p)->params.value("bypass").toBool());
    }

    void bypassChangesStructuralKeyAndTogglesAsOneUndoStep() {
        ClipGraphController c;
        const quint64 a = c.addNode(int(NT::Input), 0, 0, canvas("a"));
        const quint64 p = c.addNode(int(NT::Process), 0, 0, {{"effect", 4}});
        const quint64 q = c.addNode(int(NT::Process), 0, 0, {{"effect", 4}});
        ClipGraph *g = c.graph();
        QVERIFY(g->connectNodes(a, p));
        QVERIFY(g->connectNodes(p, q));
        const auto fx = [&] { return g->evaluateVideoInput(q).layers.first().sourceEffects; };
        const QString before = ProcessEffects::sourceEffectsKey(fx());
        QCOMPARE(fx().size(), 2);

        c.clearHistory();
        QVERIFY(c.toggleBypass({QVariant::fromValue<quint64>(p), QVariant::fromValue<quint64>(q)}));
        QCOMPARE(fx().size(), 0);
        QVERIFY(ProcessEffects::sourceEffectsKey(fx()) != before);
        QVERIFY(g->node(p)->params.value("bypass").toBool());
        QVERIFY(g->node(q)->params.value("bypass").toBool());

        c.undo();
        QVERIFY(!g->node(p)->params.value("bypass").toBool());
        QVERIFY(!g->node(q)->params.value("bypass").toBool());
        QCOMPARE(ProcessEffects::sourceEffectsKey(fx()), before);
    }

    void coalescedParamEditsShareOneUndoStep() {
        ClipGraphController c;
        const quint64 p = c.addNode(int(NT::Process), 0, 0, {{"effect", 8}});
        QVERIFY(c.setParams(p, {{"params", QVariantMap{{"radius", 3}}}}, true));
        QVERIFY(c.setParams(p, {{"params", QVariantMap{{"radius", 4}}}}, true));
        QVERIFY(c.setParams(p, {{"params", QVariantMap{{"radius", 5}}}}, true));
        QCOMPARE(c.graph()->node(p)->params["params"].toMap()["radius"].toInt(), 5);
        c.undo();
        QVERIFY(c.graph()->node(p)->params["params"].toMap()["radius"].toInt() != 3
                && c.graph()->node(p)->params["params"].toMap()["radius"].toInt() != 4);
    }

    void inputModelParamsCarryPlaybackCaps() {
        ClipGraphController c;
        const quint64 a = c.addNode(int(NT::Input), 10, 20, canvas("a"));
        const auto idx = c.nodes()->index(c.nodes()->rowOfNode(a));
        const QVariantMap p = c.nodes()->data(idx, ClipGraphNodesModel::ParamsRole).toMap();
        QVERIFY(!p.value("repeatable").toBool());
        QVERIFY(!p.value("seekable").toBool());
        QVERIFY(!p.value("pausable").toBool());
        QVERIFY(!p.value("speedControl").toBool());
    }

    void signalsAreFineGrained() {
        ClipGraph g;
        QSignalSpy added(&g, &ClipGraph::nodeAdded);
        QSignalSpy removed(&g, &ClipGraph::nodeRemoved);
        QSignalSpy changed(&g, &ClipGraph::nodeChanged);
        QSignalSpy edgeAdded(&g, &ClipGraph::edgeAdded);
        QSignalSpy edgeRemoved(&g, &ClipGraph::edgeRemoved);

        const NodeId a = g.addNode(NT::Input, {}, canvas("a"));
        QCOMPARE(added.size(), 2);                    // input + auto-created output
        const NodeId p = g.addNode(NT::Process, {}, {{"effect", 4}});
        QCOMPARE(added.size(), 3);

        changed.clear();
        QVERIFY(g.moveNode(a, {50, 60}));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(g.node(a)->pos, QPointF(50, 60));

        QVERIFY(g.setEffectParams(p, {{"direction", 2}}));
        QCOMPARE(g.node(p)->params["params"].toMap()["direction"].toInt(), 2);

        QVERIFY(g.connectNodes(a, p));
        QCOMPARE(edgeAdded.size(), 1);
        QVERIFY(g.removeNode(p));
        QCOMPARE(edgeRemoved.size(), 1);
        QCOMPARE(removed.size(), 1);
        QCOMPARE(removed.first().first().toULongLong(), p);
    }

    void controllerUndoRedoAndModels() {
        ClipGraphController c;
        QCOMPARE(c.nodes()->rowCount(), 0);

        const quint64 a = c.addNode(int(NT::Input), 10, 20, canvas("a"));
        QVERIFY(a != 0);
        QCOMPARE(c.nodes()->rowCount(), 2);   // input + output
        const quint64 layer = c.addNode(int(NT::Layer), 100, 20);
        QCOMPARE(c.nodes()->rowCount(), 3);

        const auto idx = c.nodes()->index(c.nodes()->rowOfNode(layer));
        QCOMPARE(c.nodes()->data(idx, ClipGraphNodesModel::TypeRole).toInt(), int(NT::Layer));
        QCOMPARE(c.nodes()->data(idx, ClipGraphNodesModel::InputPortsRole).toList().size(), 1);
        QCOMPARE(c.nodes()->data(idx, ClipGraphNodesModel::TitleRole).toString(), QStringLiteral("Layer"));

        // two-step connect
        QVERIFY(c.beginConnection(a, true, 0));
        QVERIFY(c.connecting());
        QVERIFY(c.canConnect(layer, false, 0));
        QVERIFY(!c.canConnect(layer, true, 0));
        QVERIFY(c.finishConnection(layer, false, 0));
        QVERIFY(!c.connecting());
        QCOMPARE(c.edges()->rowCount(), 1);
        QCOMPARE(c.edges()->data(c.edges()->index(0), ClipGraphEdgesModel::KindRole).toInt(), int(EK::Chain));
        QCOMPARE(c.edges()->data(c.edges()->index(0), ClipGraphEdgesModel::ToNodeRole).toULongLong(), layer);
        QVERIFY(c.nodes()->data(idx, ClipGraphNodesModel::InputPortsRole).toList().first().toMap()["connected"].toBool());

        // incompatible drop is refused
        QVERIFY(c.beginConnection(layer, true, 0));
        QVERIFY(!c.canConnect(a, false, 0));
        QVERIFY(!c.finishConnection(a, false, 0));

        c.undo();
        QCOMPARE(c.edges()->rowCount(), 0);
        QVERIFY(c.canRedo());
        c.redo();
        QCOMPARE(c.edges()->rowCount(), 1);

        // move merges until endMove
        c.moveNode(layer, 120, 30);
        c.moveNode(layer, 140, 40);
        c.endMove();
        QCOMPARE(c.graph()->node(layer)->pos, QPointF(140, 40));
        c.undo();
        QCOMPARE(c.graph()->node(layer)->pos, QPointF(100, 20));
        c.redo();

        c.select({QVariant::fromValue<quint64>(layer)});
        QVERIFY(c.isSelected(layer));
        c.removeNodes({QVariant::fromValue<quint64>(layer)});
        QVERIFY(!c.isSelected(layer));
        QCOMPARE(c.edges()->rowCount(), 0);
        QCOMPARE(c.nodes()->rowOfNode(layer), -1);
        c.undo();
        QVERIFY(c.graph()->hasNode(layer));
        QCOMPARE(c.edges()->rowCount(), 1);

        QVERIFY(c.setParams(layer, {{"canvasW", 800}}));
        QCOMPARE(c.graph()->node(layer)->params["canvasW"].toInt(), 800);
        c.undo();
        QCOMPARE(c.graph()->node(layer)->params["canvasW"].toInt(), 1280);

        QVERIFY(c.disconnectEdge(c.edges()->data(c.edges()->index(0), ClipGraphEdgesModel::EdgeIdRole).toULongLong()));
        QCOMPARE(c.edges()->rowCount(), 0);
    }

    void undoRestoresGraphJson() {
        ClipGraphController c;
        c.addNode(int(NT::Input), 0, 0, canvas("a", true));
        c.addNode(int(NT::AudioMixer), 0, 0);
        const QJsonObject full = c.graph()->toJson();
        c.addNode(int(NT::Layer), 0, 0);
        c.undo();
        QVERIFY2(c.graph()->toJson() == full, qPrintable(dump(c.graph()->toJson())));
    }

    void sourceChangeIsUndoable() {
        ClipGraphController c;
        const quint64 a = c.addNode(int(NT::Input), 0, 0, canvas("a"));
        const quint64 mixer = c.addNode(int(NT::AudioMixer), 0, 0);
        QVERIFY(!c.setSource(mixer, {}));

        SourceDescriptor desc = c.graph()->sourceDescriptor(a);
        QCOMPARE(desc.displayName, QStringLiteral("a"));
        desc.displayName = QStringLiteral("renamed");
        QVERIFY(c.setSourceDescriptor(a, desc));
        QCOMPARE(c.graph()->sourceName(a), QStringLiteral("renamed"));

        c.undo();
        QCOMPARE(c.graph()->sourceName(a), QStringLiteral("a"));
        c.redo();
        QCOMPARE(c.graph()->sourceName(a), QStringLiteral("renamed"));

        QVariantMap map = ClipGraph::sourceToMap(desc);
        map["displayName"] = QStringLiteral("viaMap");
        QVERIFY(c.setSource(a, map));
        QCOMPARE(c.graph()->sourceName(a), QStringLiteral("viaMap"));
        c.undo();
        QCOMPARE(c.graph()->sourceName(a), QStringLiteral("renamed"));
    }

    void insertIntoEdgeGeneralised() {
        ClipGraph g;
        const NodeId a = g.addNode(NT::Input, {}, canvas("a"));
        const NodeId layer = g.addNode(NT::Layer);
        QVERIFY(g.connectNodes(a, layer, int(EK::Chain), 0));
        const EdgeId e = g.edges().first().id;

        const NodeId mic = g.addNode(NT::MasterAudioInput);
        QVERIFY(!g.canInsertIntoEdge(mic, e));
        QVERIFY(!g.insertIntoEdge(mic, e));
        const NodeId fx = g.addNode(NT::AudioEffect, {}, {{"effect", 0}});
        QVERIFY(!g.canInsertIntoEdge(fx, e));

        const NodeId l2 = g.addNode(NT::Layer);
        QVERIFY(g.canInsertIntoEdge(l2, e));
        QVERIFY(g.insertIntoEdge(l2, e));
        QCOMPARE(g.producerForInput(layer, 0), l2);
        QCOMPARE(g.producerForInput(l2, 0), a);

        const NodeId p = g.addNode(NT::Process, {}, {{"effect", 4}});
        const NodeId q = g.addNode(NT::Process, {}, {{"effect", 4}});
        QVERIFY(g.connectNodes(a, p));
        QVERIFY(!g.canInsertIntoEdge(p, e));
        QVERIFY(!g.canInsertIntoEdge(q, 9999));
    }

    void detachReconnectsNeighbours() {
        ClipGraphController c;
        const quint64 a = c.addNode(int(NT::Input), 0, 0, canvas("a"));
        const quint64 p = c.addNode(int(NT::Process), 0, 0, {{"effect", 4}});
        const quint64 layer = c.addNode(int(NT::Layer), 0, 0);
        QVERIFY(c.graph()->connectNodes(a, p));
        QVERIFY(c.graph()->connectNodes(p, layer, int(EK::Chain), 0));
        QCOMPARE(c.graph()->edges().size(), 2);

        QVERIFY(c.detachNode(p));
        QCOMPARE(c.graph()->edges().size(), 1);
        QCOMPARE(c.graph()->producerForInput(layer, 0), a);
        QVERIFY(!c.graph()->isPortConnected(p, ClipGraph::PortDirection::In, 0));
        QVERIFY(!c.detachNode(p));

        c.undo();
        QCOMPARE(c.graph()->edges().size(), 2);
        QCOMPARE(c.graph()->producerForInput(layer, 0), p);
    }

    void copyPasteKeepsInternalEdgesAndNewIds() {
        ClipGraphController c;
        const quint64 a = c.addNode(int(NT::Input), 100, 50, canvas("a"));
        const quint64 p = c.addNode(int(NT::Process), 300, 80, {{"effect", 8}});
        const quint64 layer = c.addNode(int(NT::Layer), 500, 50);
        QVERIFY(c.setParams(p, {{"params", QVariantMap{{"radius", 7}}}}));
        QVERIFY(c.graph()->connectNodes(a, p));
        QVERIFY(c.graph()->connectNodes(p, layer, int(EK::Chain), 0));
        const int nodesBefore = c.graph()->nodeIds().size();

        c.select({a, p});
        QVERIFY(c.copySelection());
        const QVariantList pasted = c.paste(1000, 400);
        QCOMPARE(pasted.size(), 2);
        QCOMPARE(c.graph()->nodeIds().size(), nodesBefore + 2);
        for (const QVariant &v : pasted)
            QVERIFY(v.toULongLong() != a && v.toULongLong() != p && v.toULongLong() != layer);
        QCOMPARE(c.selection(), pasted);

        const quint64 na = pasted[0].toULongLong();
        const quint64 np = pasted[1].toULongLong();
        QCOMPARE(c.graph()->node(na)->pos, QPointF(1000, 400));
        QCOMPARE(c.graph()->node(np)->pos, QPointF(1200, 430));
        QCOMPARE(c.graph()->node(np)->params["params"].toMap()["radius"].toInt(), 7);
        QCOMPARE(c.graph()->producerForInput(np, 0), na);
        // The edge leaving the selection is not copied.
        QCOMPARE(c.graph()->edges().size(), 3);

        c.undo();
        QCOMPARE(c.graph()->nodeIds().size(), nodesBefore);
        QCOMPARE(c.graph()->edges().size(), 2);
    }

    void duplicateOffsetsAndSkipsOutput() {
        ClipGraphController c;
        const quint64 a = c.addNode(int(NT::Input), 10, 20, canvas("a"));
        const quint64 out = c.graph()->outputNodeId();
        c.select({a, out});
        const int before = c.graph()->nodeIds().size();
        const QVariantList dup = c.duplicateSelection();
        QCOMPARE(dup.size(), 1);
        QCOMPARE(c.graph()->nodeIds().size(), before + 1);
        QCOMPARE(c.graph()->node(dup[0].toULongLong())->pos, QPointF(50, 60));
        QCOMPARE(c.graph()->sourceName(dup[0].toULongLong()), QStringLiteral("a"));
        c.undo();
        QCOMPARE(c.graph()->nodeIds().size(), before);
    }

    void addNodeIntoEdgeIsOneUndoStep() {
        ClipGraphController c;
        const quint64 a = c.addNode(int(NT::Input), 0, 0, canvas("a"));
        const quint64 layer = c.addNode(int(NT::Layer), 0, 0);
        QVERIFY(c.graph()->connectNodes(a, layer, int(EK::Chain), 0));
        const quint64 e = c.graph()->edges().first().id;
        const quint64 p = c.addNodeIntoEdge(int(NT::Process), 50, 50, {{"effect", 4}}, e);
        QVERIFY(p != 0);
        QCOMPARE(c.graph()->producerForInput(layer, 0), p);
        c.undo();
        QVERIFY(!c.graph()->hasNode(p));
        QCOMPARE(c.graph()->producerForInput(layer, 0), a);
    }

    void modulatorRoundTripsThroughJson() {
        ClipGraph g;
        const NodeId m = g.addNode(NT::Modulator, QPointF(10, 20),
                                   {{"kind", 4}, {"smooth", true}, {"rateBeats", 0.5}, {"sync", true},
                                    {"seed", 7}, {"max", 100.0}, {"in", QStringLiteral("mod:3")}});
        QVERIFY(m != 0);
        QCOMPARE(g.node(m)->title, QStringLiteral("Random"));
        QCOMPARE(g.outputPorts(m).size(), 1);
        QCOMPARE(g.outputPorts(m).first().label, QStringLiteral("value"));

        const QJsonObject json = g.toJson();
        QCOMPARE(json["modulatorNodes"].toArray().size(), 1);
        ClipGraph again;
        QVERIFY(again.fromJson(json));
        QVERIFY(again.hasNode(m));
        const ClipGraph::Node &n = *again.node(m);
        QCOMPARE(n.type, NT::Modulator);
        QCOMPARE(n.pos, QPointF(10, 20));
        QCOMPARE(n.params.value("kind").toInt(), 4);
        QCOMPARE(n.params.value("smooth").toBool(), true);
        QCOMPARE(n.params.value("sync").toBool(), true);
        QCOMPARE(n.params.value("rateBeats").toDouble(), 0.5);
        QCOMPARE(n.params.value("seed").toInt(), 7);
        QCOMPARE(n.params.value("max").toDouble(), 100.0);
        QCOMPARE(n.params.value("in").toString(), QStringLiteral("mod:3"));
        QCOMPARE(again.toJson()["modulatorNodes"], json["modulatorNodes"]);
        QVERIFY(again.toJson()["nextId"].toInteger() > qint64(m));

        const QJsonObject snap = again.snapshot([](NodeId) { return 0.25; });
        bool found = false;
        for (const QJsonValue &v : snap["nodes"].toArray()) {
            const QJsonObject row = v.toObject();
            if (row["type"].toString() != QStringLiteral("modulator")) continue;
            found = true;
            QCOMPARE(row["kind"].toString(), QStringLiteral("random"));
            QCOMPARE(row["value"].toDouble(), 0.25);
        }
        QVERIFY(found);
    }

    void bindParamRoundTripsAndIsOneUndoStep() {
        ClipGraphController c;
        const quint64 p = c.addNode(int(NT::Process), 0, 0, {{"effect", 6}});
        QVERIFY(p != 0);
        c.clearHistory();

        const quint64 mod = c.bindParam(p, QStringLiteral("params.opacity"), 0, 0.0, 1.0);
        QVERIFY(mod != 0);
        QCOMPARE(c.graph()->node(mod)->type, NT::Modulator);
        QCOMPARE(c.graph()->node(mod)->params.value("max").toDouble(), 1.0);
        QCOMPARE(c.graph()->node(p)->params.value("expressions").toMap().value("params.opacity").toString(),
                 QStringLiteral("mod(%1)").arg(mod));
        QCOMPARE(c.modulators().size(), 1);

        const QJsonObject json = c.graph()->toJson();
        ClipGraph again;
        QVERIFY(again.fromJson(json));
        QCOMPARE(again.node(p)->params.value("expressions").toMap().value("params.opacity").toString(),
                 QStringLiteral("mod(%1)").arg(mod));
        QVERIFY(again.hasNode(mod));

        c.undo();
        QVERIFY(!c.graph()->hasNode(mod));
        QVERIFY(c.graph()->node(p)->params.value("expressions").toMap().isEmpty());
        QVERIFY(!c.canUndo());

        c.redo();
        QVERIFY(c.graph()->hasNode(mod));
        QCOMPARE(c.bindParam(p, QStringLiteral("params.opacity"), 999999), quint64(0));
    }

    void framesRoundTripAndAreOptional() {
        ClipGraph g;
        const NodeId a = g.addNode(NT::Input, {}, canvas("a"));
        QVERIFY(!g.toJson().contains("frames"));
        QVERIFY(!g.snapshot().contains("frames"));

        const quint64 f = g.addFrame(QRectF(10, 20, 300, 200), "Intro", "#0090ff", "line one\nline two");
        QVERIFY(f != 0);
        QVERIFY(f != a && !g.hasNode(f));
        QCOMPARE(g.toJson()["graphVersion"].toInt(), 6);
        QCOMPARE(g.toJson()["frames"].toArray().size(), 1);

        ClipGraph again;
        QVERIFY(again.fromJson(g.toJson()));
        QCOMPARE(again.frames().size(), 1);
        const ClipGraph::Frame *r = again.frame(f);
        QVERIFY(r);
        QCOMPARE(r->rect, QRectF(10, 20, 300, 200));
        QCOMPARE(r->title, QStringLiteral("Intro"));
        QCOMPARE(r->color, QStringLiteral("#0090ff"));
        QCOMPARE(r->comment, QStringLiteral("line one\nline two"));
        QCOMPARE(again.toJson().value("frames"), g.toJson().value("frames"));
        QVERIFY(again.addNode(NT::Input, {}, canvas("b")) != f);

        const QJsonArray snapFrames = g.snapshot()["frames"].toArray();
        QCOMPARE(snapFrames.size(), 1);
        QCOMPARE(snapFrames[0].toObject()["title"].toString(), QStringLiteral("Intro"));
        QCOMPARE(snapFrames[0].toObject()["comment"].toString(), QStringLiteral("line one\nline two"));

        QJsonObject noFrames = g.toJson();
        noFrames.remove("frames");
        QVERIFY(again.fromJson(noFrames));
        QVERIFY(again.frames().isEmpty());

        QVERIFY(g.removeFrame(f));
        QVERIFY(!g.toJson().contains("frames"));
    }

    void frameMoveCarriesNodesInsideAsOneUndoStep() {
        ClipGraphController c;
        const quint64 inside = c.addNode(int(NT::Input), 100, 100, canvas("in"));
        const quint64 outside = c.addNode(int(NT::Input), 1000, 1000, canvas("out"));
        const quint64 frame = c.addFrame(0, 0, 400, 400);
        QVERIFY(frame != 0);
        c.clearHistory();

        c.moveFrame(frame, 10, 20);
        c.moveFrame(frame, 50, 60);
        c.endMove();
        ClipGraph *g = c.graph();
        QCOMPARE(g->frame(frame)->rect.topLeft(), QPointF(50, 60));
        QCOMPARE(g->node(inside)->pos, QPointF(150, 160));
        QCOMPARE(g->node(outside)->pos, QPointF(1000, 1000));
        QCOMPARE(c.undoStack()->count(), 1);

        c.undo();
        QCOMPARE(g->frame(frame)->rect.topLeft(), QPointF(0, 0));
        QCOMPARE(g->node(inside)->pos, QPointF(100, 100));
        QVERIFY(!c.canUndo());

        c.redo();
        QCOMPARE(g->node(inside)->pos, QPointF(150, 160));
    }

    void frameEditsAreUndoable() {
        ClipGraphController c;
        const quint64 a = c.addNode(int(NT::Input), 100, 100, canvas("a"));
        c.select({QVariant::fromValue<quint64>(a)});
        c.clearHistory();
        const quint64 f = c.frameSelection();
        QVERIFY(f != 0);
        QVERIFY(c.graph()->frame(f)->rect.contains(QRectF(c.graph()->node(a)->pos, c.graph()->node(a)->size)));
        QCOMPARE(c.selectedFrame(), f);
        QVERIFY(c.selection().isEmpty());

        QVERIFY(c.setFrameProps(f, {{"title", "Renamed"}, {"comment", "note"}}));
        c.resizeFrame(f, 500, 300);
        c.resizeFrame(f, 600, 320);
        c.endMove();
        QCOMPARE(c.graph()->frame(f)->rect.size(), QSizeF(600, 320));
        c.undo();
        QVERIFY(c.graph()->frame(f)->rect.size() != QSizeF(600, 320));
        c.undo();
        QCOMPARE(c.graph()->frame(f)->title, QStringLiteral("Frame"));
        QVERIFY(c.removeFrame(f));
        QVERIFY(!c.graph()->frame(f));
        c.undo();
        QVERIFY(c.graph()->frame(f));
    }

    void rerouteInVideoEdgeKeepsStream() {
        ClipGraph g;
        const NodeId a = g.addNode(NT::Input, {}, canvas("a"));
        const NodeId p = g.addNode(NT::Process, {}, {{"effect", 4}});
        const NodeId q = g.addNode(NT::Process, {}, {{"effect", 4}});
        QVERIFY(g.connectNodes(a, p));
        QVERIFY(g.connectNodes(p, q));
        const auto key = [&](const ClipGraph &gr) {
            const auto s = gr.evaluateVideoInput(q);
            return ProcessEffects::sourceEffectsKey(s.layers.first().sourceEffects);
        };
        const QString before = key(g);
        QCOMPARE(g.evaluateVideoInput(q).layers.first().sourceEffects.size(), 2);

        EdgeId edge = 0;
        for (const ClipGraph::Edge &e : g.edges())
            if (e.from == p && e.to == q) edge = e.id;
        QVERIFY(edge != 0);
        const NodeId r = g.addNode(NT::Reroute, QPointF(5, 5));
        QVERIFY(r != 0);
        QVERIFY(g.canInsertIntoEdge(r, edge));
        QVERIFY(g.insertIntoEdge(r, edge));

        QCOMPARE(g.edges().size(), 3);
        QCOMPARE(g.outputPorts(r)[0].kind, ClipGraph::PortKind::ChainOut);
        QCOMPARE(g.producerForInput(q, 0), p);
        QCOMPARE(key(g), before);
        const auto viaReroute = g.evaluateVideoInput(r);
        QCOMPARE(viaReroute.layers.first().inputNodeId, a);
        QCOMPARE(viaReroute.layers.first().sourceEffects.size(), 1);

        ClipGraph again;
        QVERIFY(again.fromJson(g.toJson()));
        QCOMPARE(again.edges().size(), 3);
        QCOMPARE(key(again), before);
        QVERIFY(again.node(r)->type == NT::Reroute);
        QVERIFY(!g.toJson()["rerouteNodes"].toArray().isEmpty());
    }

    void rerouteInLayerSlotIsTransparent() {
        ClipGraph g;
        const NodeId a = g.addNode(NT::Input, {}, canvas("alpha"));
        const NodeId layer = g.addNode(NT::Layer);
        QVERIFY(g.connectNodes(a, layer, int(EK::Chain), 0));
        EdgeId edge = g.edges().first().id;
        const NodeId r = g.addNode(NT::Reroute);
        QVERIFY(g.insertIntoEdge(r, edge));

        QCOMPARE(g.producerForInput(layer, 0), a);
        const auto stream = g.evaluateVideoInput(layer);
        QCOMPARE(stream.layers.first().composite->layers.size(), 1);
        QCOMPARE(stream.layers.first().composite->layers[0].inputNodeId, a);
        const auto views = g.layerSlotViews(layer);
        QCOMPARE(views.size(), 1);
        QCOMPARE(views[0].producer, a);
        QCOMPARE(views[0].sourceNode, a);
        QCOMPARE(views[0].name, QStringLiteral("alpha"));
    }

    void rerouteInAudioPathIsTransparent() {
        ClipGraph g;
        const NodeId clip = g.addNode(NT::Input, {}, canvas("song", true));
        const NodeId mixer = g.addNode(NT::AudioMixer);
        const NodeId master = g.addNode(NT::MasterAudioOutput);
        QVERIFY(g.connectNodes(clip, mixer, int(EK::StreamToMixer), 0));
        QVERIFY(g.connectNodes(mixer, master, int(EK::MixerToOutput), 0));
        ClipGraph::ResolvedAudioRoute before;
        QVERIFY(g.resolveAudioStreamRoute(clip, before));

        EdgeId edge = 0;
        for (const ClipGraph::Edge &e : g.edges())
            if (e.from == clip && e.to == mixer) edge = e.id;
        const NodeId r = g.addNode(NT::Reroute);
        QVERIFY(g.insertIntoEdge(r, edge));
        QCOMPARE(g.outputPorts(r)[0].kind, ClipGraph::PortKind::AudioEffectOut);

        ClipGraph::ResolvedAudioRoute after;
        QVERIFY(g.resolveAudioStreamRoute(clip, after));
        QCOMPARE(after.mixerNodeId, before.mixerNodeId);
        QCOMPARE(after.mixerSlotIndex, before.mixerSlotIndex);
        QCOMPARE(after.outputNodeId, master);
        QVERIFY(after.effects.isEmpty());

        ClipGraph again;
        QVERIFY(again.fromJson(g.toJson()));
        QVERIFY(again.resolveAudioStreamRoute(clip, after));
        QCOMPARE(after.mixerNodeId, mixer);
        QCOMPARE(after.outputNodeId, master);
    }

    void rerouteAdaptsKindAndRefusesMismatches() {
        ClipGraph g;
        const NodeId a = g.addNode(NT::Input, {}, canvas("a"));
        const NodeId r = g.addNode(NT::Reroute);
        const NodeId script = g.addNode(NT::Script);
        const NodeId proc = g.addNode(NT::Process, {}, {{"effect", 4}});

        QCOMPARE(g.inputPorts(r)[0].kind, ClipGraph::PortKind::RerouteIn);
        QVERIFY(g.canConnectPorts(r, 0, script, 0));
        QVERIFY(g.connectNodes(a, r));
        QCOMPARE(g.inputPorts(r)[0].kind, ClipGraph::PortKind::ChainIn);
        QCOMPARE(g.outputPorts(r)[0].kind, ClipGraph::PortKind::ChainOut);
        QVERIFY(!g.canConnectPorts(r, 0, script, 0));
        QVERIFY(!g.canConnectNodes(r, script));
        QVERIFY(g.canConnectPorts(r, 0, proc, 0));
        QVERIFY(g.connectPorts(r, 0, proc, 0));

        g.disconnectNodes(a, r);
        g.disconnectNodes(r, proc);
        QCOMPARE(g.outputPorts(r)[0].kind, ClipGraph::PortKind::RerouteOut);

        const NodeId s1 = g.addNode(NT::Script);
        const NodeId r2 = g.addNode(NT::Reroute);
        QVERIFY(g.connectNodes(s1, r2));
        QVERIFY(g.connectNodes(r2, script));
        QCOMPARE(g.scriptProducersForData(script), QVector<NodeId>{s1});
        QCOMPARE(g.dataScriptNodeId(script), s1);
        QVERIFY(!g.canConnectPorts(a, 0, r2, 0));
        QVERIFY(!g.canConnectPorts(r2, 0, proc, 0));
    }

    void reroutesCannotFormLoops() {
        ClipGraph g;
        const NodeId r1 = g.addNode(NT::Reroute);
        const NodeId r2 = g.addNode(NT::Reroute);
        QVERIFY(g.connectPorts(r1, 0, r2, 0));
        QVERIFY(!g.canConnectPorts(r2, 0, r1, 0));
        QVERIFY(!g.connectPorts(r2, 0, r1, 0));
        QVERIFY(!g.connectNodes(r2, r1));

        const NodeId p = g.addNode(NT::Process, {}, {{"effect", 4}});
        const NodeId q = g.addNode(NT::Process, {}, {{"effect", 4}});
        QVERIFY(g.connectPorts(p, 0, r1, 0));
        QVERIFY(g.connectPorts(r2, 0, q, 0));
        QVERIFY(!g.canConnectPorts(q, 0, p, 0));
        QCOMPARE(g.evaluateVideoInput(r1).layers.size(), 0);
    }
    void groupRoundTripsThroughJson() {
        ClipGraphController c;
        const Chain k = buildChain(c);
        c.select(ids({k.p1, k.p2, k.p3}));
        const quint64 gid = c.groupSelection();
        QVERIFY(gid != 0);
        QCOMPARE(c.selectedGroup(), gid);

        const QJsonObject json = c.graph()->toJson();
        QCOMPARE(json["groups"].toArray().size(), 1);
        QCOMPARE(json["groups"].toArray()[0].toObject()["name"].toString(), QStringLiteral("Group 1"));

        ClipGraph g2;
        QVERIFY(g2.fromJson(json));
        const ClipGraph::Group *g = g2.group(gid);
        QVERIFY(g);
        QVERIFY(g->collapsed);
        QCOMPARE(g2.groupMembers(gid), (QList<NodeId>{k.p1, k.p2, k.p3}));
        QCOMPARE(g2.node(k.p2)->group, gid);
        QCOMPARE(g2.node(k.a)->group, quint64(0));
        const QString diff = firstDiff(withoutVolatile(json, false), withoutVolatile(g2.toJson(), false));
        QVERIFY2(diff.isEmpty(), qPrintable(diff));

        QVERIFY(c.ungroupSelection());
        QVERIFY(c.graph()->groups().isEmpty());
        QCOMPARE(c.graph()->node(k.p2)->group, quint64(0));
        const QJsonObject plain = c.graph()->toJson();
        QVERIFY(!plain.contains("groups"));
        ClipGraph g3;
        QVERIFY(g3.fromJson(plain));
        QVERIFY(g3.groups().isEmpty());
    }

    void groupingDoesNotChangeEvaluation() {
        ClipGraphController c;
        const Chain k = buildChain(c);
        ClipGraph *g = c.graph();
        const auto key = [&] {
            const auto s = g->evaluateVideoInput(g->producerForInput(k.out, 0));
            return ProcessEffects::sourceEffectsKey(s.layers.first().sourceEffects)
                + ProcessEffects::sourceEffectsParamsKey(s.layers.first().sourceEffects);
        };
        const QString before = key();
        QVERIFY(!before.isEmpty());
        const int edges = g->edges().size();

        c.select(ids({k.p1, k.p2, k.p3}));
        const quint64 gid = c.groupSelection();
        QVERIFY(g->group(gid)->collapsed);
        QCOMPARE(key(), before);
        QCOMPARE(g->edges().size(), edges);

        QVERIFY(c.setGroupCollapsed(gid, false));
        QCOMPARE(key(), before);
        QVERIFY(c.ungroup(gid));
        QCOMPARE(key(), before);
        QCOMPARE(g->edges().size(), edges);
    }

    void groupCollapseAndUngroupUndoInOneStep() {
        ClipGraphController c;
        const Chain k = buildChain(c);
        ClipGraph *g = c.graph();
        c.select(ids({k.p1, k.p2, k.p3}));
        c.clearHistory();

        const quint64 gid = c.groupSelection();
        QVERIFY(gid != 0);
        c.undo();
        QVERIFY(g->groups().isEmpty());
        QCOMPARE(g->node(k.p1)->group, quint64(0));
        QVERIFY(!c.canUndo());
        c.redo();
        QVERIFY(g->group(gid));

        QVERIFY(c.toggleGroupCollapsed(gid));
        QVERIFY(!g->group(gid)->collapsed);
        c.undo();
        QVERIFY(g->group(gid));
        QVERIFY(g->group(gid)->collapsed);
        c.redo();
        QVERIFY(!g->group(gid)->collapsed);

        QVERIFY(c.ungroup(gid));
        QVERIFY(g->groups().isEmpty());
        c.undo();
        QVERIFY(g->group(gid));
        QCOMPARE(g->groupMembers(gid).size(), 3);
        QVERIFY(!g->group(gid)->collapsed);
    }

    void movingACollapsedGroupCarriesMembersInOneStep() {
        ClipGraphController c;
        const Chain k = buildChain(c);
        ClipGraph *g = c.graph();
        c.select(ids({k.p1, k.p2, k.p3}));
        const quint64 gid = c.groupSelection();
        c.clearHistory();

        const QPointF p1 = g->node(k.p1)->pos, p3 = g->node(k.p3)->pos;
        const QRectF box = g->groupRect(gid);
        c.moveGroup(gid, box.x() + 50, box.y() + 30);
        c.moveGroup(gid, box.x() + 60, box.y() + 40);
        c.endMove();
        QCOMPARE(g->node(k.p1)->pos, p1 + QPointF(60, 40));
        QCOMPARE(g->node(k.p3)->pos, p3 + QPointF(60, 40));
        QCOMPARE(g->groupRect(gid).topLeft(), box.topLeft() + QPointF(60, 40));

        c.undo();
        QCOMPARE(g->node(k.p1)->pos, p1);
        QCOMPARE(g->node(k.p3)->pos, p3);
        QCOMPARE(g->groupRect(gid).topLeft(), box.topLeft());
        QVERIFY(!c.canUndo());
    }

    void groupBoundaryPortsMapToInnerPorts() {
        ClipGraphController c;
        const Chain k = buildChain(c);
        ClipGraph *g = c.graph();
        c.select(ids({k.p1, k.p2, k.p3}));
        const quint64 gid = c.groupSelection();

        const auto ins = g->groupPorts(gid, ClipGraph::PortDirection::In);
        const auto outs = g->groupPorts(gid, ClipGraph::PortDirection::Out);
        QCOMPARE(ins.size(), 1);
        QCOMPARE(outs.size(), 1);
        QCOMPARE(ins[0].node, k.p1);
        QCOMPARE(ins[0].innerPort, 0);
        QCOMPARE(ins[0].port.index, 0);
        QCOMPARE(ins[0].port.kind, ClipGraph::PortKind::ChainIn);
        QVERIFY(ins[0].port.label.contains(g->node(k.p1)->title));
        QCOMPARE(ins[0].port.pos.x(), 0.0);
        QCOMPARE(outs[0].node, k.p3);
        QCOMPARE(outs[0].innerPort, 0);
        QCOMPARE(outs[0].port.kind, ClipGraph::PortKind::ChainOut);
        QCOMPARE(outs[0].port.pos.x(), g->groupRect(gid).width());

        quint64 proxyGroup = 0;
        int proxy = -1;
        QVERIFY(g->groupProxyFor(k.p1, ClipGraph::PortDirection::In, 0, &proxyGroup, &proxy));
        QCOMPARE(proxyGroup, gid);
        QCOMPARE(proxy, 0);
        QVERIFY(g->groupProxyFor(k.p3, ClipGraph::PortDirection::Out, 0, &proxyGroup, &proxy));
        QVERIFY(!g->groupProxyFor(k.p2, ClipGraph::PortDirection::In, 0, &proxyGroup, &proxy));

        int hidden = 0;
        for (const ClipGraph::Edge &e : g->edges())
            if (g->edgeHiddenByGroup(e)) ++hidden;
        QCOMPARE(hidden, 2);

        QVERIFY(c.setGroupCollapsed(gid, false));
        QVERIFY(!g->groupProxyFor(k.p1, ClipGraph::PortDirection::In, 0, &proxyGroup, &proxy));
        for (const ClipGraph::Edge &e : g->edges())
            QVERIFY(!g->edgeHiddenByGroup(e));

        ClipGraphController lone;
        const quint64 x = lone.addNode(int(NT::Process), 0, 0, {{"effect", 4}});
        const quint64 y = lone.addNode(int(NT::Process), 200, 0, {{"effect", 4}});
        lone.select(ids({x, y}));
        const quint64 lg = lone.groupSelection();
        QVERIFY(lone.graph()->groupPorts(lg, ClipGraph::PortDirection::In).isEmpty());
        QVERIFY(lone.graph()->groupPorts(lg, ClipGraph::PortDirection::Out).isEmpty());
    }

    void connectingToGroupProxyConnectsInnerPort() {
        ClipGraphController c;
        const Chain k = buildChain(c);
        ClipGraph *g = c.graph();
        c.select(ids({k.p1, k.p2, k.p3}));
        const quint64 gid = c.groupSelection();
        const quint64 b = c.addNode(int(NT::Input), 0, 300, canvas("b"));
        const int edges = g->edges().size();

        QVERIFY(c.beginConnection(b, true, 0));
        QVERIFY(c.canConnect(gid, false, 0));
        QVERIFY(!c.canConnect(gid, false, 5));
        QVERIFY(c.finishConnection(gid, false, 0));
        QCOMPARE(g->producerForInput(k.p1, 0), b);
        QCOMPARE(g->edges().size(), edges);
        QCOMPARE(g->groupPorts(gid, ClipGraph::PortDirection::In).size(), 1);

        const quint64 q = c.addNode(int(NT::Process), 800, 300, {{"effect", 4}});
        QVERIFY(c.beginConnection(gid, true, 0));
        QVERIFY(c.finishConnection(q, false, 0));
        QCOMPARE(g->producerForInput(q, 0), k.p3);

        QVERIFY(!c.beginConnection(gid, true, 7));
        c.undo();
        QVERIFY(g->producerForInput(q, 0) != k.p3);
    }

    void copyAndPasteOfGroupCreatesNewGroup() {
        ClipGraphController c;
        const Chain k = buildChain(c);
        ClipGraph *g = c.graph();
        c.select(ids({k.p1, k.p2, k.p3}));
        const quint64 gid = c.groupSelection();
        const int edges = g->edges().size();

        QVERIFY(c.copySelection());
        const QVariantList pasted = c.paste(900, 400);
        QCOMPARE(pasted.size(), 3);
        QCOMPARE(g->groups().size(), 2);
        QCOMPARE(g->edges().size(), edges + 2);
        const quint64 ng = g->node(pasted[0].toULongLong())->group;
        QVERIFY(ng != 0);
        QVERIFY(ng != gid);
        for (const QVariant &v : pasted) {
            const quint64 id = v.toULongLong();
            QVERIFY(id != k.p1 && id != k.p2 && id != k.p3);
            QCOMPARE(g->node(id)->group, ng);
        }
        QVERIFY(g->group(ng)->collapsed);
        QCOMPARE(c.selectedGroup(), ng);
        QCOMPARE(g->groupMembers(gid).size(), 3);

        c.undo();
        QCOMPARE(g->groups().size(), 1);

        c.select(ids({k.p1, k.p2}));
        QVERIFY(c.copySelection());
        const QVariantList partial = c.paste(0, 800);
        QCOMPARE(partial.size(), 2);
        QCOMPARE(g->node(partial[0].toULongLong())->group, quint64(0));
        QCOMPARE(g->groups().size(), 1);
    }

    void deletingACollapsedGroupRemovesMembersAndGroup() {
        ClipGraphController c;
        const Chain k = buildChain(c);
        ClipGraph *g = c.graph();
        c.select(ids({k.p1, k.p2, k.p3}));
        const quint64 gid = c.groupSelection();
        c.clearHistory();

        c.removeNodes(c.selection());
        QVERIFY(!g->hasNode(k.p2));
        QVERIFY(g->groups().isEmpty());
        QCOMPARE(c.selectedGroup(), quint64(0));
        c.undo();
        QVERIFY(g->group(gid));
        QCOMPARE(g->groupMembers(gid).size(), 3);
        QVERIFY(!c.canUndo());
    }

    void snapshotListsGroups() {
        ClipGraphController c;
        const Chain k = buildChain(c);
        c.select(ids({k.p1, k.p2, k.p3}));
        const quint64 gid = c.groupSelection();
        const QJsonObject snap = c.graph()->snapshot();
        const QJsonArray groups = snap["groups"].toArray();
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups[0].toObject()["id"].toString(), QString::number(gid));
        QCOMPARE(groups[0].toObject()["collapsed"].toBool(), true);
        QCOMPARE(groups[0].toObject()["members"].toArray().size(), 3);
        int tagged = 0;
        for (const QJsonValue &v : snap["nodes"].toArray())
            if (v.toObject()["group"].toString() == QString::number(gid)) ++tagged;
        QCOMPARE(tagged, 3);
    }
};

QTEST_MAIN(TestClipGraph)
#include "test_clipgraph.moc"
