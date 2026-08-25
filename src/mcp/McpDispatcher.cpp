#include "mcp/McpDispatcher.h"
#include "mcp/McpCatalog.h"
#include "mcp/McpJson.h"

#include "core/media/ThumbnailExtractor.h"
#include "core/project/ClipManager.h"
#include "core/sources/ImageSource.h"
#include "core/sources/NdiSource.h"
#include "ui/canvas/VideoWidget.h"
#include "ui/common/CameraEnumerator.h"
#include "ui/common/ThumbHelper.h"
#include "ui/mainwindow/MainWindow.h"
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/nodes/ClipNodeModel.h"
#include "ui/nodes/ProcessEffects.h"
#include "ui/output/OutputHub.h"
#include "ui/output/OutputWindow.h"
#include "ui/session/SessionManager.h"
#include "ui/transitions/TransitionController.h"

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
    case K::Html:       return QStringLiteral("html");
    case K::Ndi:        return QStringLiteral("ndi");
    case K::WebRtc:     return QStringLiteral("webrtc");
    case K::Text:       return QStringLiteral("text");
    case K::AudioFile:  return QStringLiteral("audio");
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
    else if (k == QLatin1String("html"))
        *out = K::Html;
    else if (k == QLatin1String("text"))
        *out = K::Text;
    else if (k == QLatin1String("ndi"))
        *out = K::Ndi;
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

QPixmap thumbFor(const SourceDescriptor &desc)
{
    using K = SourceDescriptor::Kind;
    switch (desc.kind) {
    case K::VideoFile:
    case K::Image:
    case K::AudioFile:
        if (!desc.path.isEmpty())
            return ThumbnailExtractor::extract(desc.path, 110, 65);
        break;
    case K::Canvas:
        return ThumbHelper::makeCanvasThumb(QStringLiteral("%1x%2").arg(desc.canvasWidth).arg(desc.canvasHeight),
                                            desc.canvasFill, desc.color);
    case K::Shader:
        return ThumbHelper::makeShaderThumb(desc.shaderCode);
    case K::Html:
        return ThumbHelper::makeHtmlThumb(desc.htmlContent, desc.path);
    case K::Text:
        return ThumbHelper::makeTextThumb(desc.textTemplate, desc.color);
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

QJsonObject clipOk(ClipNodeModel *node, QJsonObject extra = {})
{
    extra.insert(QStringLiteral("ok"), true);
    extra.insert(QStringLiteral("id"), QString::number(node->nodeId()));
    extra.insert(QStringLiteral("name"), node->sourceName());
    extra.insert(QStringLiteral("kind"), kindName(node->sourceDescriptor().kind));
    return extra;
}

} // namespace

McpDispatcher::McpDispatcher(MainWindow *window)
    : m_window(window)
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
    if (tool == QLatin1String("set_html"))
        return opSetHtml(args);
    if (tool == QLatin1String("set_shader"))
        return opSetShader(args);
    if (tool == QLatin1String("select_a"))
        return opSelectDeck(args, true);
    if (tool == QLatin1String("select_b"))
        return opSelectDeck(args, false);
    if (tool == QLatin1String("play_a"))
        return opPlayDeck(true, true);
    if (tool == QLatin1String("pause_a"))
        return opPlayDeck(true, false);
    if (tool == QLatin1String("play_b"))
        return opPlayDeck(false, true);
    if (tool == QLatin1String("pause_b"))
        return opPlayDeck(false, false);
    if (tool == QLatin1String("seek_a"))
        return opSeekDeck(args, true);
    if (tool == QLatin1String("seek_b"))
        return opSeekDeck(args, false);
    if (tool == QLatin1String("set_speed"))
        return opSetSpeed(args);
    if (tool == QLatin1String("set_fader"))
        return opSetFader(args);
    if (tool == QLatin1String("cut"))
        return opCut();
    if (tool == QLatin1String("auto_transition"))
        return opAuto();
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
    if (tool == QLatin1String("add_ab_select"))
        return opAddAbSelect(args);
    if (tool == QLatin1String("connect"))
        return opConnect(args);
    if (tool == QLatin1String("disconnect"))
        return opDisconnect(args);
    if (tool == QLatin1String("set_process_params"))
        return opSetProcessParams(args);
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

ClipNodeModel *resolveClip(MainWindow *w, const QJsonObject &args, QJsonObject *error)
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
        return nullptr;
    }
    ClipNodeEditor *editor = w->clipNodeEditor();
    ClipNodeModel *node = editor ? editor->nodeAt(static_cast<NodeId>(id)) : nullptr;
    if (!node || !node->hasSource()) {
        if (error)
            *error = err("not_found", QStringLiteral("No clip %1").arg(id));
        return nullptr;
    }
    return node;
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
    if (s == QLatin1String("ab_to_output") || s == QLatin1String("8"))
        return 8;
    if (s == QLatin1String("script_to_data") || s == QLatin1String("6"))
        return 6;
    if (s == QLatin1String("clip_to_shader_audio") || s == QLatin1String("5"))
        return 5;
    if (s == QLatin1String("clip_to_audio_script") || s == QLatin1String("12"))
        return 12;
    if (s == QLatin1String("controller_to_master") || s == QLatin1String("3"))
        return 3;
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
    add("html", "HTML in html");
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

    ClipNodeModel *node = m_window->addSourceFromDescriptor(desc, thumbFor(desc));
    if (!node)
        return err("not_found", QStringLiteral("Could not add clip"));
    return clipOk(node);
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
        desc.color = parseColor(args.value(QStringLiteral("color")).toString(), Qt::white);
        if (desc.displayName.isEmpty())
            desc.displayName = text.left(24);
        break;
    }
    case SourceDescriptor::Kind::Html: {
        const QString html = args.value(QStringLiteral("html")).toString();
        if (html.trimmed().isEmpty())
            return err("bad_args", QStringLiteral("html is required for kind html"));
        desc.htmlContent = html;
        if (desc.displayName.isEmpty())
            desc.displayName = QStringLiteral("HTML Overlay");
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
    default:
        return err("bad_args", QStringLiteral("Unsupported kind: %1").arg(kindStr));
    }

    ClipNodeModel *node = m_window->addSourceFromDescriptor(desc, thumbFor(desc));
    if (!node)
        return err("not_found", QStringLiteral("Could not add source"));
    return clipOk(node);
}

