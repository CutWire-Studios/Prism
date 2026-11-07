#include "mcp/McpDispatcher.h"
#include "mcp/McpCatalog.h"
#include "app/show/ShowManager.h"
#include "mcp/McpJson.h"

#include "core/media/ThumbnailExtractor.h"
#include "core/project/ClipManager.h"
#include "core/sources/ImageSource.h"
#include "core/sources/NdiSource.h"
#include "core/media/CameraEnumerator.h"
#include "app/common/ThumbHelper.h"
#include "core/sources/ShapeSource.h"
#include "core/sources/SvgTemplateSource.h"
#include "core/sources/TextSource.h"
#include "app/AppController.h"
#include "app/show/ShowManager.h"
#include "core/graph/ClipGraph.h"
#include "core/graph/ProcessEffects.h"
#include "app/output/OutputHub.h"
#include "app/session/SessionManager.h"
#include "app/transitions/TransitionController.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QtNumeric>

namespace prism::mcp {
namespace {

int jsonInt(const QJsonValue &v, int fallback = -1)
{
    if (v.isDouble())
        return v.toInt(fallback);
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().toInt(&ok);
        return ok ? n : fallback;
    }
    return fallback;
}

/// to_port as a slot number or the name of an input port ("b", "mask", ...). -2 = unknown name.
int resolveToPort(const ClipGraph *graph, NodeId to, const QJsonValue &v)
{
    if (v.isString()) {
        bool numeric = false;
        v.toString().toInt(&numeric);
        if (!numeric) {
            const QString name = v.toString().trimmed();
            for (const ClipGraph::Port &p : graph->inputPorts(to))
                if (p.slotted && p.label.compare(name, Qt::CaseInsensitive) == 0)
                    return p.index;
            return -2;
        }
    }
    return jsonInt(v, -1);
}

double jsonNumber(const QJsonValue &v, double fallback)
{
    if (v.isDouble())
        return v.toDouble(fallback);
    if (v.isString()) {
        bool ok = false;
        const double n = v.toString().toDouble(&ok);
        return ok ? n : fallback;
    }
    return fallback;
}

bool jsonBool(const QJsonValue &v, bool fallback = false)
{
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toInt() != 0;
    if (v.isString()) {
        const QString s = v.toString().toLower();
        if (s == QLatin1String("true") || s == QLatin1String("1"))
            return true;
        if (s == QLatin1String("false") || s == QLatin1String("0"))
            return false;
    }
    return fallback;
}

Scene *resolveScene(AppController *w, const QJsonObject &args, QJsonObject *error);

QString kindName(SourceDescriptor::Kind kind)
{
    using K = SourceDescriptor::Kind;
    switch (kind) {
    case K::VideoFile:  return QStringLiteral("video");
    case K::Image:      return QStringLiteral("image");
    case K::Slideshow:  return QStringLiteral("slideshow");
    case K::Camera:     return QStringLiteral("camera");
    case K::Screen:     return QStringLiteral("screen");
    case K::Canvas:     return QStringLiteral("canvas");
    case K::Window:     return QStringLiteral("window");
    case K::Shader:     return QStringLiteral("shader");
    case K::Ndi:        return QStringLiteral("ndi");
    case K::WebRtc:     return QStringLiteral("webrtc");
    case K::Text:       return QStringLiteral("text");
    case K::Shape:      return QStringLiteral("shape");
    case K::AudioFile:  return QStringLiteral("audio");
    case K::SvgTemplate: return QStringLiteral("svg_template");
    case K::Scene:      return QStringLiteral("scene");
    }
    return QStringLiteral("unknown");
}

bool parseKind(const QString &name, SourceDescriptor::Kind *out)
{
    const QString k = name.trimmed().toLower();
    using K = SourceDescriptor::Kind;
    if (k == QLatin1String("video") || k == QLatin1String("videofile"))
        *out = K::VideoFile;
    else if (k == QLatin1String("image"))
        *out = K::Image;
    else if (k == QLatin1String("audio") || k == QLatin1String("audiofile"))
        *out = K::AudioFile;
    else if (k == QLatin1String("slideshow"))
        *out = K::Slideshow;
    else if (k == QLatin1String("camera"))
        *out = K::Camera;
    else if (k == QLatin1String("canvas"))
        *out = K::Canvas;
    else if (k == QLatin1String("shader"))
        *out = K::Shader;
    else if (k == QLatin1String("svg_template") || k == QLatin1String("svg"))
        *out = K::SvgTemplate;
    else if (k == QLatin1String("text"))
        *out = K::Text;
    else if (k == QLatin1String("shape"))
        *out = K::Shape;
    else if (k == QLatin1String("ndi"))
        *out = K::Ndi;
    else if (k == QLatin1String("scene"))
        *out = K::Scene;
    else
        return false;
    return true;
}

QColor parseColor(const QString &s, const QColor &fallback = Qt::white)
{
    if (s.isEmpty())
        return fallback;
    const QColor c(s);
    return c.isValid() ? c : fallback;
}

QString defaultShader()
{
    return QStringLiteral(
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "uniform vec2 u_resolution;\n"
        "uniform float u_time;\n"
        "void main() {\n"
        "    vec2 uv = gl_FragCoord.xy / u_resolution.xy;\n"
        "    gl_FragColor = vec4(uv, 0.5 + 0.5 * sin(u_time), 1.0);\n"
        "}\n");
}

QImage thumbFor(const SourceDescriptor &desc)
{
    using K = SourceDescriptor::Kind;
    switch (desc.kind) {
    case K::VideoFile:
    case K::Image:
    case K::AudioFile:
        if (!desc.path.isEmpty())
            return ThumbnailExtractor::extract(desc.path, 110, 65).toImage();
        break;
    case K::Canvas:
        return ThumbHelper::makeCanvasThumb(QStringLiteral("%1x%2").arg(desc.canvasWidth).arg(desc.canvasHeight),
                                            desc.canvasFill, desc.color);
    case K::Shader:
        return ThumbHelper::makeShaderThumb(desc.shaderCode);
    case K::SvgTemplate:
        return ThumbHelper::makeSvgTemplateThumb(desc);
    case K::Text:
        return ThumbHelper::makeTextThumb(desc);
    case K::Shape:
        return ThumbHelper::makeShapeThumb(desc);
    case K::Camera:
        return ThumbHelper::makeIconThumb(QStringLiteral("photo_camera"));
    case K::Slideshow:
        return ThumbHelper::makeIconThumb(QStringLiteral("folder"));
    case K::Ndi:
        return ThumbHelper::makeIconThumb(QStringLiteral("sensors"));
    default:
        break;
    }
    return ThumbHelper::makeIconThumb(QStringLiteral("movie"));
}

QJsonObject clipOk(const ClipGraph *graph, NodeId id, QJsonObject extra = {})
{
    extra.insert(QStringLiteral("ok"), true);
    extra.insert(QStringLiteral("id"), QString::number(id));
    extra.insert(QStringLiteral("name"), graph->sourceName(id));
    extra.insert(QStringLiteral("kind"), kindName(graph->sourceDescriptor(id).kind));
    return extra;
}

} // namespace

McpDispatcher::McpDispatcher(AppController *app)
    : m_window(app)
{
}

