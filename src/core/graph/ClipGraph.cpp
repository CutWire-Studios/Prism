#include "core/graph/ClipGraph.h"

#include "core/audio/AudioRack.h"
#include "core/effects/ShaderEffects.h"
#include "core/media/AudioAnalyzer.h"
#include "core/project/ClipSettings.h"

#include <QColor>
#include <QFileInfo>
#include <QJsonArray>
#include <QTime>
#include <QUuid>

#include <algorithm>

using NT = ClipGraph::NodeType;
using PK = ClipGraph::PortKind;
using PD = ClipGraph::PortDirection;
using EK = ClipGraph::EdgeKind;

struct ClipGraph::ConnectPlan {
    EdgeKind kind = EdgeKind::Chain;
    bool insertMixer = false;
};

namespace {

constexpr qreal kCardW = 122.0;
constexpr qreal kCardBodyH = 105.0;
constexpr qreal kPortR = 4.5;
constexpr qreal kHeaderH = 16.0;
constexpr qreal kProxyY = kPortR + kHeaderH;
constexpr qreal kAudioStripH = 54.0;
constexpr qreal kSmallW = 100.0;
constexpr qreal kSmallH = 110.0;
constexpr qreal kProcW = 116.0;
constexpr qreal kProcH = 66.0;
constexpr qreal kProcExtraRow = 22.0;
constexpr qreal kLayerW = 158.0, kLayerHdr = 20.0, kLayerRow = 22.0, kLayerFoot = 26.0;
constexpr qreal kOutW = 116.0, kOutH = 78.0, kOutBodyH = 52.0;
constexpr qreal kMixW = 158.0, kMixHdr = 20.0, kMixRow = 22.0, kMixFoot = 26.0;
constexpr qreal kAoW = 168.0, kAoHdr = 22.0, kAoRow = 20.0;

int processExtraInputs(const ClipGraph::Node &n) {
    const ShaderEffectSpec *spec = ShaderEffects::byId(n.params.value("effect").toInt());
    return spec ? spec->extraInputs : 0;
}

bool isAudioStreamOutKind(PK kind) {
    return kind == PK::AudioControllerOut || kind == PK::MasterAudioInputOut
        || kind == PK::AudioEffectOut;
}
bool isAudioEffectInKind(PK kind) { return kind == PK::AudioEffectIn; }
bool isMixedAudioInKind(PK kind) { return kind == PK::MasterAudioIn; }
bool isMixedAudioOutKind(PK kind) { return kind == PK::AudioMixerOut; }
bool isAudioMixerInKind(PK kind) { return kind == PK::AudioMixerIn; }

QVariantMap defaultSlot(NT type) {
    switch (type) {
    case NT::Layer:
        return {{"baseX", 0.0}, {"baseY", 0.0}, {"baseW", 1.0}, {"baseH", 1.0},
                {"visible", true}, {"name", QString()}};
    case NT::AudioMixer:
        return {{"volume", 100}, {"muted", false}, {"name", QString()}};
    default:
        return {{"name", QString()}};
    }
}

QVariantMap canonSlot(NT type, const QJsonObject &so) {
    switch (type) {
    case NT::Layer:
        return {{"baseX", double(float(so["baseX"].toDouble(0.0)))},
                {"baseY", double(float(so["baseY"].toDouble(0.0)))},
                {"baseW", double(float(so["baseW"].toDouble(1.0)))},
                {"baseH", double(float(so["baseH"].toDouble(1.0)))},
                {"visible", so["visible"].toBool(true)},
                {"name", so["name"].toString()}};
    case NT::AudioMixer:
        return {{"volume", so["volume"].toInt(100)},
                {"muted", so["muted"].toBool(false)},
                {"name", so["name"].toString()}};
    default:
        return {{"name", so["name"].toString()}};
    }
}

QVariantMap defaultDevicePort() {
    return {{"deviceId", QStringLiteral("Main")}, {"deviceLabel", QStringLiteral("Main")}};
}

QVariantMap defaultParams(NT type) {
    switch (type) {
    case NT::Layer:
        return {{"canvasCustom", false}, {"canvasW", 1280}, {"canvasH", 720},
                {"slots", QVariantList{defaultSlot(NT::Layer)}}};
    case NT::AudioMixer:
        return {{"slots", QVariantList{defaultSlot(NT::AudioMixer)}}};
    case NT::MasterAudioOutput:
        return {{"devicePorts", QVariantList{defaultDevicePort()}},
                {"deviceId", QStringLiteral("Main")}, {"deviceLabel", QStringLiteral("Main")}};
    case NT::MasterAudioInput:
    case NT::MasterAudioCapture:
        return {{"deviceId", QString()}, {"deviceLabel", QString()},
                {"volume", 100}, {"muted", false}};
    case NT::Script:
        return {{"luaCode", QString()}, {"runOn", 0}, {"intervalMs", 1000},
                {"runTime", QStringLiteral("12:00:00")},
                {"scriptType", QStringLiteral("lua")}};
    case NT::AudioAnalyzer: {
        const AudioAnalyzerConfig c;
        return {{"scriptType", QStringLiteral("audio")},
                {"fftSize", c.fftSize}, {"binCount", c.binCount},
                {"smoothing", double(c.smoothing)}, {"beatSensitivity", double(c.beatSensitivity)},
                {"lowMidHz", double(c.lowMidHz)}, {"midHighHz", double(c.midHighHz)}};
    }
    default:
        return {};
    }
}

QJsonObject descriptorToJson(const SourceDescriptor &d) {
    QJsonObject o;
    o["kind"]               = (int)d.kind;
    o["path"]               = d.path;
    o["displayName"]        = d.displayName;
    o["color"]              = d.color.name(QColor::HexArgb);
    o["cameraIndex"]        = d.cameraIndex;
    o["screenIndex"]        = d.screenIndex;
    o["windowIndex"]        = d.windowIndex;
    o["captureId"]          = d.captureId;
    o["slideshowIntervalMs"]= d.slideshowIntervalMs;
    o["slideshowEffect"]    = d.slideshowEffect;
    o["slideshowTransitionMs"] = d.slideshowTransitionMs;
    o["canvasWidth"]        = d.canvasWidth;
    o["canvasHeight"]       = d.canvasHeight;
    o["canvasFill"]         = (int)d.canvasFill;
    o["shaderCode"]         = d.shaderCode;
    o["obsSceneName"]       = d.obsSceneName;
    o["textTemplate"]       = d.textTemplate;
    o["textStyle"]          = d.textStyleJson;
    o["shapeStyle"]         = d.shapeStyleJson;
    o["svgTemplateId"]      = d.svgTemplateId;
    o["svgParams"]          = d.svgParamsJson;
    o["sceneId"]            = d.sceneId;
    o["webrtcRelayUrl"]     = d.webrtcRelayUrl;
    return o;
}

SourceDescriptor descriptorFromJson(const QJsonObject &o) {
    SourceDescriptor d;
    d.kind               = (SourceDescriptor::Kind)o["kind"].toInt();
    d.path               = o["path"].toString();
    d.displayName        = o["displayName"].toString();
    d.color              = QColor(o["color"].toString("#ffffffff"));
    d.cameraIndex        = o["cameraIndex"].toInt();
    d.screenIndex        = o["screenIndex"].toInt();
    d.windowIndex        = o["windowIndex"].toInt();
    d.captureId          = o["captureId"].toString();
    if (d.captureId.isEmpty() &&
        (d.kind == SourceDescriptor::Kind::Screen ||
         d.kind == SourceDescriptor::Kind::Window)) {
        d.captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    d.slideshowIntervalMs= o["slideshowIntervalMs"].toInt(3000);
    d.slideshowEffect    = o["slideshowEffect"].toInt(0);
    d.slideshowTransitionMs = o["slideshowTransitionMs"].toInt(800);
    d.canvasWidth        = o["canvasWidth"].toInt(1280);
    d.canvasHeight       = o["canvasHeight"].toInt(720);
    d.canvasFill         = (SourceDescriptor::CanvasFill)o["canvasFill"].toInt();
    d.shaderCode         = o["shaderCode"].toString();
    d.obsSceneName       = o["obsSceneName"].toString();
    d.textTemplate       = o["textTemplate"].toString();
    d.textStyleJson      = o["textStyle"].toString();
    d.shapeStyleJson     = o["shapeStyle"].toString();
    d.svgTemplateId      = o["svgTemplateId"].toString();
    d.svgParamsJson      = o["svgParams"].toString();
    d.sceneId            = o["sceneId"].toString();
    d.webrtcRelayUrl     = o["webrtcRelayUrl"].toString();
    return d;
}

bool kindShowsDataIn(int kind) {
    using K = SourceDescriptor::Kind;
    return kind == int(K::Text) || kind == int(K::SvgTemplate) || kind == int(K::Shader);
}

QJsonObject toJsonObject(const QVariantMap &m) { return QJsonObject::fromVariantMap(m); }

QString slotLabel(const QVariantList &slotList, int i) {
    const QString name = slotList.value(i).toMap().value("name").toString();
    return name.isEmpty() ? QStringLiteral("in %1").arg(i + 1) : name;
}

QString sourceKindName(int kind) {
    using K = SourceDescriptor::Kind;
    switch (K(kind)) {
    case K::VideoFile: return QStringLiteral("video");
    case K::Image:     return QStringLiteral("image");
    case K::AudioFile: return QStringLiteral("audio");
    case K::Slideshow: return QStringLiteral("slideshow");
    case K::Camera:    return QStringLiteral("camera");
    case K::Screen:    return QStringLiteral("screen");
    case K::Canvas:    return QStringLiteral("canvas");
    case K::Window:    return QStringLiteral("window");
    case K::Shader:    return QStringLiteral("shader");
    case K::Ndi:       return QStringLiteral("ndi");
    case K::WebRtc:    return QStringLiteral("webrtc");
    case K::Text:      return QStringLiteral("text");
    case K::Shape:     return QStringLiteral("shape");
    case K::SvgTemplate: return QStringLiteral("svg_template");
    case K::Scene:     return QStringLiteral("scene");
    default: break;
    }
    return QStringLiteral("unknown");
}

}  // namespace

// ── Static validation tables ────────────────────────────────────────────────

bool ClipGraph::portsCompatible(PortKind a, PortKind b) {
    if (a == b) return false;
    if ((a == PK::ChainOut && b == PK::ChainIn) || (a == PK::ChainIn && b == PK::ChainOut)) return true;
    if ((isAudioStreamOutKind(a) && isAudioMixerInKind(b)) ||
        (isAudioMixerInKind(a) && isAudioStreamOutKind(b))) return true;
    if ((isAudioStreamOutKind(a) && isAudioEffectInKind(b)) ||
        (isAudioEffectInKind(a) && isAudioStreamOutKind(b))) return true;
    if ((a == PK::AudioEffectOut && b == PK::AudioEffectIn) ||
        (a == PK::AudioEffectIn && b == PK::AudioEffectOut)) return true;
    if ((a == PK::AudioEffectOut && isAudioMixerInKind(b)) ||
        (isAudioMixerInKind(a) && b == PK::AudioEffectOut)) return true;
    if ((a == PK::AudioEffectOut && isMixedAudioInKind(b)) ||
        (isMixedAudioInKind(a) && b == PK::AudioEffectOut)) return true;
    if ((isMixedAudioOutKind(a) && isMixedAudioInKind(b)) ||
        (isMixedAudioInKind(a) && isMixedAudioOutKind(b))) return true;
    if ((isAudioStreamOutKind(a) && isMixedAudioInKind(b)) ||
        (isMixedAudioInKind(a) && isAudioStreamOutKind(b))) return true;
    if ((a == PK::AudioControllerOut && b == PK::ShaderAudioIn) ||
        (a == PK::ShaderAudioIn && b == PK::AudioControllerOut)) return true;
    if (((a == PK::AudioControllerOut || isAudioStreamOutKind(a)) && b == PK::AudioIn) ||
        (a == PK::AudioIn && (b == PK::AudioControllerOut || isAudioStreamOutKind(b)))) return true;
    if ((a == PK::ScriptOut && b == PK::DataIn) || (a == PK::DataIn && b == PK::ScriptOut)) return true;
    return false;
}

bool ClipGraph::isSingleConnection(PortKind kind) {
    if (isAudioStreamOutKind(kind)) return false;
    if (isAudioEffectInKind(kind)) return true;
    if (isAudioMixerInKind(kind)) return true;
    if (isMixedAudioOutKind(kind)) return true;
    if (isMixedAudioInKind(kind)) return true;
    if (kind == PK::ScriptOut) return false;
    if (kind == PK::DataIn) return false;
    return true;
}

ClipGraph::PortDirection ClipGraph::directionOf(PortKind kind) {
    switch (kind) {
    case PK::ChainIn:
    case PK::ShaderAudioIn:
    case PK::AudioIn:
    case PK::MasterAudioIn:
    case PK::DataIn:
    case PK::AudioMixerIn:
    case PK::AudioEffectIn:
        return PD::In;
    default:
        return PD::Out;
    }
}

QString ClipGraph::edgeKindName(int kind) {
    switch (EK(kind)) {
    case EK::Chain:              return QStringLiteral("chain");
    case EK::ScriptToData:       return QStringLiteral("script_to_data");
    case EK::ClipToShaderAudio:  return QStringLiteral("clip_to_shader_audio");
    case EK::ClipToAudioAnalyzer:  return QStringLiteral("clip_to_audio_analyzer");
    case EK::InputToMaster:      return QStringLiteral("input_to_master");
    case EK::StreamToMixer:      return QStringLiteral("stream_to_mixer");
    case EK::MixerToOutput:      return QStringLiteral("mixer_to_output");
    case EK::AudioEffectChain:   return QStringLiteral("audio_effect");
    default:                     return QStringLiteral("kind_%1").arg(kind);
    }
}

// ── Access ──────────────────────────────────────────────────────────────────

ClipGraph::ClipGraph(QObject *parent) : QObject(parent) {}

const ClipGraph::Node *ClipGraph::node(NodeId id) const {
    const auto it = m_nodes.constFind(id);
    return it == m_nodes.constEnd() ? nullptr : &it.value();
}

ClipGraph::Node *ClipGraph::mutableNode(NodeId id) {
    const auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : &it.value();
}

QVector<NodeId> ClipGraph::nodeIdsOfType(NodeType type) const {
    QVector<NodeId> out;
    for (const Node &n : m_nodes)
        if (n.type == type) out.append(n.id);
    return out;
}

const ClipGraph::Edge *ClipGraph::edge(EdgeId id) const {
    for (const Edge &e : m_edges)
        if (e.id == id) return &e;
    return nullptr;
}

bool ClipGraph::isEmptyGraph() const {
    return m_nodes.size() - (m_outputNode != 0 ? 1 : 0) == 0;
}

QString ClipGraph::sourceName(NodeId inputNodeId) const {
    const Node *n = node(inputNodeId);
    if (!n || n->type != NT::Input) return {};
    return n->params.value("source").toMap().value("displayName").toString();
}

SourceDescriptor ClipGraph::sourceDescriptor(NodeId inputNodeId) const {
    const Node *n = node(inputNodeId);
    if (!n || n->type != NT::Input) return {};
    return descriptorFromJson(QJsonObject::fromVariantMap(n->params.value("source").toMap()));
}

ClipSettings ClipGraph::clipSettings(NodeId inputNodeId) const {
    const Node *n = node(inputNodeId);
    if (!n || n->type != NT::Input) return {};
    return ClipSettings::fromJson(QJsonObject::fromVariantMap(n->params.value("settings").toMap()));
}

bool ClipGraph::clipRepeat(NodeId inputNodeId) const {
    const Node *n = node(inputNodeId);
    return n && n->type == NT::Input && n->params.value("repeat").toBool();
}

bool ClipGraph::setSourceDescriptor(NodeId inputNodeId, const SourceDescriptor &desc) {
    const Node *n = node(inputNodeId);
    if (!n || n->type != NT::Input) return false;
    return setParams(inputNodeId, {{"source", sourceToMap(desc)}});
}

QVariantMap ClipGraph::sourceToMap(const SourceDescriptor &desc) {
    return descriptorToJson(desc).toVariantMap();
}

bool ClipGraph::setClipSettings(NodeId inputNodeId, const ClipSettings &settings) {
    const Node *n = node(inputNodeId);
    if (!n || n->type != NT::Input) return false;
    return setParams(inputNodeId, {{"settings", settings.toJson().toVariantMap()}});
}

int ClipGraph::slotCount(const Node &n) const {
    return int(n.params.value("slots").toList().size());
}

QSizeF ClipGraph::nodeSize(const Node &n) const {
    switch (n.type) {
    case NT::Input: {
        const bool hasAudio = n.params.value("hasAudio").toBool();
        if (n.params.value("audioOnly").toBool())
            return {kCardW, kProxyY + kAudioStripH + kPortR + kPortR};
        return {kCardW, kProxyY + kCardBodyH + (hasAudio ? kAudioStripH : 0.0) + kPortR + kPortR};
    }
    case NT::Process:
        return {kProcW, kProcH + processExtraInputs(n) * kProcExtraRow};
    case NT::AudioEffect:
        return {kProcW, kProcH};
    case NT::Layer:
        return {kLayerW, kLayerHdr + slotCount(n) * kLayerRow + kLayerFoot};
    case NT::Output:
        return {kOutW, kOutH};
    case NT::AudioMixer:
        return {kMixW, kMixHdr + slotCount(n) * kMixRow + kMixFoot};
    case NT::MasterAudioOutput:
        return {kAoW, kAoHdr + n.params.value("devicePorts").toList().size() * kAoRow + 4.0};
    default:
        return {kSmallW, kSmallH};
    }
}

void ClipGraph::buildPorts(const Node &n, QVector<Port> &in, QVector<Port> &out) const {
    const QSizeF sz = nodeSize(n);
    auto add = [](QVector<Port> &v, PortKind k, PortDirection d, const QString &label,
                  bool slotted, const QPointF &pos) {
        Port p;
        p.kind = k;
        p.direction = d;
        p.index = int(v.size());
        p.label = label;
        p.slotted = slotted;
        p.pos = pos;
        v.append(p);
    };

    switch (n.type) {
    case NT::Input: {
        const int kind = n.params.value("source").toMap().value("kind").toInt();
        const bool audioOnly = n.params.value("audioOnly").toBool();
        const bool hasAudio = n.params.value("hasAudio").toBool();
        const qreal midY = sz.height() / 2.0;
        const qreal cardMidY = kProxyY + kCardBodyH / 2.0;
        const bool shader = kind == int(SourceDescriptor::Kind::Shader);
        if (!audioOnly) {
            const qreal y = shader ? cardMidY : midY - (hasAudio ? 14.0 : 0.0);
            add(out, PK::ChainOut, PD::Out, QStringLiteral("video"), false, {sz.width(), y});
        }
        if (hasAudio)
            add(out, PK::AudioControllerOut, PD::Out, QStringLiteral("audio"), false,
                {sz.width(), midY + (audioOnly ? 0.0 : 14.0)});
        if (kindShowsDataIn(kind))
            add(in, PK::DataIn, PD::In, QStringLiteral("data"), false, {0.0, cardMidY});
        break;
    }
    case NT::Process: {
        add(in, PK::ChainIn, PD::In, QStringLiteral("in"), true, {0.0, kProcH / 2});
        if (const ShaderEffectSpec *spec = ShaderEffects::byId(n.params.value("effect").toInt())) {
            for (int i = 0; i < spec->extraInputs; ++i)
                add(in, PK::ChainIn, PD::In, spec->inputName(i), true,
                    {0.0, kProcH + i * kProcExtraRow + kProcExtraRow / 2});
        }
        add(out, PK::ChainOut, PD::Out, QStringLiteral("out"), false, {kProcW, kProcH / 2});
        break;
    }
    case NT::AudioEffect:
        add(in, PK::AudioEffectIn, PD::In, QStringLiteral("in"), true, {0.0, kProcH / 2});
        add(out, PK::AudioEffectOut, PD::Out, QStringLiteral("out"), false, {kProcW, kProcH / 2});
        break;
    case NT::Layer: {
        const QVariantList slotList = n.params.value("slots").toList();
        for (int i = 0; i < slotList.size(); ++i)
            add(in, PK::ChainIn, PD::In, slotLabel(slotList, i), true,
                {0.0, kLayerHdr + i * kLayerRow + kLayerRow / 2});
        add(out, PK::ChainOut, PD::Out, QStringLiteral("out"), false, {kLayerW, sz.height() / 2.0});
        break;
    }
    case NT::Output:
        add(in, PK::ChainIn, PD::In, QStringLiteral("video"), true, {0.0, kOutBodyH / 2});
        break;
    case NT::AudioMixer: {
        const QVariantList slotList = n.params.value("slots").toList();
        for (int i = 0; i < slotList.size(); ++i)
            add(in, PK::AudioMixerIn, PD::In, slotLabel(slotList, i), true,
                {0.0, kMixHdr + i * kMixRow + kMixRow / 2});
        add(out, PK::AudioMixerOut, PD::Out, QStringLiteral("out"), false, {kMixW, sz.height() / 2.0});
        break;
    }
    case NT::MasterAudioOutput: {
        const QVariantList ports = n.params.value("devicePorts").toList();
        for (int i = 0; i < ports.size(); ++i) {
            const QString label = ports[i].toMap().value("deviceLabel").toString();
            add(in, PK::MasterAudioIn, PD::In, label.isEmpty() ? QStringLiteral("Default") : label,
                true, {0.0, kAoHdr + i * kAoRow + kAoRow / 2});
        }
        break;
    }
    case NT::MasterAudioInput:
    case NT::MasterAudioCapture:
        add(out, PK::MasterAudioInputOut, PD::Out, QStringLiteral("audio"), false,
            {kSmallW, kSmallH / 2});
        break;
    case NT::Script:
        add(in, PK::DataIn, PD::In, QStringLiteral("data"), false, {0.0, kSmallH / 2});
        add(out, PK::ScriptOut, PD::Out, QStringLiteral("out"), false, {kSmallW, kSmallH / 2});
        break;
    case NT::AudioAnalyzer:
        add(in, PK::AudioIn, PD::In, QStringLiteral("audio"), false, {0.0, kSmallH / 2});
        add(out, PK::ScriptOut, PD::Out, QStringLiteral("out"), false, {kSmallW, kSmallH / 2});
        break;
    }
}

QVector<ClipGraph::Port> ClipGraph::inputPorts(NodeId id) const {
    QVector<Port> in, out;
    if (const Node *n = node(id)) buildPorts(*n, in, out);
    return in;
}

QVector<ClipGraph::Port> ClipGraph::outputPorts(NodeId id) const {
    QVector<Port> in, out;
    if (const Node *n = node(id)) buildPorts(*n, in, out);
    return out;
}

bool ClipGraph::isPortConnected(NodeId id, PortDirection dir, int index) const {
    for (const Edge &e : m_edges) {
        if (dir == PD::Out && e.from == id && e.fromPort == index) return true;
        if (dir == PD::In && e.to == id && e.toPort == index) return true;
    }
    return false;
}

QPointF ClipGraph::portScenePos(NodeId id, PortDirection dir, int index) const {
    const Node *n = node(id);
    if (!n) return {};
    QVector<Port> in, out;
    buildPorts(*n, in, out);
    const QVector<Port> &list = dir == PD::In ? in : out;
    if (index < 0 || index >= list.size()) return n->pos;
    return n->pos + list[index].pos;
}

int ClipGraph::findPortIndex(NodeId id, PortKind kind, int slot) const {
    const Node *n = node(id);
    if (!n) return -1;
    QVector<Port> in, out;
    buildPorts(*n, in, out);
    const QVector<Port> &list = directionOf(kind) == PD::In ? in : out;
    QVector<int> matches;
    for (const Port &p : list)
        if (p.kind == kind) matches.append(p.index);
    if (matches.isEmpty()) return -1;

    const bool slotKind = (n->type == NT::Layer && kind == PK::ChainIn)
        || (n->type == NT::AudioMixer && kind == PK::AudioMixerIn);
    if (slotKind)
        return (slot >= 0 && slot < matches.size()) ? matches[slot] : -1;
    if (n->type == NT::Process && kind == PK::ChainIn) {
        const int s = slot >= 0 ? slot : 0;
        return s < matches.size() ? matches[s] : -1;
    }
    if (n->type == NT::MasterAudioOutput && kind == PK::MasterAudioIn) {
        const int s = slot >= 0 ? slot : 0;
        return s < matches.size() ? matches[s] : -1;
    }
    return matches.first();
}

bool ClipGraph::lookupConnectionPorts(NodeId from, NodeId to, int kind, int slot,
                                      int *fromPort, int *toPort) const {
    *fromPort = -1;
    *toPort = -1;
    auto fromFirst = [&](std::initializer_list<PK> kinds) {
        for (PK k : kinds) {
            const int i = findPortIndex(from, k, -1);
            if (i >= 0) return i;
        }
        return -1;
    };

    switch (EK(kind)) {
    case EK::Chain:
        *fromPort = findPortIndex(from, PK::ChainOut, -1);
        *toPort = findPortIndex(to, PK::ChainIn, slot);
        break;
    case EK::ClipToShaderAudio:
        *fromPort = findPortIndex(from, PK::AudioControllerOut, -1);
        *toPort = findPortIndex(to, PK::ShaderAudioIn, -1);
        break;
    case EK::ClipToAudioAnalyzer:
        *fromPort = fromFirst({PK::AudioControllerOut, PK::MasterAudioInputOut});
        *toPort = findPortIndex(to, PK::AudioIn, -1);
        break;
    case EK::InputToMaster:
        *fromPort = fromFirst({PK::MasterAudioInputOut, PK::AudioEffectOut});
        *toPort = findPortIndex(to, PK::MasterAudioIn, slot >= 0 ? slot : 0);
        break;
    case EK::StreamToMixer:
        *fromPort = fromFirst({PK::AudioControllerOut, PK::MasterAudioInputOut, PK::AudioEffectOut});
        *toPort = findPortIndex(to, PK::AudioMixerIn, slot);
        break;
    case EK::AudioEffectChain:
        *fromPort = fromFirst({PK::AudioControllerOut, PK::MasterAudioInputOut, PK::AudioEffectOut});
        *toPort = findPortIndex(to, PK::AudioEffectIn, -1);
        break;
    case EK::MixerToOutput:
        *fromPort = findPortIndex(from, PK::AudioMixerOut, -1);
        *toPort = findPortIndex(to, PK::MasterAudioIn, slot >= 0 ? slot : 0);
        break;
    case EK::ScriptToData:
        *fromPort = findPortIndex(from, PK::ScriptOut, -1);
        *toPort = findPortIndex(to, PK::DataIn, -1);
        break;
    default:
        break;
    }
    return *fromPort >= 0 && *toPort >= 0;
}

// ── Node lifecycle ──────────────────────────────────────────────────────────

void ClipGraph::notifyNode(NodeId id) {
    if (!m_restoring) emit nodeChanged(id);
}

void ClipGraph::notifyChanged() {
    if (!m_restoring) emit changed();
}

void ClipGraph::refreshNode(Node &n) {
    switch (n.type) {
    case NT::Input: {
        const QString name = n.params.value("source").toMap().value("displayName").toString();
        n.title = name.isEmpty() ? QStringLiteral("Input") : name;
        break;
    }
    case NT::Process: {
        const ProcessEffectDescriptor *d = ProcessEffects::byId(n.params.value("effect").toInt());
        n.title = d ? d->name : QStringLiteral("Process");
        break;
    }
    case NT::AudioEffect: {
        const AudioEffectDescriptor *d = AudioEffects::byId(n.params.value("effect").toInt());
        n.title = d ? d->name : QStringLiteral("Audio Effect");
        break;
    }
    case NT::Layer:             n.title = QStringLiteral("Layer"); break;
    case NT::Output:            n.title = QStringLiteral("Output"); break;
    case NT::AudioMixer:        n.title = QStringLiteral("Audio Mixer"); break;
    case NT::MasterAudioOutput: n.title = QStringLiteral("Audio Output"); break;
    case NT::MasterAudioInput:  n.title = QStringLiteral("Mic Input"); break;
    case NT::MasterAudioCapture: n.title = QStringLiteral("Audio Capture"); break;
    case NT::Script:            n.title = QStringLiteral("Lua Script"); break;
    case NT::AudioAnalyzer:       n.title = QStringLiteral("Audio Analyzer"); break;
    }
    if (n.type != NT::Input) {
        const QString custom = n.params.value("name").toString();
        if (!custom.isEmpty()) n.title = custom;
    }
    n.size = nodeSize(n);
}

QPointF ClipGraph::autoPosition(NodeType type) const {
    const int idx = int(nodeIdsOfType(type).size());
    switch (type) {
    case NT::Process:  return {220.0 + idx * 16.0, 48.0 + idx * 78.0};
    case NT::Layer:    return {280.0 + idx * 16.0, 48.0 + idx * 78.0};
    default:           return {idx * 160.0 + 20.0, idx * 60.0 + 20.0};
    }
}

NodeId ClipGraph::createNode(NodeType type, const QPointF &posIn, const QVariantMap &params) {
    Node n;
    n.id = allocateId();
    n.type = type;
    n.pos = (qIsNaN(posIn.x()) || qIsNaN(posIn.y())) ? autoPosition(type) : posIn;
    n.params = params;
    refreshNode(n);
    m_nodes.insert(n.id, n);
    m_order.append(n.id);
    if (type == NT::Output) m_outputNode = n.id;
    if (!m_restoring) emit nodeAdded(n.id);
    return n.id;
}

void ClipGraph::ensureOutputNode() {
    if (m_outputNode != 0) return;
    createNode(NT::Output, QPointF(360, 40), {});
}

NodeId ClipGraph::addInputNode(const SourceDescriptor &descIn, const QPointF &pos,
                               bool hasAudio, bool audioOnly) {
    SourceDescriptor desc = descIn;
    using K = SourceDescriptor::Kind;
    if (desc.captureId.isEmpty() && (desc.kind == K::Screen || desc.kind == K::Window))
        desc.captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (desc.kind == K::AudioFile) audioOnly = true;
    hasAudio = hasAudio || audioOnly;

    QVariantMap p;
    p["hasAudio"] = hasAudio;
    p["audioOnly"] = audioOnly;
    if (hasAudio) {
        p["audioVolume"] = 100;
        p["audioMuted"] = false;
        p["audioPlaybackMode"] = 0;
        p["audioDelayMs"] = 0;
    }
    p["source"] = descriptorToJson(desc).toVariantMap();
    p["settings"] = ClipSettings{}.toJson().toVariantMap();
    p["repeat"] = false;

    const NodeId id = createNode(NT::Input, pos, p);
    ensureOutputNode();
    notifyChanged();
    return id;
}

NodeId ClipGraph::addNode(NodeType type, const QPointF &pos, const QVariantMap &params) {
    switch (type) {
    case NT::Input: {
        const QVariantMap src = params.value("source").toMap();
        return addInputNode(descriptorFromJson(QJsonObject::fromVariantMap(src)), pos,
                            params.value("hasAudio").toBool(), params.value("audioOnly").toBool());
    }
    case NT::Output:
        if (m_outputNode != 0) return 0;
        break;
    case NT::MasterAudioOutput:
        if (!nodeIdsOfType(NT::MasterAudioOutput).isEmpty()) return 0;
        break;
    default:
        break;
    }

    QVariantMap p = defaultParams(type);
    if (type == NT::Process || type == NT::AudioEffect) {
        const int effect = params.value("effect").toInt();
        QJsonObject effectParams;
        if (type == NT::Process) {
            const ProcessEffectDescriptor *d = ProcessEffects::byId(effect);
            if (!d) return 0;
            effectParams = d->defaultParams;
        } else {
            const AudioEffectDescriptor *d = AudioEffects::byId(effect);
            if (!d) return 0;
            effectParams = d->defaultParams;
        }
        const QJsonObject extra = QJsonObject::fromVariantMap(params.value("params").toMap());
        for (auto it = extra.begin(); it != extra.end(); ++it)
            effectParams.insert(it.key(), it.value());
        p["effect"] = effect;
        p["params"] = effectParams.toVariantMap();
    } else {
        for (auto it = params.begin(); it != params.end(); ++it)
            p.insert(it.key(), it.value());
    }

    const NodeId id = createNode(type, pos, p);
    switch (type) {
    case NT::Process:
    case NT::AudioEffect:
    case NT::Layer:
    case NT::AudioMixer:
        ensureOutputNode();
        break;
    default:
        break;
    }
    notifyChanged();
    return id;
}

bool ClipGraph::moveNode(NodeId id, const QPointF &pos) {
    Node *n = mutableNode(id);
    if (!n) return false;
    if (n->pos == pos) return true;
    n->pos = pos;
    notifyNode(id);
    return true;
}

bool ClipGraph::setParams(NodeId id, const QVariantMap &params, bool merge) {
    Node *n = mutableNode(id);
    if (!n) return false;
    const QVariantList oldDevices = n->params.value("devicePorts").toList();
    if (merge) {
        for (auto it = params.begin(); it != params.end(); ++it)
            n->params.insert(it.key(), it.value());
    } else {
        n->params = params;
    }
    if (n->type == NT::MasterAudioOutput && params.contains("devicePorts")) {
        QVariantList ports = n->params.value("devicePorts").toList();
        if (ports.isEmpty()) {
            ports.append(defaultDevicePort());
            n->params["devicePorts"] = ports;
        }
        const QVariantMap first = ports.first().toMap();
        n->params["deviceId"] = first.value("deviceId");
        n->params["deviceLabel"] = first.value("deviceLabel");
        remapDevicePorts(*n, oldDevices);
    }
    refreshNode(*n);
    notifyNode(id);
    notifyChanged();
    return true;
}

bool ClipGraph::setEffectParams(NodeId id, const QVariantMap &params, bool merge) {
    const Node *n = node(id);
    if (!n || (n->type != NT::Process && n->type != NT::AudioEffect)) return false;
    QVariantMap next = merge ? n->params.value("params").toMap() : QVariantMap{};
    for (auto it = params.begin(); it != params.end(); ++it)
        next.insert(it.key(), it.value());
    return setParams(id, {{"params", next}}, true);
}

void ClipGraph::remapDevicePorts(Node &n, const QVariantList &oldPorts) {
    const QVariantList fresh = n.params.value("devicePorts").toList();
    for (int i = int(m_edges.size()) - 1; i >= 0; --i) {
        Edge &e = m_edges[i];
        if (e.to != n.id) continue;
        const QString deviceId = oldPorts.value(e.toPort).toMap().value("deviceId").toString();
        int found = -1;
        for (int k = 0; k < fresh.size(); ++k) {
            if (fresh[k].toMap().value("deviceId").toString() == deviceId) { found = k; break; }
        }
        if (found < 0) {
            removeEdgeAt(i);
        } else if (found != e.toPort) {
            e.toPort = found;
            if (!m_restoring) emit edgeChanged(e.id);
        }
    }
}

bool ClipGraph::removeNode(NodeId id) {
    if (!m_nodes.contains(id)) return false;
    if (id == m_outputNode && m_nodes.size() > 1) return false;

    removeEdgesOfNode(id);
    m_nodes.remove(id);
    m_order.removeOne(id);
    if (id == m_outputNode) m_outputNode = 0;
    emit nodeRemoved(id);
    afterConnectionsChanged();
    return true;
}

void ClipGraph::clear() {
    m_nodes.clear();
    m_order.clear();
    m_edges.clear();
    m_outputNode = 0;
    emit graphReset();
    emit changed();
}

// ── Edges ───────────────────────────────────────────────────────────────────

EdgeId ClipGraph::appendEdge(NodeId from, int fromPort, NodeId to, int toPort, EdgeKind kind) {
    Edge e;
    e.id = m_nextEdgeId++;
    e.from = from;
    e.fromPort = fromPort;
    e.to = to;
    e.toPort = toPort;
    e.kind = kind;
    m_edges.append(e);
    if (!m_restoring) emit edgeAdded(e.id);
    return e.id;
}

void ClipGraph::removeEdgeAt(int i) {
    const EdgeId id = m_edges[i].id;
    m_edges.removeAt(i);
    if (!m_restoring) emit edgeRemoved(id);
}

void ClipGraph::disconnectPort(NodeId node, PortDirection dir, int index) {
    for (int i = int(m_edges.size()) - 1; i >= 0; --i) {
        const Edge &e = m_edges[i];
        if ((dir == PD::Out && e.from == node && e.fromPort == index)
            || (dir == PD::In && e.to == node && e.toPort == index))
            removeEdgeAt(i);
    }
}

void ClipGraph::removeEdgesOfNode(NodeId id) {
    for (int i = int(m_edges.size()) - 1; i >= 0; --i)
        if (m_edges[i].from == id || m_edges[i].to == id)
            removeEdgeAt(i);
}

bool ClipGraph::connectPortsRaw(NodeId from, int fromPort, NodeId to, int toPort, EdgeKind kind) {
    const QVector<Port> outs = outputPorts(from);
    const QVector<Port> ins = inputPorts(to);
    if (fromPort < 0 || fromPort >= outs.size() || toPort < 0 || toPort >= ins.size()) return false;
    if (isSingleConnection(outs[fromPort].kind)) disconnectPort(from, PD::Out, fromPort);
    if (isSingleConnection(ins[toPort].kind)) disconnectPort(to, PD::In, toPort);
    appendEdge(from, fromPort, to, toPort, kind);
    return true;
}

void ClipGraph::afterConnectionsChanged() {
    if (!m_restoring) {
        normalizeSwitchingInputs();
        normalizeMixerInputs();
    }
    notifyChanged();
}

bool ClipGraph::connectNodes(NodeId from, NodeId to, int kind, int toPortIndex) {
    if (from == 0 || to == 0 || from == to || !hasNode(from) || !hasNode(to)) return false;

    static const EK kTryOrder[] = {
        EK::Chain, EK::ScriptToData, EK::ClipToShaderAudio, EK::AudioEffectChain,
        EK::StreamToMixer, EK::MixerToOutput, EK::InputToMaster,
        EK::ClipToAudioAnalyzer,
    };
    QVector<int> kinds;
    if (kind >= 0) kinds.append(kind);
    else for (EK k : kTryOrder) kinds.append(int(k));

    for (int k : kinds) {
        int fp = -1, tp = -1;
        if (!lookupConnectionPorts(from, to, k, toPortIndex, &fp, &tp)) continue;
        if (!connectPortsRaw(from, fp, to, tp, EK(k))) return false;
        afterConnectionsChanged();
        return true;
    }
    return false;
}

bool ClipGraph::canConnectNodes(NodeId from, NodeId to, int kind, int toPortIndex) const {
    if (from == 0 || to == 0 || from == to || !hasNode(from) || !hasNode(to)) return false;
    static const EK kTryOrder[] = {
        EK::Chain, EK::ScriptToData, EK::ClipToShaderAudio, EK::AudioEffectChain,
        EK::StreamToMixer, EK::MixerToOutput, EK::InputToMaster,
        EK::ClipToAudioAnalyzer,
    };
    QVector<int> kinds;
    if (kind >= 0) kinds.append(kind);
    else for (EK k : kTryOrder) kinds.append(int(k));
    for (int k : kinds) {
        int fp = -1, tp = -1;
        if (!lookupConnectionPorts(from, to, k, toPortIndex, &fp, &tp)) continue;
        return true;
    }
    return false;
}

std::optional<ClipGraph::ConnectPlan> ClipGraph::planConnection(NodeId from, int fromPort,
                                                                NodeId to, int toPort) const {
    const Node *fn = node(from);
    const Node *tn = node(to);
    if (!fn || !tn || from == to) return std::nullopt;
    const QVector<Port> outs = outputPorts(from);
    const QVector<Port> ins = inputPorts(to);
    if (fromPort < 0 || fromPort >= outs.size() || toPort < 0 || toPort >= ins.size())
        return std::nullopt;

    const PK a = outs[fromPort].kind;
    const PK b = ins[toPort].kind;
    if (!portsCompatible(a, b)) return std::nullopt;

    ConnectPlan plan;
    if (a == PK::ChainOut && b == PK::ChainIn) plan.kind = EK::Chain;
    else if (a == PK::AudioControllerOut && b == PK::ShaderAudioIn) plan.kind = EK::ClipToShaderAudio;
    else if (isAudioStreamOutKind(a) && b == PK::AudioIn) plan.kind = EK::ClipToAudioAnalyzer;
    else if (isAudioStreamOutKind(a) && isMixedAudioInKind(b)) plan.insertMixer = true;
    else if (isAudioStreamOutKind(a) && isAudioMixerInKind(b)) plan.kind = EK::StreamToMixer;
    else if (isMixedAudioOutKind(a) && isMixedAudioInKind(b)) plan.kind = EK::MixerToOutput;
    else if (isAudioStreamOutKind(a) && isAudioEffectInKind(b)) plan.kind = EK::AudioEffectChain;
    else if (a == PK::ScriptOut && b == PK::DataIn) plan.kind = EK::ScriptToData;
    else return std::nullopt;

    if (plan.insertMixer) return plan;

    if (plan.kind == EK::Chain && tn->type == NT::Process
        && fn->type != NT::Input && fn->type != NT::Process
        && !(fn->type == NT::Layer && toPort > 0))
        return std::nullopt;
    return plan;
}

bool ClipGraph::canConnectPorts(NodeId from, int fromPort, NodeId to, int toPort) const {
    return planConnection(from, fromPort, to, toPort).has_value();
}

bool ClipGraph::connectPorts(NodeId from, int fromPort, NodeId to, int toPort) {
    const auto plan = planConnection(from, fromPort, to, toPort);
    if (!plan) return false;
    if (plan->insertMixer)
        insertMixerForStreamToOutput(from, fromPort, to, toPort);
    else if (!connectPortsRaw(from, fromPort, to, toPort, plan->kind))
        return false;
    afterConnectionsChanged();
    return true;
}

bool ClipGraph::disconnectNodes(NodeId from, NodeId to, int toPortIndex) {
    bool any = false;
    for (int i = int(m_edges.size()) - 1; i >= 0; --i) {
        const Edge &e = m_edges[i];
        if (e.from != from || e.to != to) continue;
        if (toPortIndex >= 0) {
            const QVector<Port> ins = inputPorts(to);
            const int slot = (e.toPort >= 0 && e.toPort < ins.size() && ins[e.toPort].slotted)
                ? e.toPort : -1;
            if (slot != toPortIndex) continue;
        }
        removeEdgeAt(i);
        any = true;
    }
    if (any) afterConnectionsChanged();
    return any;
}

bool ClipGraph::disconnectEdge(EdgeId id) {
    for (int i = 0; i < m_edges.size(); ++i) {
        if (m_edges[i].id == id) {
            removeEdgeAt(i);
            afterConnectionsChanged();
            return true;
        }
    }
    return false;
}

struct ClipGraph::InsertPlan {
    int in = 0;
    int out = 0;
    EdgeKind inKind = EdgeKind::Chain;
    EdgeKind outKind = EdgeKind::Chain;
};

std::optional<ClipGraph::InsertPlan> ClipGraph::planInsert(NodeId nodeId, const Edge &e) const {
    if (!hasNode(nodeId) || e.from == nodeId || e.to == nodeId) return std::nullopt;
    const QVector<Port> ins = inputPorts(nodeId);
    const QVector<Port> outs = outputPorts(nodeId);
    for (const Port &in : ins) {
        if (isPortConnected(nodeId, PD::In, in.index)) continue;
        const auto first = planConnection(e.from, e.fromPort, nodeId, in.index);
        if (!first || first->insertMixer) continue;
        for (const Port &out : outs) {
            if (isPortConnected(nodeId, PD::Out, out.index)) continue;
            const auto second = planConnection(nodeId, out.index, e.to, e.toPort);
            if (!second || second->insertMixer) continue;
            return InsertPlan{in.index, out.index, first->kind, second->kind};
        }
    }
    return std::nullopt;
}

bool ClipGraph::canInsertIntoEdge(NodeId nodeId, EdgeId edgeId) const {
    const Edge *e = edge(edgeId);
    return e && planInsert(nodeId, *e).has_value();
}

bool ClipGraph::insertIntoEdge(NodeId nodeId, EdgeId edgeId) {
    for (int i = 0; i < m_edges.size(); ++i) {
        if (m_edges[i].id != edgeId) continue;
        const Edge e = m_edges[i];
        const auto plan = planInsert(nodeId, e);
        if (!plan) return false;
        removeEdgeAt(i);
        connectPortsRaw(e.from, e.fromPort, nodeId, plan->in, plan->inKind);
        connectPortsRaw(nodeId, plan->out, e.to, e.toPort, plan->outKind);
        afterConnectionsChanged();
        return true;
    }
    return false;
}

bool ClipGraph::detachNode(NodeId id) {
    if (!hasNode(id)) return false;
    QVector<Edge> incoming, outgoing;
    for (const Edge &e : std::as_const(m_edges)) {
        if (e.to == id && e.from != id) incoming.append(e);
        else if (e.from == id && e.to != id) outgoing.append(e);
    }
    if (incoming.isEmpty() && outgoing.isEmpty()) return false;
    removeEdgesOfNode(id);
    QVector<bool> used(outgoing.size(), false);
    for (const Edge &in : std::as_const(incoming)) {
        for (int k = 0; k < outgoing.size(); ++k) {
            if (used[k]) continue;
            const Edge &out = outgoing[k];
            const auto plan = planConnection(in.from, in.fromPort, out.to, out.toPort);
            if (!plan || plan->insertMixer) continue;
            if (!connectPortsRaw(in.from, in.fromPort, out.to, out.toPort, plan->kind)) continue;
            used[k] = true;
            break;
        }
    }
    afterConnectionsChanged();
    return true;
}

QJsonObject ClipGraph::copyNodes(const QList<NodeId> &ids) const {
    const QSet<NodeId> wanted(ids.begin(), ids.end());
    QList<NodeId> picked;
    for (NodeId id : m_order) {
        const Node *n = node(id);
        if (wanted.contains(id) && n->type != NT::Output && n->type != NT::MasterAudioOutput)
            picked.append(id);
    }

    QJsonArray nodes;
    qreal minX = 0, minY = 0;
    for (qsizetype i = 0; i < picked.size(); ++i) {
        const Node *n = node(picked[i]);
        if (i == 0) { minX = n->pos.x(); minY = n->pos.y(); }
        minX = std::min(minX, n->pos.x());
        minY = std::min(minY, n->pos.y());
        nodes.append(QJsonObject{{"type", int(n->type)}, {"x", n->pos.x()}, {"y", n->pos.y()},
                                 {"params", toJsonObject(n->params)}});
    }

    QJsonArray edges;
    for (const Edge &e : m_edges) {
        const qsizetype from = picked.indexOf(e.from);
        const qsizetype to = picked.indexOf(e.to);
        if (from < 0 || to < 0) continue;
        edges.append(QJsonObject{{"from", int(from)}, {"fromPort", e.fromPort}, {"to", int(to)},
                                 {"toPort", e.toPort}, {"kind", int(e.kind)}});
    }
    return QJsonObject{{"originX", minX}, {"originY", minY}, {"nodes", nodes}, {"edges", edges}};
}

QList<NodeId> ClipGraph::pasteNodes(const QJsonObject &clip, const QPointF &delta) {
    QList<NodeId> created;
    for (const QJsonValue &v : clip["nodes"].toArray()) {
        const QJsonObject o = v.toObject();
        QVariantMap params = o["params"].toObject().toVariantMap();
        const NodeType type = NodeType(o["type"].toInt());
        if (type == NT::Input) {
            QVariantMap src = params.value("source").toMap();
            if (!src.value("captureId").toString().isEmpty()) {
                src["captureId"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
                params["source"] = src;
            }
        }
        created.append(createNode(type, QPointF(o["x"].toDouble(), o["y"].toDouble()) + delta, params));
    }
    for (const QJsonValue &v : clip["edges"].toArray()) {
        const QJsonObject o = v.toObject();
        const int from = o["from"].toInt(-1);
        const int to = o["to"].toInt(-1);
        if (from < 0 || to < 0 || from >= created.size() || to >= created.size()) continue;
        connectPortsRaw(created[from], o["fromPort"].toInt(-1), created[to], o["toPort"].toInt(-1),
                        EdgeKind(o["kind"].toInt()));
    }
    if (!created.isEmpty()) {
        ensureOutputNode();
        afterConnectionsChanged();
    }
    return created;
}

NodeId ClipGraph::createMixerAt(const QPointF &scenePos) {
    const NodeId id = createNode(NT::AudioMixer, QPointF(), defaultParams(NT::AudioMixer));
    Node *n = mutableNode(id);
    n->pos = scenePos - QPointF(kMixW / 2.0, n->size.height() / 2.0);
    ensureOutputNode();
    return id;
}

int ClipGraph::firstOpenMixerInput(NodeId mixer) {
    Node *n = mutableNode(mixer);
    if (!n) return -1;
    if (normalizeSlots(*n)) {
        refreshNode(*n);
        notifyNode(mixer);
    }
    for (int i = 0; i < slotCount(*n); ++i)
        if (!isPortConnected(mixer, PD::In, i)) return i;
    return -1;
}

void ClipGraph::insertMixerForStreamToOutput(NodeId streamNode, int streamPort,
                                             NodeId deviceNode, int devicePort) {
    NodeId existing = 0;
    for (const Edge &e : std::as_const(m_edges)) {
        if (e.kind == EK::MixerToOutput && e.to == deviceNode && e.toPort == devicePort) {
            existing = e.from;
            break;
        }
    }
    if (existing != 0) {
        const int slot = firstOpenMixerInput(existing);
        if (slot >= 0) connectPortsRaw(streamNode, streamPort, existing, slot, EK::StreamToMixer);
        return;
    }

    const QPointF mid = (portScenePos(streamNode, PD::Out, streamPort)
                         + portScenePos(deviceNode, PD::In, devicePort)) / 2.0;

    if (isPortConnected(deviceNode, PD::In, devicePort)) {
        for (int i = 0; i < m_edges.size(); ++i) {
            const Edge e = m_edges[i];
            if (e.to != deviceNode || e.toPort != devicePort) continue;
            const QVector<Port> outs = outputPorts(e.from);
            if (e.fromPort < 0 || e.fromPort >= outs.size()
                || !isAudioStreamOutKind(outs[e.fromPort].kind))
                continue;
            const NodeId mixer = createMixerAt(mid);
            removeEdgeAt(i);
            connectPortsRaw(e.from, e.fromPort, mixer, 0, EK::StreamToMixer);
            connectPortsRaw(mixer, 0, deviceNode, devicePort, EK::MixerToOutput);
            const int slot1 = firstOpenMixerInput(mixer);
            if (slot1 >= 0) connectPortsRaw(streamNode, streamPort, mixer, slot1, EK::StreamToMixer);
            return;
        }
    }

    const NodeId mixer = createMixerAt(mid);
    connectPortsRaw(streamNode, streamPort, mixer, 0, EK::StreamToMixer);
    connectPortsRaw(mixer, 0, deviceNode, devicePort, EK::MixerToOutput);
}

void ClipGraph::migrateLegacyAudioConnections() {
    struct Legacy { NodeId from; int fromPort; NodeId to; int toPort; };
    QVector<Legacy> pending;
    for (int i = int(m_edges.size()) - 1; i >= 0; --i) {
        const Edge &e = m_edges[i];
        if (e.kind != EK::InputToMaster) continue;
        pending.prepend({e.from, e.fromPort, e.to, e.toPort});
        removeEdgeAt(i);
    }
    for (const Legacy &l : std::as_const(pending))
        insertMixerForStreamToOutput(l.from, l.fromPort, l.to, l.toPort);
}

// ── Slot normalisation ──────────────────────────────────────────────────────

bool ClipGraph::normalizeSlots(Node &n) {
    const QVariantList old = n.params.value("slots").toList();
    const int oldCount = int(old.size());
    QVariantList fresh;
    QVector<int> remap(oldCount, -1);
    for (int i = 0; i < oldCount; ++i) {
        if (isPortConnected(n.id, PD::In, i)) {
            remap[i] = int(fresh.size());
            fresh.append(old[i]);
        }
    }
    fresh.append(defaultSlot(n.type));
    const int newCount = int(fresh.size());

    bool changed = fresh != old;
    if (changed) {
        n.params["slots"] = fresh;
        for (Edge &e : m_edges) {
            if (e.to != n.id) continue;
            const int next = e.toPort < oldCount ? remap[e.toPort] : e.toPort + (newCount - oldCount);
            if (next >= 0 && next != e.toPort) {
                e.toPort = next;
                if (!m_restoring) emit edgeChanged(e.id);
            }
        }
        n.size = nodeSize(n);
    }
    return changed;
}

void ClipGraph::normalizeSwitchingInputs() {
    auto defaultName = [this](NodeId producer) -> QString {
        const ResolvedStream s = evaluateVideoInput(producer);
        if (!s.layers.isEmpty()) return sourceName(s.layers.first().inputNodeId);
        return {};
    };
    {
        for (NodeId id : nodeIdsOfType(NT::Layer)) {
            Node *n = mutableNode(id);
            bool changed = normalizeSlots(*n);
            QVariantList slotList = n->params.value("slots").toList();
            for (int i = 0; i < slotList.size(); ++i) {
                QVariantMap s = slotList[i].toMap();
                if (!s.value("name").toString().isEmpty()) continue;
                const NodeId prod = edgeProducer(id, i, EK::Chain);
                if (prod == 0) continue;
                const QString name = defaultName(prod);
                if (name.isEmpty()) continue;
                s["name"] = name;
                slotList[i] = s;
                changed = true;
            }
            if (changed) {
                n->params["slots"] = slotList;
                refreshNode(*n);
                notifyNode(id);
            }
        }
    }
}

void ClipGraph::normalizeMixerInputs() {
    auto streamName = [this](NodeId producer) -> QString {
        const Node *p = node(producer);
        if (!p) return {};
        if (p->type == NT::Input) return sourceName(producer);
        const QString label = p->params.value("deviceLabel").toString();
        if (p->type == NT::MasterAudioInput) return label.isEmpty() ? QStringLiteral("Mic") : label;
        if (p->type == NT::MasterAudioCapture)
            return label.isEmpty() ? QStringLiteral("System Audio") : label;
        return {};
    };
    for (NodeId id : nodeIdsOfType(NT::AudioMixer)) {
        Node *n = mutableNode(id);
        bool changed = normalizeSlots(*n);
        QVariantList slotList = n->params.value("slots").toList();
        for (int i = 0; i < slotList.size(); ++i) {
            QVariantMap s = slotList[i].toMap();
            if (!s.value("name").toString().isEmpty()) continue;
            const NodeId prod = edgeProducer(id, i, EK::StreamToMixer);
            if (prod == 0) continue;
            const QString name = streamName(prod);
            if (name.isEmpty()) continue;
            s["name"] = name;
            slotList[i] = s;
            changed = true;
        }
        if (changed) {
            n->params["slots"] = slotList;
            refreshNode(*n);
            notifyNode(id);
        }
    }
}

// ── Evaluation ──────────────────────────────────────────────────────────────

NodeId ClipGraph::edgeProducer(NodeId nodeId, int inPort, EdgeKind kind) const {
    for (const Edge &e : m_edges)
        if (e.kind == kind && e.to == nodeId && e.toPort == inPort) return e.from;
    return 0;
}

NodeId ClipGraph::producerForInput(NodeId nodeId, int inPort) const {
    return edgeProducer(nodeId, inPort, EK::Chain);
}

ClipGraph::ResolvedStream ClipGraph::evaluateVideoInput(NodeId producerNode) const {
    return evaluateGuarded(producerNode, {});
}


ClipGraph::ResolvedStream ClipGraph::evaluateGuarded(NodeId producerNode, QSet<NodeId> visited) const {
    ResolvedStream out;
    if (producerNode == 0 || visited.contains(producerNode)) return out;
    visited.insert(producerNode);

    const Node *n = node(producerNode);
    if (!n) return out;

    if (n->type == NT::Input) {
        ResolvedLayer layer;
        layer.inputNodeId = producerNode;
        out.layers.push_back(layer);
        return out;
    }

    if (n->type == NT::Process) {
        const ProcessEffectDescriptor *desc = ProcessEffects::byId(n->params.value("effect").toInt());
        if (!desc) return out;
        out = evaluateGuarded(producerForInput(producerNode, 0), visited);
        if (desc->isDecorator && !n->params.value("bypass").toBool()) {
            SourceEffectRef ref{desc->id, toJsonObject(n->params.value("params").toMap()), {}};
            for (int i = 1; i <= processExtraInputs(*n); ++i) {
                const NodeId up = producerForInput(producerNode, i);
                std::shared_ptr<SourceEffectAux> aux;
                if (up != 0) {
                    aux = std::make_shared<SourceEffectAux>();
                    aux->stream = evaluateGuarded(up, visited);
                }
                ref.aux.append(aux);
            }
            for (ResolvedLayer &l : out.layers)
                l.sourceEffects.append(ref);
        }
        return out;
    }

    if (n->type == NT::Layer) {
        // Flatten into one composited stream so a downstream process effect acts
        // on the composite rather than on each layer independently.
        auto sub = std::make_shared<ResolvedStream>();
        int canvasW = 0, canvasH = 0;
        layerCanvasSize(producerNode, canvasW, canvasH);
        sub->canvasWidth = canvasW;
        sub->canvasHeight = canvasH;
        const QVariantList slotList = n->params.value("slots").toList();
        for (int i = int(slotList.size()) - 1; i >= 0; --i) {
            const NodeId up = producerForInput(producerNode, i);
            if (up == 0) continue;
            ResolvedStream s = evaluateGuarded(up, visited);
            const QVariantMap slot = slotList[i].toMap();
            for (ResolvedLayer &l : s.layers) {
                l.baseX = slot.value("baseX").toFloat();
                l.baseY = slot.value("baseY").toFloat();
                l.baseW = slot.value("baseW").toFloat();
                l.baseH = slot.value("baseH").toFloat();
                l.visible = l.visible && slot.value("visible").toBool();
                sub->layers.push_back(l);
            }
        }
        ResolvedLayer composite;
        composite.composite = sub;
        composite.layerNodeId = producerNode;
        out.layers.push_back(composite);
        out.canvasWidth = canvasW;
        out.canvasHeight = canvasH;
        return out;
    }

    return out;
}

NodeId ClipGraph::outputSingleProducer() const {
    if (m_outputNode == 0) return 0;
    return producerForInput(m_outputNode, 0);
}

// ── Layer placement ─────────────────────────────────────────────────────────

QVector<ClipGraph::LayerSlotView> ClipGraph::layerSlotViews(NodeId layerId) const {
    QVector<LayerSlotView> views;
    const Node *n = node(layerId);
    if (!n || n->type != NT::Layer) return views;
    const QVariantList slotList = n->params.value("slots").toList();
    // Reversed so the list order matches the canvas z-order (last = drawn on top).
    for (int i = int(slotList.size()) - 1; i >= 0; --i) {
        const NodeId up = producerForInput(layerId, i);
        if (up == 0) continue;
        const QVariantMap s = slotList[i].toMap();
        LayerSlotView v;
        v.index = i;
        v.rect = QRectF(s.value("baseX").toDouble(), s.value("baseY").toDouble(),
                        s.value("baseW").toDouble(), s.value("baseH").toDouble());
        v.name = slotLabel(slotList, i);
        v.visible = s.value("visible").toBool();
        v.producer = up;
        const ResolvedStream sub = evaluateVideoInput(up);
        if (!sub.layers.isEmpty()) v.sourceNode = sub.layers.first().inputNodeId;
        views.push_back(v);
    }
    return views;
}

bool ClipGraph::setLayerSlotRect(NodeId layerId, int index, float x, float y, float w, float h) {
    Node *n = mutableNode(layerId);
    if (!n || n->type != NT::Layer) return false;
    QVariantList slotList = n->params.value("slots").toList();
    if (index < 0 || index >= slotList.size()) return false;
    QVariantMap s = slotList[index].toMap();
    s["baseX"] = double(x);
    s["baseY"] = double(y);
    s["baseW"] = double(w);
    s["baseH"] = double(h);
    slotList[index] = s;
    n->params["slots"] = slotList;
    notifyNode(layerId);
    notifyChanged();
    return true;
}

bool ClipGraph::setLayerSlotVisible(NodeId layerId, int index, bool visible) {
    Node *n = mutableNode(layerId);
    if (!n || n->type != NT::Layer) return false;
    QVariantList slotList = n->params.value("slots").toList();
    if (index < 0 || index >= slotList.size()) return false;
    QVariantMap s = slotList[index].toMap();
    s["visible"] = visible;
    slotList[index] = s;
    n->params["slots"] = slotList;
    notifyNode(layerId);
    notifyChanged();
    return true;
}

bool ClipGraph::layerCanvasSize(NodeId layerId, int &w, int &h) const {
    const Node *n = node(layerId);
    if (!n || n->type != NT::Layer) return false;
    if (n->params.value("canvasCustom").toBool()) {
        w = n->params.value("canvasW").toInt();
        h = n->params.value("canvasH").toInt();
    } else {
        w = m_outputCanvasW;
        h = m_outputCanvasH;
    }
    return true;
}

bool ClipGraph::setLayerCanvasSize(NodeId layerId, int w, int h) {
    Node *n = mutableNode(layerId);
    if (!n || n->type != NT::Layer) return false;
    n->params["canvasCustom"] = true;
    n->params["canvasW"] = w;
    n->params["canvasH"] = h;
    notifyNode(layerId);
    notifyChanged();
    return true;
}

bool ClipGraph::layerCanvasCustom(NodeId layerId) const {
    const Node *n = node(layerId);
    return n && n->type == NT::Layer && n->params.value("canvasCustom").toBool();
}

bool ClipGraph::setLayerCanvasCustom(NodeId layerId, bool custom) {
    Node *n = mutableNode(layerId);
    if (!n || n->type != NT::Layer) return false;
    n->params["canvasCustom"] = custom;
    notifyNode(layerId);
    notifyChanged();
    return true;
}

void ClipGraph::setOutputCanvasSize(int w, int h) {
    if (w == m_outputCanvasW && h == m_outputCanvasH) return;
    m_outputCanvasW = w;
    m_outputCanvasH = h;
    bool anyFollowing = false;
    for (const Node &n : m_nodes) {
        if (n.type == NT::Layer && !n.params.value("canvasCustom").toBool()) {
            notifyNode(n.id);
            anyFollowing = true;
        }
    }
    if (anyFollowing) notifyChanged();
}

// ── Audio / script queries ──────────────────────────────────────────────────

bool ClipGraph::edgeFromAudioStreamOut(NodeId from, const Edge **out) const {
    static const EK kinds[] = {EK::AudioEffectChain, EK::StreamToMixer,
                               EK::InputToMaster};
    for (EK wanted : kinds) {
        for (const Edge &e : m_edges) {
            if (e.from == from && e.kind == wanted) {
                *out = &e;
                return true;
            }
        }
    }
    return false;
}

QString ClipGraph::deviceIdAt(const Node &outNode, int index) const {
    return outNode.params.value("devicePorts").toList().value(index).toMap()
        .value("deviceId").toString();
}

bool ClipGraph::resolveAudioStreamRoute(NodeId sourceNodeId, ResolvedAudioRoute &route) const {
    route = {};
    NodeId current = sourceNodeId;
    QSet<NodeId> seen;
    while (true) {
        if (seen.contains(current)) return false;
        seen.insert(current);

        const Edge *e = nullptr;
        if (!edgeFromAudioStreamOut(current, &e)) return false;
        const NodeId toId = e->to;

        if (e->kind == EK::AudioEffectChain) {
            const Node *ae = node(toId);
            if (ae && ae->type == NT::AudioEffect) {
                if (!ae->params.value("bypass").toBool())
                    route.effects.append({int(toId), prism::audiofx::fromJson(
                        toJsonObject(ae->params.value("params").toMap())),
                        std::clamp(ae->params.value("wet", 1.0).toFloat(), 0.0f, 1.0f)});
                current = toId;
                continue;
            }
            return false;
        }

        if (e->kind == EK::StreamToMixer) {
            route.mixerNodeId = toId;
            route.mixerSlotIndex = e->toPort;
            if (const Node *mx = node(toId)) {
                const QVariantList slotList = mx->params.value("slots").toList();
                if (route.mixerSlotIndex >= 0 && route.mixerSlotIndex < slotList.size()) {
                    const QVariantMap s = slotList[route.mixerSlotIndex].toMap();
                    route.mixerSlotVolume = s.value("volume").toInt();
                    route.mixerSlotMuted = s.value("muted").toBool();
                }
            }
            for (const Edge &m : m_edges) {
                if (m.from == route.mixerNodeId && m.kind == EK::MixerToOutput) {
                    route.outputNodeId = m.to;
                    route.outputPortIndex = m.toPort;
                    if (const Node *out = node(route.outputNodeId);
                        out && out->type == NT::MasterAudioOutput)
                        route.outputDeviceId = deviceIdAt(*out, route.outputPortIndex);
                    break;
                }
            }
            return route.isValid();
        }

        if (e->kind == EK::InputToMaster) {
            route.outputNodeId = toId;
            route.outputPortIndex = e->toPort;
            if (route.outputPortIndex < 0) route.outputPortIndex = 0;
            if (const Node *out = node(route.outputNodeId);
                out && out->type == NT::MasterAudioOutput)
                route.outputDeviceId = deviceIdAt(*out, route.outputPortIndex);
            return route.isValid();
        }

        return false;
    }
}

bool ClipGraph::audioSettingsForClip(NodeId clipId, int &volume, bool &muted, bool &routedToMaster,
                                     int &playbackMode, int &delayMs, QString &outputDeviceId) const {
    const Node *clip = node(clipId);
    if (!clip || clip->type != NT::Input || !clip->params.value("hasAudio").toBool()) return false;

    volume = std::clamp(clip->params.value("audioVolume", 100).toInt(), 0, 100);
    muted = clip->params.value("audioMuted").toBool();
    playbackMode = clip->params.value("audioPlaybackMode").toInt();
    delayMs = clip->params.value("audioDelayMs").toInt();
    outputDeviceId.clear();

    ResolvedAudioRoute route;
    routedToMaster = resolveAudioStreamRoute(clipId, route);
    if (routedToMaster) {
        outputDeviceId = route.outputDeviceId;
        if (route.mixerSlotIndex >= 0) {
            volume = volume * route.mixerSlotVolume / 100;
            muted = muted || route.mixerSlotMuted;
        }
    }
    return true;
}

bool ClipGraph::masterSettings(NodeId id, NodeType type, MasterAudioSettings &settings) const {
    const Node *n = node(id);
    if (!n || n->type != type) return false;
    settings = {};
    settings.deviceId = n->params.value("deviceId").toString();
    settings.deviceLabel = n->params.value("deviceLabel").toString();
    settings.volume = std::clamp(n->params.value("volume", 100).toInt(), 0, 100);
    settings.muted = n->params.value("muted").toBool();
    ResolvedAudioRoute route;
    if (resolveAudioStreamRoute(id, route)) {
        settings.routedMasterOutputId = route.outputNodeId;
        settings.routedOutputPortIndex = route.outputPortIndex;
        if (route.mixerSlotIndex >= 0) {
            settings.volume = settings.volume * route.mixerSlotVolume / 100;
            settings.muted = settings.muted || route.mixerSlotMuted;
        }
    }
    return true;
}

bool ClipGraph::masterAudioInputSettings(NodeId inputNodeId, MasterAudioSettings &settings) const {
    return masterSettings(inputNodeId, NT::MasterAudioInput, settings);
}

bool ClipGraph::masterAudioCaptureSettings(NodeId captureNodeId, MasterAudioSettings &settings) const {
    return masterSettings(captureNodeId, NT::MasterAudioCapture, settings);
}

bool ClipGraph::masterAudioOutputDevice(NodeId masterOutputNodeId, QString &outputDeviceId) const {
    return masterAudioOutputDeviceForPort(masterOutputNodeId, 0, outputDeviceId);
}

bool ClipGraph::masterAudioOutputDeviceForPort(NodeId masterOutputNodeId, int portIndex,
                                               QString &outputDeviceId) const {
    const Node *n = node(masterOutputNodeId);
    if (!n || n->type != NT::MasterAudioOutput) return false;
    outputDeviceId = deviceIdAt(*n, portIndex);
    return true;
}

NodeId ClipGraph::audioOutputNodeId() const {
    const QVector<NodeId> ids = nodeIdsOfType(NT::MasterAudioOutput);
    return ids.isEmpty() ? 0 : ids.first();
}

bool ClipGraph::mixerSlotSettings(NodeId mixerId, int slotIndex, int &volume, bool &muted,
                                  QString &name) const {
    const Node *n = node(mixerId);
    if (!n || n->type != NT::AudioMixer) return false;
    const QVariantList slotList = n->params.value("slots").toList();
    if (slotIndex < 0 || slotIndex >= slotList.size()) return false;
    const QVariantMap s = slotList[slotIndex].toMap();
    volume = s.value("volume").toInt();
    muted = s.value("muted").toBool();
    name = s.value("name").toString();
    return true;
}

NodeId ClipGraph::clipForAudioAnalyzer(NodeId scriptNodeId) const {
    for (const Edge &e : m_edges)
        if (e.kind == EK::ClipToAudioAnalyzer && e.to == scriptNodeId) return e.from;
    return 0;
}

bool ClipGraph::audioSourceForAudioAnalyzer(NodeId scriptNodeId, QString &filePath) const {
    const Node *clip = node(clipForAudioAnalyzer(scriptNodeId));
    if (!clip || clip->type != NT::Input) return false;
    const QVariantMap src = clip->params.value("source").toMap();
    const int kind = src.value("kind").toInt();
    if (kind != int(SourceDescriptor::Kind::VideoFile) && kind != int(SourceDescriptor::Kind::AudioFile))
        return false;
    filePath = src.value("path").toString();
    return !filePath.isEmpty();
}

NodeId ClipGraph::audioSourceNodeIdForAudioAnalyzer(NodeId scriptNodeId) const {
    return clipForAudioAnalyzer(scriptNodeId);
}

bool ClipGraph::isLiveAudioAnalyzerProducer(NodeId producerId) const {
    const Node *n = node(producerId);
    return n && (n->type == NT::MasterAudioInput || n->type == NT::MasterAudioCapture);
}

bool ClipGraph::hasAudioAnalyzerConsumer(NodeId producerId) const {
    for (NodeId id : nodeIdsOfType(NT::AudioAnalyzer))
        if (clipForAudioAnalyzer(id) == producerId) return true;
    return false;
}

NodeId ClipGraph::dataScriptNodeId(NodeId dataNodeId) const {
    for (const Edge &e : m_edges)
        if (e.kind == EK::ScriptToData && e.to == dataNodeId) return e.from;
    return 0;
}

QVector<NodeId> ClipGraph::scriptProducersForData(NodeId dataNodeId) const {
    QVector<NodeId> out;
    for (const Edge &e : m_edges)
        if (e.kind == EK::ScriptToData && e.to == dataNodeId) out.append(e.from);
    return out;
}

// ── Persistence ─────────────────────────────────────────────────────────────

QJsonObject ClipGraph::toJson() const {
    QJsonObject root;
    root["graphVersion"] = kGraphVersion;
    root["nextId"] = (qint64)m_nextId;

    QJsonArray inputNodes, processNodes, audioEffectNodes, layerNodes,
        audioMixerNodes, scriptNodes, masterAudioNodes, masterAudioInputNodes,
        masterAudioCaptureNodes;

    for (const Node &n : m_nodes) {
        QJsonObject o = toJsonObject(n.params);
        o["id"] = (qint64)n.id;
        o["posX"] = n.pos.x();
        o["posY"] = n.pos.y();
        switch (n.type) {
        case NT::Input:             inputNodes.append(o); break;
        case NT::Process:
            if (n.params.value("effect").toInt() == 0) {
                const QJsonObject p = toJsonObject(n.params.value("params").toMap());
                o["cropX"] = p["x"].toDouble(0.0);
                o["cropY"] = p["y"].toDouble(0.0);
                o["cropW"] = p["w"].toDouble(1.0);
                o["cropH"] = p["h"].toDouble(1.0);
            }
            processNodes.append(o);
            break;
        case NT::AudioEffect:       audioEffectNodes.append(o); break;
        case NT::Layer:             layerNodes.append(o); break;
        case NT::AudioMixer:        audioMixerNodes.append(o); break;
        case NT::Output:
            root["outputNode"] = QJsonObject{{"id", (qint64)n.id}, {"posX", n.pos.x()}, {"posY", n.pos.y()}};
            if (n.params.contains("name")) {
                QJsonObject out = root["outputNode"].toObject();
                out["name"] = n.params.value("name").toString();
                root["outputNode"] = out;
            }
            break;
        case NT::Script:
        case NT::AudioAnalyzer:     scriptNodes.append(o); break;
        case NT::MasterAudioOutput: masterAudioNodes.append(o); break;
        case NT::MasterAudioInput:  masterAudioInputNodes.append(o); break;
        case NT::MasterAudioCapture: masterAudioCaptureNodes.append(o); break;
        }
    }
    root["inputNodes"] = inputNodes;
    root["processNodes"] = processNodes;
    root["audioEffectNodes"] = audioEffectNodes;
    root["layerNodes"] = layerNodes;
    root["audioMixerNodes"] = audioMixerNodes;
    root["scriptNodes"] = scriptNodes;
    root["masterAudioNodes"] = masterAudioNodes;
    root["masterAudioInputNodes"] = masterAudioInputNodes;
    root["masterAudioCaptureNodes"] = masterAudioCaptureNodes;

    QJsonArray connections;
    for (const Edge &e : m_edges) {
        const QVector<Port> ins = inputPorts(e.to);
        const bool slotted = e.toPort >= 0 && e.toPort < ins.size() && ins[e.toPort].slotted;
        QJsonObject obj;
        obj["from"] = (qint64)e.from;
        obj["to"] = (qint64)e.to;
        obj["kind"] = (int)e.kind;
        obj["toPortIndex"] = slotted ? e.toPort : -1;
        connections.append(obj);
    }
    root["connections"] = connections;
    return root;
}

bool ClipGraph::fromJson(const QJsonObject &state) {
    m_restoring = true;
    m_nodes.clear();
    m_order.clear();
    m_edges.clear();
    m_outputNode = 0;
    m_nextId = m_idBase + 1;

    auto finish = [this](bool ok) {
        m_restoring = false;
        emit graphReset();
        emit changed();
        return ok;
    };

    if (state.value("graphVersion").toInt(0) < kGraphVersion) return finish(false);

    NodeId maxId = 0;
    auto restore = [&](const QJsonObject &obj, NodeType type, const QVariantMap &params) {
        const NodeId id = (NodeId)obj["id"].toInteger();
        if (id == 0 || m_nodes.contains(id)) return;
        Node n;
        n.id = id;
        n.type = type;
        n.pos = QPointF(obj["posX"].toDouble(), obj["posY"].toDouble());
        n.params = params;
        if (type != NT::Input && !obj["name"].toString().isEmpty()) n.params["name"] = obj["name"].toString();
        if (obj["expressions"].isObject()) n.params["expressions"] = obj["expressions"].toObject().toVariantMap();
        refreshNode(n);
        m_nodes.insert(id, n);
        m_order.append(id);
        maxId = std::max(maxId, id);
        if (type == NT::Output) m_outputNode = id;
    };

    for (const QJsonValue &val : state["inputNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        SourceDescriptor desc = descriptorFromJson(obj["source"].toObject());
        using K = SourceDescriptor::Kind;
        if (desc.kind == K::VideoFile || desc.kind == K::Image || desc.kind == K::AudioFile)
            desc.displayName = QFileInfo(desc.path).baseName();

        const bool audioOnly = obj["audioOnly"].toBool(desc.kind == K::AudioFile);
        const bool hasAudio = obj["hasAudio"].toBool(audioOnly) || audioOnly;

        const int mode = obj["audioPlaybackMode"].toInt(0) == 1 ? 1 : 0;

        QVariantMap p;
        p["hasAudio"] = hasAudio;
        p["audioOnly"] = audioOnly;
        if (hasAudio) {
            p["audioVolume"] = obj["audioVolume"].toInt(100);
            p["audioMuted"] = obj["audioMuted"].toBool(false);
            p["audioPlaybackMode"] = mode;
            p["audioDelayMs"] = obj["audioDelayMs"].toInt(0);
        }
        p["source"] = descriptorToJson(desc).toVariantMap();
        p["settings"] = (obj.contains("settings")
            ? ClipSettings::fromJson(obj["settings"].toObject()) : ClipSettings{}).toJson().toVariantMap();
        p["repeat"] = obj["repeat"].toBool();
        restore(obj, NT::Input, p);
    }

    for (const QJsonValue &val : state["processNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const ProcessEffectDescriptor *desc = ProcessEffects::byId(obj["effect"].toInt());
        if (!desc) continue;
        QJsonObject params = desc->defaultParams;
        if (obj.contains("params")) {
            params = obj["params"].toObject();
        } else if (desc->id == 0) {
            params["x"] = obj["cropX"].toDouble(0.0);
            params["y"] = obj["cropY"].toDouble(0.0);
            params["w"] = obj["cropW"].toDouble(1.0);
            params["h"] = obj["cropH"].toDouble(1.0);
        }
        QVariantMap nodeParams{{"effect", desc->id}, {"params", params.toVariantMap()}};
        if (obj.contains("bypass")) nodeParams["bypass"] = obj["bypass"].toBool();
        restore(obj, NT::Process, nodeParams);
    }

    for (const QJsonValue &val : state["audioEffectNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const AudioEffectDescriptor *desc = AudioEffects::byId(obj["effect"].toInt());
        if (!desc) continue;
        const QJsonObject params = obj.contains("params") ? obj["params"].toObject() : desc->defaultParams;
        QVariantMap nodeParams{{"effect", desc->id}, {"params", params.toVariantMap()}};
        if (obj.contains("bypass")) nodeParams["bypass"] = obj["bypass"].toBool();
        if (obj.contains("wet")) nodeParams["wet"] = obj["wet"].toDouble(1.0);
        restore(obj, NT::AudioEffect, nodeParams);
    }

    auto readSlots = [](const QJsonObject &obj, NT type) {
        QVariantList slotList;
        for (const QJsonValue &sv : obj["slots"].toArray())
            slotList.append(canonSlot(type, sv.toObject()));
        if (slotList.isEmpty()) slotList.append(defaultSlot(type));
        return slotList;
    };

    for (const QJsonValue &val : state["layerNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const int canvasW = obj["canvasW"].toInt(1280);
        const int canvasH = obj["canvasH"].toInt(720);
        // Sessions from before the override existed: anything but the old 1280x720 default was a deliberate choice.
        const bool custom = obj.contains("canvasCustom") ? obj["canvasCustom"].toBool()
                                                         : (canvasW != 1280 || canvasH != 720);
        restore(obj, NT::Layer, {{"canvasCustom", custom},
                                 {"canvasW", canvasW},
                                 {"canvasH", canvasH},
                                 {"slots", readSlots(obj, NT::Layer)}});
    }

    if (state.contains("outputNode"))
        restore(state["outputNode"].toObject(), NT::Output, {});

    for (const QJsonValue &val : state["scriptNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        const QString scriptType = obj["scriptType"].toString();
        if (scriptType == QStringLiteral("audio")) {
            const AudioAnalyzerConfig c;
            QVariantMap p = defaultParams(NT::AudioAnalyzer);
            p["fftSize"] = obj["fftSize"].toInt(c.fftSize);
            p["binCount"] = obj["binCount"].toInt(c.binCount);
            p["smoothing"] = double(float(obj["smoothing"].toDouble(c.smoothing)));
            p["beatSensitivity"] = double(float(obj["beatSensitivity"].toDouble(c.beatSensitivity)));
            p["lowMidHz"] = double(float(obj["lowMidHz"].toDouble(c.lowMidHz)));
            p["midHighHz"] = double(float(obj["midHighHz"].toDouble(c.midHighHz)));
            restore(obj, NT::AudioAnalyzer, p);
        } else {
            QVariantMap p = defaultParams(NT::Script);
            p["luaCode"] = obj["luaCode"].toString();
            p["runOn"] = std::clamp(obj["runOn"].toInt(0), 0, 3);
            p["intervalMs"] = std::max(20, obj["intervalMs"].toInt(1000));
            p["runTime"] = QTime::fromString(obj["runTime"].toString(QStringLiteral("12:00:00")),
                                             QStringLiteral("HH:mm:ss"))
                               .toString(QStringLiteral("HH:mm:ss"));
            restore(obj, NT::Script, p);
        }
    }

    for (const QJsonValue &val : state["masterAudioNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        QVariantList ports;
        for (const QJsonValue &pv : obj["devicePorts"].toArray()) {
            const QJsonObject po = pv.toObject();
            ports.append(QVariantMap{{"deviceId", po["deviceId"].toString()},
                                     {"deviceLabel", po["deviceLabel"].toString()}});
        }
        if (ports.isEmpty()) {
            const QString id = obj["deviceId"].toString();
            const QString label = obj["deviceLabel"].toString();
            ports.append(QVariantMap{{"deviceId", id},
                                     {"deviceLabel", label.isEmpty() && id.isEmpty()
                                                         ? QStringLiteral("Default") : label}});
        }
        const QVariantMap first = ports.first().toMap();
        restore(obj, NT::MasterAudioOutput, {{"devicePorts", ports},
                                             {"deviceId", first.value("deviceId")},
                                             {"deviceLabel", first.value("deviceLabel")}});
    }

    for (const QJsonValue &val : state["audioMixerNodes"].toArray()) {
        const QJsonObject obj = val.toObject();
        restore(obj, NT::AudioMixer, {{"slots", readSlots(obj, NT::AudioMixer)}});
    }

    for (NT type : {NT::MasterAudioInput, NT::MasterAudioCapture}) {
        const char *key = type == NT::MasterAudioInput ? "masterAudioInputNodes" : "masterAudioCaptureNodes";
        for (const QJsonValue &val : state[key].toArray()) {
            const QJsonObject obj = val.toObject();
            restore(obj, type, {{"deviceId", obj["deviceId"].toString()},
                                {"deviceLabel", obj["deviceLabel"].toString()},
                                {"volume", obj["volume"].toInt(100)},
                                {"muted", obj["muted"].toBool(false)}});
        }
    }

    m_nextId = std::max({maxId + 1, (NodeId)state["nextId"].toInteger(0), NodeId(m_idBase + 1)});

    for (const QJsonValue &val : state["connections"].toArray()) {
        const QJsonObject obj = val.toObject();
        const NodeId from = (NodeId)obj["from"].toInteger();
        const NodeId to = (NodeId)obj["to"].toInteger();
        const int kind = obj["kind"].toInt();
        int fp = -1, tp = -1;
        if (lookupConnectionPorts(from, to, kind, obj["toPortIndex"].toInt(-1), &fp, &tp))
            appendEdge(from, fp, to, tp, EK(kind));
    }
    migrateLegacyAudioConnections();

    normalizeSwitchingInputs();
    normalizeMixerInputs();
    if (m_outputNode == 0 && !m_nodes.isEmpty())
        ensureOutputNode();

    return finish(true);
}

QJsonObject ClipGraph::snapshot() const {
    QJsonArray nodes;
    auto append = [&](const Node &n, const QString &type, QJsonObject extra = {}) {
        extra.insert(QStringLiteral("id"), QString::number(n.id));
        extra.insert(QStringLiteral("type"), type);
        extra.insert(QStringLiteral("x"), n.pos.x());
        extra.insert(QStringLiteral("y"), n.pos.y());
        nodes.append(extra);
    };

    for (NodeType type : {NT::Input, NT::Process, NT::Layer}) {
        for (NodeId id : nodeIdsOfType(type)) {
            const Node &n = *node(id);
            switch (type) {
            case NT::Input:
                append(n, QStringLiteral("input"),
                       {{QStringLiteral("name"), sourceName(id)},
                        {QStringLiteral("kind"),
                         sourceKindName(n.params.value("source").toMap().value("kind").toInt())}});
                break;
            case NT::Process: {
                const ProcessEffectDescriptor *d = ProcessEffects::byId(n.params.value("effect").toInt());
                QJsonObject extra{{QStringLiteral("effect"), n.params.value("effect").toInt()},
                                  {QStringLiteral("name"), d ? d->name : QString()},
                                  {QStringLiteral("params"), toJsonObject(n.params.value("params").toMap())}};
                if (processExtraInputs(n) > 0) {
                    QJsonArray inputs;
                    for (const Port &p : inputPorts(id)) inputs.append(p.label);
                    extra.insert(QStringLiteral("inputs"), inputs);
                }
                append(n, QStringLiteral("process"), extra);
                break;
            }
            case NT::Layer:
                append(n, QStringLiteral("layer"), {{QStringLiteral("slots"), slotCount(n)}});
                break;
            default:
                break;
            }
        }
    }
    if (m_outputNode != 0)
        append(*node(m_outputNode), QStringLiteral("output"));

    QJsonArray connections;
    for (const Edge &e : m_edges) {
        const QVector<Port> ins = inputPorts(e.to);
        const bool slotted = e.toPort >= 0 && e.toPort < ins.size() && ins[e.toPort].slotted;
        connections.append(QJsonObject{
            {QStringLiteral("from"), QString::number(e.from)},
            {QStringLiteral("to"), QString::number(e.to)},
            {QStringLiteral("kind"), int(e.kind)},
            {QStringLiteral("kindName"), edgeKindName(int(e.kind))},
            {QStringLiteral("toPort"), slotted ? e.toPort : -1},
        });
    }

    return QJsonObject{
        {QStringLiteral("output"), m_outputNode ? QString::number(m_outputNode) : QString()},
        {QStringLiteral("nodes"), nodes},
        {QStringLiteral("connections"), connections},
    };
}