QJsonObject McpDispatcher::opRemoveClip(const QJsonObject &args)
{
    QJsonObject error;
    ClipNodeModel *node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    const NodeId id = node->nodeId();
    if (!m_window->mcpRemoveClip(id))
        return err("not_found", QStringLiteral("Could not remove clip"));
    return ok({{QStringLiteral("id"), QString::number(id)}});
}

QJsonObject McpDispatcher::opRenameClip(const QJsonObject &args)
{
    QJsonObject error;
    ClipNodeModel *node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    const QString name = args.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty())
        return err("bad_args", QStringLiteral("name is required"));
    if (!m_window->mcpRenameClip(node->nodeId(), name))
        return err("not_found");
    return clipOk(node);
}

QJsonObject McpDispatcher::opSetText(const QJsonObject &args)
{
    QJsonObject error;
    ClipNodeModel *node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    if (node->sourceDescriptor().kind != SourceDescriptor::Kind::Text)
        return err("type_mismatch", QStringLiteral("Clip is not a text source"));
    SourceDescriptor desc = node->sourceDescriptor();
    desc.textTemplate = args.value(QStringLiteral("text")).toString();
    if (args.contains(QStringLiteral("color")))
        desc.color = parseColor(args.value(QStringLiteral("color")).toString(), desc.color);
    if (!m_window->mcpUpdateSource(node->nodeId(), desc, thumbFor(desc)))
        return err("not_found");
    return clipOk(node);
}

QJsonObject McpDispatcher::opSetHtml(const QJsonObject &args)
{
    QJsonObject error;
    ClipNodeModel *node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    if (node->sourceDescriptor().kind != SourceDescriptor::Kind::Html)
        return err("type_mismatch", QStringLiteral("Clip is not an HTML source"));
    SourceDescriptor desc = node->sourceDescriptor();
    desc.htmlContent = args.value(QStringLiteral("html")).toString();
    if (!m_window->mcpUpdateSource(node->nodeId(), desc, thumbFor(desc)))
        return err("not_found");
    return clipOk(node);
}