QJsonObject McpDispatcher::inspect(const QJsonObject &args) const
{
    if (!m_window)
        return err("not_found", QStringLiteral("No mixer window"));
    const bool clips = jsonBool(args.value(QStringLiteral("clips")));
    const bool detail = jsonBool(args.value(QStringLiteral("detail")));
    const bool graph = jsonBool(args.value(QStringLiteral("graph")));
    const int since = jsonInt(args.value(QStringLiteral("since")), -1);
    return m_window->mcpInspect(clips, detail, since, graph);
}

QJsonObject McpDispatcher::apply(const QJsonObject &args)
{
    const QJsonArray ops = args.value(QStringLiteral("ops")).toArray();
    if (ops.isEmpty())
        return err("bad_args", QStringLiteral("ops must be a non-empty array"));

    QJsonArray results;
    for (int i = 0; i < ops.size(); ++i) {
        const QJsonObject op = ops.at(i).toObject();
        const QString tool = op.value(QStringLiteral("tool")).toString();
        const QJsonObject opArgs = op.value(QStringLiteral("args")).toObject();
        if (isHomepageTool(tool))
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("apply_failed")},
                    {QStringLiteral("stopped"), i},
                    {QStringLiteral("failed"), err("unknown_op", tool)},
                    {QStringLiteral("done"), results}};
        const QJsonObject one = applyOne(tool, opArgs);
        results.append(QJsonObject{{QStringLiteral("tool"), tool}, {QStringLiteral("result"), one}});
        if (!one.value(QStringLiteral("ok")).toBool()) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("apply_failed")},
                    {QStringLiteral("stopped"), i},
                    {QStringLiteral("failed"), one},
                    {QStringLiteral("done"), results}};
        }
    }
    return ok({{QStringLiteral("n"), ops.size()}, {QStringLiteral("done"), results}});
}

QJsonObject McpDispatcher::applyOne(const QString &tool, const QJsonObject &args)
{
    if (!m_window)
        return err("not_found", QStringLiteral("No mixer window"));
    if (tool == QLatin1String("list_clips"))
        return opListClips();
    if (tool == QLatin1String("list_source_kinds"))
        return opListSourceKinds();
    if (tool == QLatin1String("list_cameras"))
        return opListCameras();
    if (tool == QLatin1String("list_ndi_sources"))
        return opListNdiSources();
    if (tool == QLatin1String("add_clip"))
        return opAddClip(args);
    if (tool == QLatin1String("add_source"))
        return opAddSource(args);
    if (tool == QLatin1String("remove_clip"))
        return opRemoveClip(args);
    if (tool == QLatin1String("rename_clip"))
        return opRenameClip(args);
    if (tool == QLatin1String("set_text"))
        return opSetText(args);
    if (tool == QLatin1String("set_svg_params"))
        return opSetSvgParams(args);
    if (tool == QLatin1String("set_shader"))
        return opSetShader(args);
    if (tool == QLatin1String("set_scene_source"))
        return opSetSceneSource(args);
    if (tool == QLatin1String("list_scenes"))
        return opListScenes();
    if (tool == QLatin1String("stage_scene"))
        return opStageScene(args);
    if (tool == QLatin1String("add_scene"))
        return opAddScene(args);
    if (tool == QLatin1String("enter_scene"))
        return opEnterScene(args);
    if (tool == QLatin1String("take_scene"))
        return opTakeScene(args);
    if (tool == QLatin1String("remove_scene"))
        return opRemoveScene(args);
    if (tool == QLatin1String("reorder_scene"))
        return opReorderScene(args);
    if (tool == QLatin1String("duplicate_scene"))
        return opDuplicateScene(args);
    if (tool == QLatin1String("exit_scene"))
        return opExitScene();
    if (tool == QLatin1String("set_scene_props"))
        return opSetSceneProps(args);
    if (tool == QLatin1String("set_default_transition"))
        return opSetDefaultTransition(args);
    if (tool == QLatin1String("preflight"))
        return opPreflight();
    if (tool == QLatin1String("play"))
        return opPlayDeck(args, true);
    if (tool == QLatin1String("pause"))
        return opPlayDeck(args, false);
    if (tool == QLatin1String("seek"))
        return opSeekDeck(args);
    if (tool == QLatin1String("set_speed"))
        return opSetSpeed(args);
    if (tool == QLatin1String("set_transition_progress"))
        return opSetFader(args);
    if (tool == QLatin1String("cut"))
        return opCut();
    if (tool == QLatin1String("take"))
        return opTake();
    if (tool == QLatin1String("list_transitions"))
        return opListTransitions();
    if (tool == QLatin1String("set_transition"))
        return opSetTransition(args);
    if (tool == QLatin1String("set_duration"))
        return opSetDuration(args);
    if (tool == QLatin1String("set_panic"))
        return opSetPanic(args);
    if (tool == QLatin1String("start_recording"))
        return opStartRecording(args);
    if (tool == QLatin1String("stop_recording"))
        return opStopRecording();
    if (tool == QLatin1String("recording_status"))
        return opRecordingStatus();
    if (tool == QLatin1String("set_ndi"))
        return opSetNdi(args);
    if (tool == QLatin1String("set_virtual_camera"))
        return opSetVirtualCamera(args);
    if (tool == QLatin1String("save_session"))
        return opSaveSession(args);
    if (tool == QLatin1String("load_session"))
        return opLoadSession(args);
    if (tool == QLatin1String("list_process_effects"))
        return opListProcessEffects();
    if (tool == QLatin1String("list_nodes"))
        return opListNodes();
    if (tool == QLatin1String("add_process_node"))
        return opAddProcessNode(args);
    if (tool == QLatin1String("add_layer_node"))
        return opAddLayerNode(args);
    if (tool == QLatin1String("connect"))
        return opConnect(args);
    if (tool == QLatin1String("disconnect"))
        return opDisconnect(args);
    if (tool == QLatin1String("set_process_params"))
        return opSetProcessParams(args);
    if (tool == QLatin1String("add_modulator_node"))
        return opAddModulatorNode(args);
    if (tool == QLatin1String("set_modulator_params"))
        return opSetModulatorParams(args);
    if (tool == QLatin1String("bind_param"))
        return opBindParam(args);
    if (tool == QLatin1String("remove_node"))
        return opRemoveNode(args);
    return err("unknown_op", tool);
}

QJsonObject McpDispatcher::capture(const QJsonObject &args)
{
    if (!m_window)
        return textResult(err("not_found", QStringLiteral("No mixer window")), true);
    return m_window->mcpCaptureFrame(jsonBool(args.value(QStringLiteral("full"))));
}

NodeId resolveClip(AppController *w, const QJsonObject &args, QJsonObject *error)
{
    const QJsonValue v = args.value(QStringLiteral("clip"));
    qint64 id = 0;
    if (v.isDouble())
        id = v.toInteger();
    else if (v.isString()) {
        bool ok = false;
        id = v.toString().toLongLong(&ok);
        if (!ok)
            id = 0;
    }
    if (id <= 0) {
        if (error)
            *error = err("bad_args", QStringLiteral("clip is required"));
        return 0;
    }
    const ClipGraph *graph = w->clipGraph();
    const ClipGraph::Node *node = graph ? graph->node(static_cast<NodeId>(id)) : nullptr;
    if (!node || node->type != ClipGraph::NodeType::Input) {
        if (error)
            *error = err("not_found", QStringLiteral("No clip %1").arg(id));
        return 0;
    }
    return static_cast<NodeId>(id);
}

qint64 parseNodeId(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toInteger();
    if (v.isString()) {
        bool ok = false;
        const qint64 n = v.toString().toLongLong(&ok);
        return ok ? n : 0;
    }
    return 0;
}