QJsonObject McpDispatcher::opSetShader(const QJsonObject &args)
{
    QJsonObject error;
    ClipNodeModel *node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    if (node->sourceDescriptor().kind != SourceDescriptor::Kind::Shader)
        return err("type_mismatch", QStringLiteral("Clip is not a shader source"));
    SourceDescriptor desc = node->sourceDescriptor();
    desc.shaderCode = args.value(QStringLiteral("code")).toString();
    if (!m_window->mcpUpdateSource(node->nodeId(), desc, thumbFor(desc)))
        return err("not_found");
    return clipOk(node);
}

QJsonObject McpDispatcher::opSelectDeck(const QJsonObject &args, bool deckA)
{
    QJsonObject error;
    ClipNodeModel *node = resolveClip(m_window, args, &error);
    if (!node)
        return error;
    if (deckA)
        m_window->selectNodeA(node->nodeId());
    else
        m_window->selectNodeB(node->nodeId());
    m_window->mcpBumpRevision();
    return clipOk(node, {{QStringLiteral("deck"), deckA ? QStringLiteral("a") : QStringLiteral("b")}});
}

QJsonObject McpDispatcher::opPlayDeck(bool deckA, bool play)
{
    m_window->playDeck(deckA, play);
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("deck"), deckA ? QStringLiteral("a") : QStringLiteral("b")},
               {QStringLiteral("playing"), play}});
}

QJsonObject McpDispatcher::opSeekDeck(const QJsonObject &args, bool deckA)
{
    if (!args.contains(QStringLiteral("at")))
        return err("bad_args", QStringLiteral("at is required"));
    const double at = jsonNumber(args.value(QStringLiteral("at")), 0);
    m_window->seekDeck(deckA, qMax(0.0, at));
    return ok({{QStringLiteral("deck"), deckA ? QStringLiteral("a") : QStringLiteral("b")},
               {QStringLiteral("at"), at}});
}

QJsonObject McpDispatcher::opSetSpeed(const QJsonObject &args)
{
    const QString deck = args.value(QStringLiteral("deck")).toString().trimmed().toLower();
    if (deck != QLatin1String("a") && deck != QLatin1String("b"))
        return err("bad_args", QStringLiteral("deck must be a or b"));
    if (!args.contains(QStringLiteral("speed")))
        return err("bad_args", QStringLiteral("speed is required"));
    const double speed = jsonNumber(args.value(QStringLiteral("speed")), 1.0);
    m_window->setDeckSpeedValue(deck == QLatin1String("a"), speed);
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("deck"), deck}, {QStringLiteral("speed"), speed}});
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
    if (auto *t = m_window->transitionController())
        t->onCutTransitionClicked();
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("fader"), m_window->faderValue()}});
}

QJsonObject McpDispatcher::opAuto()
{
    if (auto *t = m_window->transitionController())
        t->onAutoTransitionClicked();
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
    auto *editor = m_window->clipNodeEditor();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    QJsonObject snap = editor->graphSnapshot();
    snap.insert(QStringLiteral("ok"), true);
    return snap;
}

QJsonObject McpDispatcher::opAddProcessNode(const QJsonObject &args)
{
    auto *editor = m_window->clipNodeEditor();
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
    const NodeId id = editor->addProcessNode(desc->id, params, x, y);
    if (!id)
        return err("conflict", QStringLiteral("Could not add process node"));
    m_window->mcpBumpRevision();
    const QJsonObject snap = editor->graphSnapshot();
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
    auto *editor = m_window->clipNodeEditor();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const double x = args.contains(QStringLiteral("x"))
                         ? jsonNumber(args.value(QStringLiteral("x")), 0)
                         : qQNaN();
    const double y = args.contains(QStringLiteral("y"))
                         ? jsonNumber(args.value(QStringLiteral("y")), 0)
                         : qQNaN();
    const NodeId id = editor->addLayerNode(x, y);
    if (!id)
        return err("conflict", QStringLiteral("Could not add layer node"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("id"), QString::number(id)},
               {QStringLiteral("type"), QStringLiteral("layer")}});
}

QJsonObject McpDispatcher::opAddAbSelect(const QJsonObject &args)
{
    auto *editor = m_window->clipNodeEditor();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const double x = args.contains(QStringLiteral("x"))
                         ? jsonNumber(args.value(QStringLiteral("x")), 0)
                         : qQNaN();
    const double y = args.contains(QStringLiteral("y"))
                         ? jsonNumber(args.value(QStringLiteral("y")), 0)
                         : qQNaN();
    const NodeId id = editor->addAbSelectNode(x, y);
    if (!id)
        return err("conflict", QStringLiteral("Could not add A/B select node"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("id"), QString::number(id)},
               {QStringLiteral("type"), QStringLiteral("ab_select")}});
}

QJsonObject McpDispatcher::opConnect(const QJsonObject &args)
{
    auto *editor = m_window->clipNodeEditor();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const NodeId from = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("from"))));
    const NodeId to = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("to"))));
    if (!from || !to)
        return err("bad_args", QStringLiteral("from and to are required node ids"));
    if (!editor->hasGraphNode(from))
        return err("not_found", QStringLiteral("No node %1").arg(from));
    if (!editor->hasGraphNode(to))
        return err("not_found", QStringLiteral("No node %1").arg(to));
    const int kind = parseConnectionKind(args.value(QStringLiteral("kind")));
    if (kind == -2)
        return err("bad_args", QStringLiteral("Unknown connection kind"));
    const int toPort = jsonInt(args.value(QStringLiteral("to_port")), -1);
    if (!editor->connectNodes(from, to, kind, toPort))
        return err("conflict",
                   QStringLiteral("Could not connect those ports. Output cannot take a video "
                                  "chain and A/B at the same time — disconnect the other Output "
                                  "input first, or splice into an existing chain."));
    m_window->mcpBumpRevision();
    QString kindName;
    for (const QJsonValue &v : editor->graphSnapshot().value(QStringLiteral("connections")).toArray()) {
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
    auto *editor = m_window->clipNodeEditor();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const NodeId from = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("from"))));
    const NodeId to = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("to"))));
    if (!from || !to)
        return err("bad_args", QStringLiteral("from and to are required node ids"));
    const int toPort = jsonInt(args.value(QStringLiteral("to_port")), -1);
    if (!editor->disconnectNodes(from, to, toPort))
        return err("not_found", QStringLiteral("No such connection"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("from"), QString::number(from)},
               {QStringLiteral("to"), QString::number(to)}});
}

QJsonObject McpDispatcher::opSetProcessParams(const QJsonObject &args)
{
    auto *editor = m_window->clipNodeEditor();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    const NodeId id = static_cast<NodeId>(parseNodeId(args.value(QStringLiteral("node"))));
    if (!id)
        return err("bad_args", QStringLiteral("node is required"));
    if (!editor->hasGraphNode(id))
        return err("not_found", QStringLiteral("No node %1").arg(id));
    const QJsonObject params = args.value(QStringLiteral("params")).toObject();
    if (params.isEmpty())
        return err("bad_args", QStringLiteral("params is required"));
    if (!editor->setProcessParams(id, params, true))
        return err("type_mismatch", QStringLiteral("Node is not a process node"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("id"), QString::number(id)}, {QStringLiteral("params"), params}});
}

QJsonObject McpDispatcher::opRemoveNode(const QJsonObject &args)
{
    auto *editor = m_window->clipNodeEditor();
    if (!editor)
        return err("not_found", QStringLiteral("No node editor"));
    QJsonValue nodeVal = args.value(QStringLiteral("node"));
    if (nodeVal.isUndefined() || nodeVal.isNull())
        nodeVal = args.value(QStringLiteral("clip"));
    const NodeId id = static_cast<NodeId>(parseNodeId(nodeVal));
    if (!id)
        return err("bad_args", QStringLiteral("node is required"));
    if (!editor->hasGraphNode(id))
        return err("not_found", QStringLiteral("No node %1").arg(id));
    if (id == editor->outputNodeId())
        return err("conflict", QStringLiteral("Output node cannot be removed while the graph has other nodes"));
    editor->removeNode(id);
    if (editor->hasGraphNode(id))
        return err("conflict", QStringLiteral("Could not remove node"));
    m_window->mcpBumpRevision();
    return ok({{QStringLiteral("id"), QString::number(id)}});
}

} // namespace prism::mcp