int parseConnectionKind(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toInt(-1);
    const QString s = v.toString().trimmed().toLower();
    if (s.isEmpty() || s == QLatin1String("auto"))
        return -1;
    if (s == QLatin1String("chain") || s == QLatin1String("0"))
        return 0;
    if (s == QLatin1String("script_to_data") || s == QLatin1String("6"))
        return 6;
    if (s == QLatin1String("clip_to_shader_audio") || s == QLatin1String("5"))
        return 5;
    if (s == QLatin1String("clip_to_audio_analyzer") || s == QLatin1String("12"))
        return 12;
    if (s == QLatin1String("input_to_master") || s == QLatin1String("7"))
        return 7;
    if (s == QLatin1String("stream_to_mixer") || s == QLatin1String("9"))
        return 9;
    if (s == QLatin1String("mixer_to_output") || s == QLatin1String("10"))
        return 10;
    if (s == QLatin1String("audio_effect") || s == QLatin1String("11"))
        return 11;
    bool ok = false;
    const int n = s.toInt(&ok);
    return ok ? n : -2;
}

const ProcessEffectDescriptor *resolveEffect(const QJsonValue &v)
{
    if (v.isDouble())
        return ProcessEffects::byId(v.toInt());
    if (v.isString())
        return ProcessEffects::byName(v.toString());
    return nullptr;
}

static QString effectSlugOf(const ProcessEffectDescriptor &d)
{
    if (!d.slug.isEmpty())
        return d.slug;
    QString out;
    for (QChar c : d.name.toLower()) {
        if (c.isLetterOrNumber())
            out += c;
        else if (!out.isEmpty() && out.back() != QLatin1Char('_'))
            out += QLatin1Char('_');
    }
    while (out.endsWith(QLatin1Char('_')))
        out.chop(1);
    return out;
}

QJsonObject McpDispatcher::opListClips() const
{
    return m_window->mcpInspect(true, false, -1);
}

QJsonObject McpDispatcher::opListSourceKinds() const
{
    QJsonArray kinds;
    auto add = [&](const char *id, const char *hint) {
        kinds.append(QJsonObject{{QStringLiteral("id"), QString::fromUtf8(id)},
                                 {QStringLiteral("hint"), QString::fromUtf8(hint)}});
    };
    add("video", "File path — add_clip or add_source");
    add("image", "File path");
    add("audio", "File path");
    add("slideshow", "Folder path");
    add("camera", "list_cameras then camera/index");
    add("canvas", "w/h/fill/color");
    add("shader", "GLSL in code");
    add("svg_template", "template (built-in id or .svg path) + params");
    add("text", "Overlay string in text");
    add("ndi", "list_ndi_sources then ndi");
    return ok({{QStringLiteral("kinds"), kinds}});
}

QJsonObject McpDispatcher::opListCameras() const
{
    QJsonArray cameras;
    int i = 0;
    for (const CameraDeviceInfo &dev : CameraEnumerator::listDevices()) {
        cameras.append(QJsonObject{
            {QStringLiteral("index"), i++},
            {QStringLiteral("id"), dev.id},
            {QStringLiteral("label"), dev.label},
            {QStringLiteral("default"), dev.isDefault},
        });
    }
    return ok({{QStringLiteral("cameras"), cameras}});
}

QJsonObject McpDispatcher::opListNdiSources() const
{
    if (!NdiSource::isAvailable())
        return err("not_found", QStringLiteral("NDI is not available in this build"));
    QJsonArray sources;
    for (const QString &name : NdiSource::discoverSources())
        sources.append(name);
    return ok({{QStringLiteral("sources"), sources}});
}

QJsonObject McpDispatcher::opAddClip(const QJsonObject &args)
{
    const QString path = args.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty())
        return err("bad_args", QStringLiteral("path is required"));
    if (!QFileInfo::exists(path))
        return err("not_found", QStringLiteral("File does not exist: %1").arg(path));

    SourceDescriptor desc;
    if (ClipManager::isAudioPath(path))
        desc.kind = SourceDescriptor::Kind::AudioFile;
    else if (ImageSource::isStaticImageFile(path))
        desc.kind = SourceDescriptor::Kind::Image;
    else
        desc.kind = SourceDescriptor::Kind::VideoFile;
    desc.path = path;
    desc.displayName = args.value(QStringLiteral("name")).toString().trimmed();
    if (desc.displayName.isEmpty())
        desc.displayName = QFileInfo(path).completeBaseName();

    const NodeId node = m_window->addSourceFromDescriptor(desc, thumbFor(desc));
    if (!node)
        return err("not_found", QStringLiteral("Could not add clip"));
    return clipOk(m_window->clipGraph(), node);
}

QJsonObject McpDispatcher::opAddSource(const QJsonObject &args)
{
    const QString kindStr = args.value(QStringLiteral("kind")).toString();
    SourceDescriptor::Kind kind = SourceDescriptor::Kind::Canvas;
    if (!parseKind(kindStr, &kind))
        return err("bad_args", QStringLiteral("Unknown kind: %1").arg(kindStr));

    if (kind == SourceDescriptor::Kind::VideoFile || kind == SourceDescriptor::Kind::Image
        || kind == SourceDescriptor::Kind::AudioFile)
        return opAddClip(args);

    SourceDescriptor desc;
    desc.kind = kind;
    desc.displayName = args.value(QStringLiteral("name")).toString().trimmed();

    switch (kind) {
    case SourceDescriptor::Kind::Text: {
        const QString text = args.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            return err("bad_args", QStringLiteral("text is required for kind text"));
        desc.textTemplate = text;
        prism::TextStyle style;
        style.setPrimaryColor(parseColor(args.value(QStringLiteral("color")).toString(), Qt::white));
        desc.textStyleJson = TextSource::styleToJson(style);
        if (desc.displayName.isEmpty())
            desc.displayName = text.left(24);
        break;
    }
    case SourceDescriptor::Kind::Shape: {
        prism::ShapeStyle style;
        if (args.contains(QStringLiteral("color")))
            style.setSolidFill(parseColor(args.value(QStringLiteral("color")).toString(), Qt::white));
        desc.shapeStyleJson = ShapeSource::styleToJson(style);
        if (desc.displayName.isEmpty())
            desc.displayName = QStringLiteral("Shape");
        break;
    }
    case SourceDescriptor::Kind::SvgTemplate: {
        const QString tmpl = args.value(QStringLiteral("template")).toString().trimmed();
        if (tmpl.isEmpty())
            return err("bad_args", QStringLiteral("template is required for kind svg_template"));
        const QByteArray svg = prism::svgTemplateBytes(tmpl);
        if (svg.isEmpty())
            return err("bad_args", QStringLiteral("Unknown template or unreadable file: %1").arg(tmpl));
        desc.svgTemplateId = tmpl;
        desc.svgParamsJson = SvgTemplateSource::paramsToJson(args.value(QStringLiteral("params")).toObject());
        if (desc.displayName.isEmpty())
            desc.displayName = prism::svgTemplateInfo(tmpl, svg).name;
        break;
    }
    case SourceDescriptor::Kind::Shader: {
        QString code = args.value(QStringLiteral("code")).toString();
        if (code.trimmed().isEmpty())
            code = defaultShader();
        desc.shaderCode = code;
        if (desc.displayName.isEmpty())
            desc.displayName = QStringLiteral("Shader");
        break;
    }
    case SourceDescriptor::Kind::Canvas: {
        desc.canvasWidth = qMax(16, jsonInt(args.value(QStringLiteral("w")), 1280));
        desc.canvasHeight = qMax(16, jsonInt(args.value(QStringLiteral("h")), 720));
        const QString fill = args.value(QStringLiteral("fill")).toString().toLower();
        if (fill == QLatin1String("transparent"))
            desc.canvasFill = SourceDescriptor::CanvasFill::Transparent;
        else if (fill == QLatin1String("color"))
            desc.canvasFill = SourceDescriptor::CanvasFill::Color;
        else
            desc.canvasFill = SourceDescriptor::CanvasFill::Checkered;
        desc.color = parseColor(args.value(QStringLiteral("color")).toString(), Qt::white);
        if (desc.displayName.isEmpty())
            desc.displayName = QStringLiteral("Canvas %1x%2").arg(desc.canvasWidth).arg(desc.canvasHeight);
        break;
    }
    case SourceDescriptor::Kind::Camera: {
        QString camId = args.value(QStringLiteral("camera")).toString();
        const int index = jsonInt(args.value(QStringLiteral("index")), 0);
        const auto devices = CameraEnumerator::listDevices();
        if (!camId.isEmpty()) {
            desc.path = camId;
            for (const CameraDeviceInfo &dev : devices) {
                if (dev.id == camId) {
                    if (desc.displayName.isEmpty())
                        desc.displayName = dev.label;
                    break;
                }
            }
        } else if (index >= 0 && index < devices.size()) {
            desc.path = devices.at(index).id;
            desc.cameraIndex = index;
            if (desc.displayName.isEmpty())
                desc.displayName = devices.at(index).label;
        } else if (!devices.isEmpty()) {
            desc.path = devices.first().id;
            if (desc.displayName.isEmpty())
                desc.displayName = devices.first().label;
        }
        if (desc.displayName.isEmpty())
            desc.displayName = QStringLiteral("Camera");
        break;
    }
    case SourceDescriptor::Kind::Slideshow: {
        const QString path = args.value(QStringLiteral("path")).toString().trimmed();
        if (path.isEmpty() || !QDir(path).exists())
            return err("not_found", QStringLiteral("Slideshow folder missing"));
        desc.path = path;
        if (desc.displayName.isEmpty())
            desc.displayName = QFileInfo(path).fileName();
        break;
    }
    case SourceDescriptor::Kind::Ndi: {
        if (!NdiSource::isAvailable())
            return err("not_found", QStringLiteral("NDI is not available in this build"));
        const QString ndi = args.value(QStringLiteral("ndi")).toString().trimmed();
        if (ndi.isEmpty())
            return err("bad_args", QStringLiteral("ndi sender name is required"));
        desc.path = ndi;
        if (desc.displayName.isEmpty())
            desc.displayName = ndi;
        break;
    }
    case SourceDescriptor::Kind::Scene: {
        QJsonObject error;
        const Scene *target = resolveScene(m_window, args, &error);
        if (!target)
            return error;
        const ShowManager *show = m_window->showManager();
        if (show->wouldCreateSceneCycle(show->editedSceneId(), target->id()))
            return err("bad_args", QStringLiteral("Scene %1 would contain itself").arg(target->name()));
        desc.sceneId = target->id();
        if (desc.displayName.isEmpty())
            desc.displayName = target->name();
        break;
    }
    default:
        return err("bad_args", QStringLiteral("Unsupported kind: %1").arg(kindStr));
    }

    const NodeId node = m_window->addSourceFromDescriptor(desc, thumbFor(desc));
    if (!node)
        return err("not_found", QStringLiteral("Could not add source"));
    return clipOk(m_window->clipGraph(), node);
}

QJsonObject McpDispatcher::opRemoveClip(const QJsonObject &args)
{
    QJsonObject error;
    const NodeId node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    const NodeId id = node;
    if (!m_window->mcpRemoveClip(id))
        return err("not_found", QStringLiteral("Could not remove clip"));
    return ok({{QStringLiteral("id"), QString::number(id)}});
}

QJsonObject McpDispatcher::opRenameClip(const QJsonObject &args)
{
    QJsonObject error;
    const NodeId node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    const QString name = args.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty())
        return err("bad_args", QStringLiteral("name is required"));
    if (!m_window->mcpRenameClip(node, name))
        return err("not_found");
    return clipOk(m_window->clipGraph(), node);
}

QJsonObject McpDispatcher::opSetText(const QJsonObject &args)
{
    QJsonObject error;
    const NodeId node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    if (m_window->clipGraph()->sourceDescriptor(node).kind != SourceDescriptor::Kind::Text)
        return err("type_mismatch", QStringLiteral("Clip is not a text source"));
    SourceDescriptor desc = m_window->clipGraph()->sourceDescriptor(node);
    desc.textTemplate = args.value(QStringLiteral("text")).toString();
    if (args.contains(QStringLiteral("color"))) {
        prism::TextStyle style = TextSource::styleFromDescriptor(desc);
        style.setPrimaryColor(parseColor(args.value(QStringLiteral("color")).toString(), style.primaryColor()));
        desc.textStyleJson = TextSource::styleToJson(style);
    }
    if (!m_window->mcpUpdateSource(node, desc, thumbFor(desc)))
        return err("not_found");
    return clipOk(m_window->clipGraph(), node);
}

QJsonObject McpDispatcher::opSetSvgParams(const QJsonObject &args)
{
    QJsonObject error;
    const NodeId node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    if (m_window->clipGraph()->sourceDescriptor(node).kind != SourceDescriptor::Kind::SvgTemplate)
        return err("type_mismatch", QStringLiteral("Clip is not an SVG template source"));
    SourceDescriptor desc = m_window->clipGraph()->sourceDescriptor(node);
    QJsonObject params = SvgTemplateSource::paramsFromDescriptor(desc);
    const QJsonObject changes = args.value(QStringLiteral("params")).toObject();
    for (auto it = changes.begin(); it != changes.end(); ++it)
        params.insert(it.key(), it.value());
    desc.svgParamsJson = SvgTemplateSource::paramsToJson(params);
    if (!m_window->mcpUpdateSource(node, desc, thumbFor(desc)))
        return err("not_found");
    return clipOk(m_window->clipGraph(), node);
}

QJsonObject McpDispatcher::opSetShader(const QJsonObject &args)
{
    QJsonObject error;
    const NodeId node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    if (m_window->clipGraph()->sourceDescriptor(node).kind != SourceDescriptor::Kind::Shader)
        return err("type_mismatch", QStringLiteral("Clip is not a shader source"));
    SourceDescriptor desc = m_window->clipGraph()->sourceDescriptor(node);
    desc.shaderCode = args.value(QStringLiteral("code")).toString();
    if (!m_window->mcpUpdateSource(node, desc, thumbFor(desc)))
        return err("not_found");
    return clipOk(m_window->clipGraph(), node);
}

QJsonObject McpDispatcher::opSetSceneSource(const QJsonObject &args)
{
    QJsonObject error;
    const NodeId node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    if (m_window->clipGraph()->sourceDescriptor(node).kind != SourceDescriptor::Kind::Scene)
        return err("type_mismatch", QStringLiteral("Clip is not a scene source"));
    const Scene *target = resolveScene(m_window, args, &error);
    if (!target)
        return error;
    const ShowManager *show = m_window->showManager();
    if (show->wouldCreateSceneCycle(show->editedSceneId(), target->id()))
        return err("bad_args", QStringLiteral("Scene %1 would contain itself").arg(target->name()));
    SourceDescriptor desc = m_window->clipGraph()->sourceDescriptor(node);
    desc.sceneId = target->id();
    desc.displayName = target->name();
    if (!m_window->mcpUpdateSource(node, desc, {}))
        return err("not_found");
    return clipOk(m_window->clipGraph(), node);
}

bool McpDispatcher::parseDeck(const QJsonObject &args, Deck &deck, QJsonObject *error)
{
    const QString name = args.value(QStringLiteral("deck")).toString().trimmed().toLower();
    if (name == QLatin1String("live"))
        deck = Deck::Live;
    else if (name == QLatin1String("staging"))
        deck = Deck::Staging;
    else {
        *error = err("bad_args", QStringLiteral("deck must be live or staging"));
        return false;
    }
    return true;
}

namespace {
QString deckName(Deck deck)
{
    return deck == Deck::Live ? QStringLiteral("live") : QStringLiteral("staging");
}
}

namespace {
// "scene" is a scene id or a 1-based position in the scene list.
Scene *resolveScene(AppController *w, const QJsonObject &args, QJsonObject *error)
{
    const QJsonValue v = args.value(QStringLiteral("scene"));
    Scene *scene = nullptr;
    if (v.isDouble()) {
        const int index = v.toInt() - 1;
        const QList<Scene *> &scenes = w->showManager()->scenes();
        scene = index >= 0 && index < scenes.size() ? scenes.at(index) : nullptr;
    } else if (v.isString()) {
        scene = w->showManager()->scene(v.toString());
    }
    if (!scene && error)
        *error = err("not_found", QStringLiteral("No such scene"));
    return scene;
}

QJsonObject sceneJson(const AppController *w, const Scene *scene)
{
    const ShowManager *show = w->showManager();
    const QVariantMap info = w->sceneInfo(scene->id());
    return {{QStringLiteral("id"), scene->id()},
            {QStringLiteral("index"), show->indexOf(scene->id()) + 1},
            {QStringLiteral("name"), scene->name()},
            {QStringLiteral("live"), scene->id() == show->liveSceneId()},
            {QStringLiteral("staging"), scene->id() == show->stagingSceneId()},
            {QStringLiteral("edited"), show->inScene() && scene->id() == show->editedSceneId()},
            {QStringLiteral("endAction"), Scene::endActionName(scene->endAction())},
            {QStringLiteral("transition"), QJsonValue::fromVariant(info.value(QStringLiteral("transition")))},
            {QStringLiteral("autoAdvanceMs"), scene->autoAdvanceMs()},
            {QStringLiteral("advanceOnMediaEnd"), scene->advanceOnMediaEnd()},
            {QStringLiteral("hotkey"), info.value(QStringLiteral("hotkey")).toString()},
            {QStringLiteral("warning"), info.value(QStringLiteral("warning")).toString()}};
}

int transitionModeIndex(AppController *w, const QJsonValue &v)
{
    const QStringList modes = w->transitionModeNames();
    if (v.isString()) {
        for (int i = 0; i < modes.size(); ++i) {
            if (modes.at(i).compare(v.toString().trimmed(), Qt::CaseInsensitive) == 0)
                return i;
        }
        return -1;
    }
    const int index = v.toInt(-1);
    return index >= 0 && index < modes.size() ? index : -1;
}
}

QJsonObject McpDispatcher::opListScenes() const
{
    QJsonArray scenes;
    for (const Scene *scene : m_window->showManager()->scenes())
        scenes.append(sceneJson(m_window, scene));
    return ok({{QStringLiteral("scenes"), scenes}});
}

QJsonObject McpDispatcher::opStageScene(const QJsonObject &args)
{
    QJsonObject error;
    Scene *scene = resolveScene(m_window, args, &error);
    if (!scene)
        return error;
    if (!m_window->stageScene(scene->id()))
        return err("conflict", QStringLiteral("Scene is Live or a take is running"));
    m_window->mcpBumpRevision();
    return ok(sceneJson(m_window, scene));
}

QJsonObject McpDispatcher::opAddScene(const QJsonObject &args)
{
    const QString id = m_window->addScene(args.value(QStringLiteral("name")).toString().trimmed());
    m_window->mcpBumpRevision();
    return ok(sceneJson(m_window, m_window->showManager()->scene(id)));
}

QJsonObject McpDispatcher::opEnterScene(const QJsonObject &args)
{
    QJsonObject error;
    Scene *scene = resolveScene(m_window, args, &error);
    if (!scene)
        return error;
    m_window->enterScene(scene->id());
    m_window->mcpBumpRevision();
    return ok(sceneJson(m_window, scene));
}

QJsonObject McpDispatcher::opTakeScene(const QJsonObject &args)
{
    QJsonObject error;
    Scene *scene = resolveScene(m_window, args, &error);
    if (!scene)
        return error;
    if (!m_window->stageAndTake(scene->id()))
        return err("conflict", QStringLiteral("Scene is Live or a take is running"));
    m_window->mcpBumpRevision();
    return ok(sceneJson(m_window, scene));
}

QJsonObject McpDispatcher::opRemoveScene(const QJsonObject &args)
{
    QJsonObject error;
    Scene *scene = resolveScene(m_window, args, &error);
    if (!scene)
        return error;
    const QString id = scene->id();
    if (!m_window->removeScene(id))
        return err("conflict", QStringLiteral("The Live scene and the last scene cannot be removed, nor during a take"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("id"), id}});
}

QJsonObject McpDispatcher::opReorderScene(const QJsonObject &args)
{
    QJsonObject error;
    Scene *scene = resolveScene(m_window, args, &error);
    if (!scene)
        return error;
    const int to = jsonInt(args.value(QStringLiteral("to")), 0);
    if (to < 1 || to > m_window->showManager()->scenes().size())
        return err("bad_args", QStringLiteral("to must be a 1-based position in list_scenes"));
    if (!m_window->moveScene(scene->id(), to - 1))
        return err("conflict", QStringLiteral("Scene could not be moved"));
    m_window->mcpBumpRevision();
    return ok(sceneJson(m_window, scene));
}

QJsonObject McpDispatcher::opDuplicateScene(const QJsonObject &args)
{
    QJsonObject error;
    Scene *scene = resolveScene(m_window, args, &error);
    if (!scene)
        return error;
    const QString id = m_window->duplicateScene(scene->id());
    const Scene *copy = m_window->showManager()->scene(id);
    if (!copy)
        return err("conflict", QStringLiteral("Scene could not be duplicated"));
    m_window->mcpBumpRevision();
    return ok(sceneJson(m_window, copy));
}

QJsonObject McpDispatcher::opExitScene()
{
    m_window->exitScene();
    m_window->mcpBumpRevision();
    return ok();
}

QJsonObject McpDispatcher::opSetSceneProps(const QJsonObject &args)
{
    QJsonObject error;
    Scene *scene = resolveScene(m_window, args, &error);
    if (!scene)
        return error;
    QVariantMap props;
    for (const char *key : {"name", "color", "notes", "endAction", "autoAdvanceMs", "advanceOnMediaEnd", "hotkey"}) {
        const QString k = QString::fromLatin1(key);
        if (args.contains(k))
            props.insert(k, args.value(k).toVariant());
    }
    const QString transitionKey = QStringLiteral("transition");
    if (args.contains(transitionKey)) {
        const QJsonValue t = args.value(transitionKey);
        if (t.isNull()) {
            props.insert(transitionKey, QVariant());
        } else {
            const QJsonObject o = t.toObject();
            const int mode = transitionModeIndex(m_window, o.value(QStringLiteral("mode")));
            if (mode < 0)
                return err("bad_args", QStringLiteral("transition.mode must be a name or index from list_transitions"));
            props.insert(transitionKey, QVariantMap{{QStringLiteral("mode"), mode},
                                                    {QStringLiteral("durationMs"),
                                                     jsonInt(o.value(QStringLiteral("durationMs")), 1000)}});
        }
    }
    m_window->setSceneProps(scene->id(), props);
    m_window->mcpBumpRevision();
    return ok(sceneJson(m_window, scene));
}

QJsonObject McpDispatcher::opSetDefaultTransition(const QJsonObject &args)
{
    const int mode = transitionModeIndex(m_window, args.value(QStringLiteral("mode")));
    if (mode < 0)
        return err("bad_args", QStringLiteral("mode must be a name or index from list_transitions"));
    const int durationMs = qMax(0, jsonInt(args.value(QStringLiteral("durationMs")), 1000));
    m_window->showManager()->setDefaultTransition(TransitionSpec{mode, durationMs});
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("mode"), m_window->transitionModeNames().at(mode)},
               {QStringLiteral("durationMs"), durationMs}});
}

QJsonObject McpDispatcher::opPreflight() const
{
    QJsonArray issues;
    for (const QVariant &v : m_window->preflight())
        issues.append(QJsonObject::fromVariantMap(v.toMap()));
    return ok({{QStringLiteral("issues"), issues}});
}

QJsonObject McpDispatcher::opPlayDeck(const QJsonObject &args, bool play)
{
    Deck deck;
    QJsonObject error;
    if (!parseDeck(args, deck, &error))
        return error;
    m_window->playDeck(static_cast<int>(deck), play);
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("deck"), deckName(deck)}, {QStringLiteral("playing"), play}});
}

QJsonObject McpDispatcher::opSeekDeck(const QJsonObject &args)
{
    Deck deck;
    QJsonObject error;
    if (!parseDeck(args, deck, &error))
        return error;
    if (!args.contains(QStringLiteral("at")))
        return err("bad_args", QStringLiteral("at is required"));
    const double at = jsonNumber(args.value(QStringLiteral("at")), 0);
    m_window->seekDeck(static_cast<int>(deck), qMax(0.0, at));
    return ok({{QStringLiteral("deck"), deckName(deck)}, {QStringLiteral("at"), at}});
}

QJsonObject McpDispatcher::opSetSpeed(const QJsonObject &args)
{
    QJsonObject error;
    const NodeId node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    if (!args.contains(QStringLiteral("speed")))
        return err("bad_args", QStringLiteral("speed is required"));
    ClipGraph *graph = m_window->clipGraph();
    if (!graph->sourceDescriptor(node).hasSpeedControl())
        return err("type_mismatch", QStringLiteral("Clip has no speed control"));
    ClipSettings settings = graph->clipSettings(node);
    settings.speed = qBound(0.25, jsonNumber(args.value(QStringLiteral("speed")), 1.0), 4.0);
    graph->setClipSettings(node, settings);
    m_window->mcpBumpRevision();
    return clipOk(graph, node, {{QStringLiteral("speed"), settings.speed}});
}

QJsonObject McpDispatcher::opSetFader(const QJsonObject &args)
{
    if (!args.contains(QStringLiteral("value")))
        return err("bad_args", QStringLiteral("value is required"));
    const int value = qBound(0, jsonInt(args.value(QStringLiteral("value")), 0), 100);
    m_window->setFaderValue(value);
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("value"), value}});
}

QJsonObject McpDispatcher::opCut()
{
    m_window->cut();
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("fader"), m_window->faderValue()}});
}

QJsonObject McpDispatcher::opTake()
{
    m_window->take();
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("fader"), m_window->faderValue()}});
}

QJsonObject McpDispatcher::opListTransitions() const
{
    auto *t = m_window->transitionController();
    const QStringList modes = t ? t->transitionModeNames() : QStringList{};
    QJsonArray arr;
    for (const QString &name : modes)
        arr.append(name);
    return ok({{QStringLiteral("modes"), arr},
               {QStringLiteral("index"), t ? t->currentModeIndex() : 0},
               {QStringLiteral("current"),
                (t && t->currentModeIndex() >= 0 && t->currentModeIndex() < modes.size())
                    ? modes.at(t->currentModeIndex())
                    : QString()},
               {QStringLiteral("duration"), t ? t->currentDurationSecs() : 0.0}});
}

QJsonObject McpDispatcher::opSetTransition(const QJsonObject &args)
{
    auto *t = m_window->transitionController();
    if (!t)
        return err("not_found", QStringLiteral("No transition controller"));
    const QStringList modes = t->transitionModeNames();
    int index = jsonInt(args.value(QStringLiteral("index")), -1);
    const QString mode = args.value(QStringLiteral("mode")).toString().trimmed();
    if (!mode.isEmpty()) {
        index = -1;
        for (int i = 0; i < modes.size(); ++i) {
            if (modes.at(i).compare(mode, Qt::CaseInsensitive) == 0) {
                index = i;
                break;
            }
        }
        if (index < 0)
            return err("not_found", QStringLiteral("Unknown transition: %1").arg(mode));
    }
    if (index < 0 || index >= modes.size())
        return err("bad_args", QStringLiteral("Pass mode or index"));
    t->setTransitionModeIndex(index);
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("index"), index}, {QStringLiteral("mode"), modes.at(index)}});
}

QJsonObject McpDispatcher::opSetDuration(const QJsonObject &args)
{
    if (!args.contains(QStringLiteral("seconds")))
        return err("bad_args", QStringLiteral("seconds is required"));
    const double secs = qBound(0.0, jsonNumber(args.value(QStringLiteral("seconds")), 1.0), 30.0);
    if (auto *t = m_window->transitionController())
        t->setTransitionDuration(secs);
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("seconds"), secs}});
}

QJsonObject McpDispatcher::opSetPanic(const QJsonObject &args)
{
    const QString mode = args.value(QStringLiteral("mode")).toString().trimmed().toLower();
    if (!m_window->mcpSetPanic(mode))
        return err("bad_args", QStringLiteral("mode must be none, blackout, freeze, or stay_tuned"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("mode"), m_window->mcpPanicMode()}});
}

QJsonObject McpDispatcher::opStartRecording(const QJsonObject &args)
{
    QString error;
    const QString dir = args.value(QStringLiteral("dir")).toString().trimmed();
    if (!m_window->mcpStartRecording(dir, &error))
        return err("bad_args", error);
    auto *hub = m_window->outputHub();
    return ok({{QStringLiteral("dir"), hub ? hub->outputDir() : dir},
               {QStringLiteral("active"), hub && hub->isRecording()}});
}

QJsonObject McpDispatcher::opStopRecording()
{
    if (auto *hub = m_window->outputHub())
        hub->stopAllRecording();
    m_window->mcpBumpRevision();
    return ok();
}

QJsonObject McpDispatcher::opRecordingStatus() const
{
    auto *hub = m_window->outputHub();
    QJsonArray tracks;
    if (hub) {
        for (const QString &label : hub->activeRecordingTrackLabels())
            tracks.append(label);
    }
    return ok({{QStringLiteral("active"), hub && hub->isRecording()},
               {QStringLiteral("elapsed"), hub ? hub->longestActiveRecordingMs() / 1000.0 : 0.0},
               {QStringLiteral("tracks"), tracks},
               {QStringLiteral("dir"), hub ? hub->outputDir() : QString()}});
}

QJsonObject McpDispatcher::opSetNdi(const QJsonObject &args)
{
    auto *hub = m_window->outputHub();
    if (!hub)
        return err("not_found");
    const bool enabled = jsonBool(args.value(QStringLiteral("enabled")));
    const QString name = args.value(QStringLiteral("name")).toString();
    if (enabled && !hub->ndiAvailable())
        return err("not_found", QStringLiteral("NDI is not available in this build"));
    if (!hub->setNdiOutputEnabled(enabled, name))
        return err("conflict", QStringLiteral("Could not change NDI output"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("enabled"), hub->ndiOutputEnabled()},
               {QStringLiteral("name"), hub->ndiStreamName()}});
}

QJsonObject McpDispatcher::opSetVirtualCamera(const QJsonObject &args)
{
    auto *hub = m_window->outputHub();
    if (!hub)
        return err("not_found");
    const bool enabled = jsonBool(args.value(QStringLiteral("enabled")));
    if (enabled && !hub->virtualCameraAvailable())
        return err("not_found", QStringLiteral("Virtual camera is not available"));
    if (!hub->setVirtualCameraEnabled(enabled))
        return err("conflict", QStringLiteral("Could not change virtual camera"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("enabled"), hub->virtualCameraEnabled()}});
}

QJsonObject McpDispatcher::opSaveSession(const QJsonObject &args)
{
    const QString path = args.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty())
        return err("bad_args", QStringLiteral("path is required"));
    if (!m_window->mcpSaveSession(path))
        return err("conflict", QStringLiteral("Could not write %1").arg(path));
    return ok({{QStringLiteral("path"), path}});
}

QJsonObject McpDispatcher::opLoadSession(const QJsonObject &args)
{
    const QString path = args.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty())
        return err("bad_args", QStringLiteral("path is required"));
    if (!QFileInfo::exists(path))
        return err("not_found", path);
    if (!m_window->mcpLoadSession(path))
        return err("conflict", QStringLiteral("Could not load %1").arg(path));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("path"), path}});
}

QJsonObject McpDispatcher::opListProcessEffects() const
{
    QJsonArray effects;
    for (const ProcessEffectDescriptor &d : ProcessEffects::all()) {
        if (!d.available)
            continue;
        effects.append(QJsonObject{
            {QStringLiteral("id"), d.id},
            {QStringLiteral("name"), d.name},
            {QStringLiteral("slug"), effectSlugOf(d)},
            {QStringLiteral("params"), d.defaultParams},
        });
    }
    return ok({{QStringLiteral("effects"), effects}});
}

QJsonObject McpDispatcher::opListNodes() const
{
    auto *editor = m_window->clipGraph();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    QJsonObject snap = editor->snapshot([this](NodeId id) { return m_window->modulatorValue(id); });
    snap.insert(QStringLiteral("ok"), true);
    return snap;
}

QJsonObject McpDispatcher::opAddProcessNode(const QJsonObject &args)
{
    auto *editor = m_window->clipGraph();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const ProcessEffectDescriptor *desc = resolveEffect(args.value(QStringLiteral("effect")));
    if (!desc)
        return err("not_found", QStringLiteral("Unknown effect — call list_process_effects"));
    const QJsonObject params = args.value(QStringLiteral("params")).toObject();
    const bool hasX = args.contains(QStringLiteral("x"));
    const bool hasY = args.contains(QStringLiteral("y"));
    const double x = hasX ? jsonNumber(args.value(QStringLiteral("x")), 0) : qQNaN();
    const double y = hasY ? jsonNumber(args.value(QStringLiteral("y")), 0) : qQNaN();
    const NodeId id = editor->addNode(ClipGraph::NodeType::Process, QPointF(x, y),
                                      {{QStringLiteral("effect"), desc->id},
                                       {QStringLiteral("params"), params.toVariantMap()}});
    if (!id)
        return err("conflict", QStringLiteral("Could not add process node"));
    m_window->mcpBumpRevision();
    const QJsonObject snap = editor->snapshot();
    QJsonObject row;
    for (const QJsonValue &v : snap.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject n = v.toObject();
        if (n.value(QStringLiteral("id")).toString() == QString::number(id)) {
            row = n;
            break;
        }
    }
    row.insert(QStringLiteral("ok"), true);
    row.insert(QStringLiteral("id"), QString::number(id));
    row.insert(QStringLiteral("effect"), desc->id);
    row.insert(QStringLiteral("name"), desc->name);
    return row;
}

QJsonObject McpDispatcher::opAddLayerNode(const QJsonObject &args)
{
    auto *editor = m_window->clipGraph();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const double x = args.contains(QStringLiteral("x"))
                         ? jsonNumber(args.value(QStringLiteral("x")), 0)
                         : qQNaN();
    const double y = args.contains(QStringLiteral("y"))
                         ? jsonNumber(args.value(QStringLiteral("y")), 0)
                         : qQNaN();
    const NodeId id = editor->addNode(ClipGraph::NodeType::Layer, QPointF(x, y));
    if (!id)
        return err("conflict", QStringLiteral("Could not add layer node"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("id"), QString::number(id)},
               {QStringLiteral("type"), QStringLiteral("layer")}});
}

QJsonObject McpDispatcher::opConnect(const QJsonObject &args)
{
    auto *editor = m_window->clipGraph();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const NodeId from = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("from"))));
    const NodeId to = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("to"))));
    if (!from || !to)
        return err("bad_args", QStringLiteral("from and to are required node ids"));
    if (!editor->hasNode(from))
        return err("not_found", QStringLiteral("No node %1").arg(from));
    if (!editor->hasNode(to))
        return err("not_found", QStringLiteral("No node %1").arg(to));
    const int kind = parseConnectionKind(args.value(QStringLiteral("kind")));
    if (kind == -2)
        return err("bad_args", QStringLiteral("Unknown connection kind"));
    const int toPort = resolveToPort(editor, to, args.value(QStringLiteral("to_port")));
    if (toPort == -2)
        return err("bad_args", QStringLiteral("Unknown to_port name. Use a slot number or an input "
                                              "port name (list_nodes shows them)."));
    if (!editor->connectNodes(from, to, kind, toPort))
        return err("conflict",
                   QStringLiteral("Could not connect those ports. Output cannot take a video "
                                  "chain and a Switcher at the same time — disconnect the other Output "
                                  "input first, or splice into an existing chain."));
    m_window->mcpBumpRevision();
    QString kindName;
    for (const QJsonValue &v : editor->snapshot().value(QStringLiteral("connections")).toArray()) {
        const QJsonObject c = v.toObject();
        if (c.value(QStringLiteral("from")).toString() == QString::number(from)
            && c.value(QStringLiteral("to")).toString() == QString::number(to)) {
            kindName = c.value(QStringLiteral("kindName")).toString();
            break;
        }
    }
    return ok({{QStringLiteral("from"), QString::number(from)},
               {QStringLiteral("to"), QString::number(to)},
               {QStringLiteral("kindName"), kindName}});
}

QJsonObject McpDispatcher::opDisconnect(const QJsonObject &args)
{
    auto *editor = m_window->clipGraph();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const NodeId from = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("from"))));
    const NodeId to = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("to"))));
    if (!from || !to)
        return err("bad_args", QStringLiteral("from and to are required node ids"));
    const int toPort = resolveToPort(editor, to, args.value(QStringLiteral("to_port")));
    if (toPort == -2)
        return err("bad_args", QStringLiteral("Unknown to_port name."));
    if (!editor->disconnectNodes(from, to, toPort))
        return err("not_found", QStringLiteral("No such connection"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("from"), QString::number(from)},
               {QStringLiteral("to"), QString::number(to)}});
}

QJsonObject McpDispatcher::opSetProcessParams(const QJsonObject &args)
{
    auto *editor = m_window->clipGraph();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const NodeId id = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("node"))));
    if (!id)
        return err("bad_args", QStringLiteral("node is required"));
    if (!editor->hasNode(id))
        return err("not_found", QStringLiteral("No node %1").arg(id));
    const QJsonObject params = args.value(QStringLiteral("params")).toObject();
    if (params.isEmpty())
        return err("bad_args", QStringLiteral("params is required"));
    if (editor->node(id)->type != ClipGraph::NodeType::Process
        || !editor->setEffectParams(id, params.toVariantMap(), true))
        return err("type_mismatch", QStringLiteral("Node is not a process node"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("id"), QString::number(id)}, {QStringLiteral("params"), params}});
}

namespace {

const QStringList kModulatorKinds = {
    QStringLiteral("lfo"),     QStringLiteral("envelope"), QStringLiteral("map_range"),
    QStringLiteral("smooth"),  QStringLiteral("random"),   QStringLiteral("trigger"),
    QStringLiteral("counter"), QStringLiteral("toggle"),   QStringLiteral("math")};

// -1 when the value is neither a kind name nor an index.
int modulatorKindIndex(const QJsonValue &v)
{
    if (v.isString())
        return kModulatorKinds.indexOf(v.toString().toLower());
    if (v.isDouble() && v.toInt() >= 0 && v.toInt() < kModulatorKinds.size())
        return v.toInt();
    return -1;
}

} // namespace

QJsonObject McpDispatcher::opAddModulatorNode(const QJsonObject &args)
{
    auto *editor = m_window->clipGraph();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    QVariantMap params = args.value(QStringLiteral("params")).toObject().toVariantMap();
    const QJsonValue kindArg = args.value(QStringLiteral("kind"));
    if (!kindArg.isUndefined()) {
        const int kind = modulatorKindIndex(kindArg);
        if (kind < 0)
            return err("bad_args", QStringLiteral("kind must be one of: %1").arg(kModulatorKinds.join(QStringLiteral(", "))));
        params.insert(QStringLiteral("kind"), kind);
    }
    const double x = args.contains(QStringLiteral("x")) ? jsonNumber(args.value(QStringLiteral("x")), 0) : qQNaN();
    const double y = args.contains(QStringLiteral("y")) ? jsonNumber(args.value(QStringLiteral("y")), 0) : qQNaN();
    const NodeId id = editor->addNode(ClipGraph::NodeType::Modulator, QPointF(x, y), params);
    if (!id)
        return err("conflict", QStringLiteral("Could not add modulator node"));
    m_window->mcpBumpRevision();
    const int kind = editor->node(id)->params.value(QStringLiteral("kind")).toInt();
    return ok({{QStringLiteral("id"), QString::number(id)},
               {QStringLiteral("type"), QStringLiteral("modulator")},
               {QStringLiteral("kind"), kModulatorKinds.value(kind)}});
}

QJsonObject McpDispatcher::opSetModulatorParams(const QJsonObject &args)
{
    auto *editor = m_window->clipGraph();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const NodeId id = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("node"))));
    if (!id)
        return err("bad_args", QStringLiteral("node is required"));
    if (!editor->hasNode(id))
        return err("not_found", QStringLiteral("No node %1").arg(id));
    if (editor->node(id)->type != ClipGraph::NodeType::Modulator)
        return err("type_mismatch", QStringLiteral("Node is not a modulator node"));
    QVariantMap params = args.value(QStringLiteral("params")).toObject().toVariantMap();
    const QJsonValue kindArg = args.value(QStringLiteral("kind"));
    if (!kindArg.isUndefined()) {
        const int kind = modulatorKindIndex(kindArg);
        if (kind < 0)
            return err("bad_args", QStringLiteral("kind must be one of: %1").arg(kModulatorKinds.join(QStringLiteral(", "))));
        params.insert(QStringLiteral("kind"), kind);
    }
    if (params.isEmpty())
        return err("bad_args", QStringLiteral("params or kind is required"));
    editor->setParams(id, params, true);
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("id"), QString::number(id)}});
}

QJsonObject McpDispatcher::opBindParam(const QJsonObject &args)
{
    auto *editor = m_window->clipGraph();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const NodeId id = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("node"))));
    if (!id)
        return err("bad_args", QStringLiteral("node is required"));
    const ClipGraph::Node *node = editor->node(id);
    if (!node)
        return err("not_found", QStringLiteral("No node %1").arg(id));
    QString param = args.value(QStringLiteral("param")).toString();
    if (param.isEmpty())
        return err("bad_args", QStringLiteral("param is required"));
    if (node->type == ClipGraph::NodeType::Process && !param.contains(QLatin1Char('.')))
        param.prepend(QStringLiteral("params."));

    QVariantMap exprs = node->params.value(QStringLiteral("expressions")).toMap();
    const QJsonValue modArg = args.value(QStringLiteral("modulator"));
    NodeId modId = 0;
    if (modArg.isUndefined() || modArg.isNull()) {
        exprs.remove(param);
    } else {
        modId = static_cast<NodeId>(parseNodeId(modArg));
        const ClipGraph::Node *mod = editor->node(modId);
        if (!mod)
            return err("not_found", QStringLiteral("No node %1").arg(modId));
        if (mod->type != ClipGraph::NodeType::Modulator)
            return err("type_mismatch", QStringLiteral("modulator must be a modulator node"));
        exprs.insert(param, QStringLiteral("mod(%1)").arg(modId));
    }
    editor->setParams(id, {{QStringLiteral("expressions"), exprs}}, true);
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("node"), QString::number(id)},
               {QStringLiteral("param"), param},
               {QStringLiteral("modulator"), modId ? QString::number(modId) : QString()}});
}

QJsonObject McpDispatcher::opRemoveNode(const QJsonObject &args)
{
    auto *editor = m_window->clipGraph();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    QJsonValue nodeVal = args.value(QStringLiteral("node"));
    if (nodeVal.isUndefined() || nodeVal.isNull())
        nodeVal = args.value(QStringLiteral("clip"));
    const NodeId id = static_cast<NodeId>(parseNodeId(nodeVal));
    if (!id)
        return err("bad_args", QStringLiteral("node is required"));
    if (!editor->hasNode(id))
        return err("not_found", QStringLiteral("No node %1").arg(id));
    if (id == editor->outputNodeId())
        return err("conflict", QStringLiteral("Output node cannot be removed while the graph has other nodes"));
    editor->removeNode(id);
    if (editor->hasNode(id))
        return err("conflict", QStringLiteral("Could not remove node"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("id"), QString::number(id)}});
}

} // namespace prism::mcp
