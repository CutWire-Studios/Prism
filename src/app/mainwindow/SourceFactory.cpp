#include "app/mainwindow/SourceFactory.h"
#include "core/sources/VideoFileSource.h"
#include "core/sources/ImageSource.h"
#include "core/sources/SlideshowSource.h"
#include "core/sources/CameraSource.h"
#include "core/sources/ScreenSource.h"
#include "core/sources/WindowCaptureSource.h"
#include "core/sources/CanvasSource.h"
#include "core/sources/ShaderSource.h"
#include "core/sources/ShapeSource.h"
#include "core/sources/SvgTemplateSource.h"
#include "core/sources/TextSource.h"
#include "core/sources/LayerCompositorSource.h"
#include "core/sources/SceneSource.h"
#include "core/sources/NdiSource.h"
#include "core/sources/SharedSourcePool.h"
#ifdef PRISM_HAVE_WEBRTC
#include "core/sources/WebRtcSource.h"
#endif
#include "core/graph/ProcessEffects.h"
#include "core/platform/MacPermissions.h"
#include <QObject>
#include <QtGlobal>
#include <QMediaDevices>
#include <QCameraDevice>

namespace {

SourceFactory::SceneResolver g_sceneResolver;
SourceFactory::SceneParamUpdater g_sceneParamUpdater;
int g_sceneDepth = 0;

std::unique_ptr<MediaSource> screenView(MediaSource::Type type, const std::shared_ptr<ScreenSource> &backend,
                                        const QString &name) {
    SharedFrameSource::Producer p;
    p.keepAlive = backend;
    p.poll = [b = backend.get()] { return b->generation(); };
    p.frame = [b = backend.get()]() -> const QImage & { return b->frame(); };
    p.missing = [b = backend.get()] { return !b->isCapturing() && !b->isPending(); };
    return std::make_unique<SharedFrameSource>(type, std::move(p), name);
}

#ifdef Q_OS_LINUX
// The portal session is shared per captureId; an empty id has no restore token to key on,
// so it gets a private session.
std::unique_ptr<MediaSource> sharedScreen(const QString &captureId, const QString &name) {
    const auto make = [captureId]() -> std::shared_ptr<ScreenSource> {
        auto s = std::make_shared<ScreenSource>();
        s->setCaptureId(captureId);
        if (!s->start(ScreenSource::CaptureType::Any)) return nullptr;
        return s;
    };
    std::shared_ptr<ScreenSource> backend;
    if (captureId.isEmpty())
        backend = make();
    else
        backend = SharedSourcePool::instance().acquire<ScreenSource>(
            QStringLiteral("screen:") + SharedSourcePool::instance().resolve(captureId), make);
    if (!backend)
        return std::make_unique<MissingSource>(MediaSource::Type::Screen, name);
    return screenView(MediaSource::Type::Screen, backend, name);
}
#endif

}

void SourceFactory::setSceneResolver(SceneResolver resolver)
{
    g_sceneResolver = std::move(resolver);
}

std::unique_ptr<MediaSource> SourceFactory::create(const SourceDescriptor &desc) {
    using Kind = SourceDescriptor::Kind;
    switch (desc.kind) {

    case Kind::VideoFile: {
        auto src = std::make_unique<VideoFileSource>();
        src->setAsync(true);
        if (!src->open(desc.path)) return nullptr;
        return src;
    }
    case Kind::Image: {
        auto src = std::make_unique<ImageSource>();
        if (!src->load(desc.path)) return nullptr;
        return src;
    }
    case Kind::Slideshow: {
        auto src = std::make_unique<SlideshowSource>();
        if (!src->loadFolder(desc.path, desc.slideshowIntervalMs)) return nullptr;
        src->setEffect(static_cast<SlideshowSource::Effect>(desc.slideshowEffect));
        src->setTransitionMs(desc.slideshowTransitionMs);
        return src;
    }
    case Kind::Camera: {
        const QString path = SharedSourcePool::instance().resolve(desc.path);
        const QString name = desc.displayName.isEmpty() ? path : desc.displayName;
        auto src = std::make_unique<CameraSource>();
        if (path.isEmpty()) {
            src->start({});
        } else {
            const auto devices = QMediaDevices::videoInputs();
            bool matched = false;
            for (const auto &dev : devices) {
                if (QString::fromUtf8(dev.id()) == path) {
                    src->start(dev);
                    matched = true;
                    break;
                }
            }
            // An empty device list is the IPU6/MIPI case where only the default camera opens.
            if (!matched && !devices.isEmpty())
                return std::make_unique<MissingSource>(MediaSource::Type::Camera, name);
            if (!matched)
                src->startDevice(path);
        }
        return src;
    }
    case Kind::Screen: {
        const QString name = desc.displayName.isEmpty() ? QObject::tr("Screen") : desc.displayName;
#ifdef Q_OS_LINUX
        return sharedScreen(desc.captureId, name);
#else
        // macOS: QScreenCapture silently yields black frames without the Screen
        // Recording grant. Trigger the prompt; show a black stand-in if not yet granted.
        if (!MacPermissions::ensureScreenCaptureAccess())
            return std::make_unique<MissingSource>(MediaSource::Type::Screen, name);
        const int index = desc.screenIndex;
        auto backend = SharedSourcePool::instance().acquire<ScreenSource>(
            QStringLiteral("screen:#%1").arg(index), [&]() -> std::shared_ptr<ScreenSource> {
                auto s = std::make_shared<ScreenSource>();
                if (!s->start(index)) return nullptr;
                return s;
            });
        if (!backend)
            return std::make_unique<MissingSource>(MediaSource::Type::Screen, name);
        return screenView(MediaSource::Type::Screen, backend, name);
#endif
    }
    case Kind::Window: {
        const QString name = desc.displayName.isEmpty() ? QObject::tr("Window") : desc.displayName;
#ifdef Q_OS_LINUX
        // On Linux there is no reliable per-window capture outside the portal, so
        // window capture uses the same portal-backed ScreenSource with a
        // remembered selection.
        return sharedScreen(desc.captureId, name);
#else
        if (!MacPermissions::ensureScreenCaptureAccess())
            return std::make_unique<MissingSource>(MediaSource::Type::Window, name);
        const auto windows = WindowCaptureSource::capturableWindows();
        if (windows.isEmpty())
            return std::make_unique<MissingSource>(MediaSource::Type::Window, name);
        const int idx = qBound(0, desc.windowIndex, windows.size() - 1);
        auto backend = SharedSourcePool::instance().acquire<WindowCaptureSource>(
            QStringLiteral("window:#%1").arg(idx), [&]() -> std::shared_ptr<WindowCaptureSource> {
                auto w = std::make_shared<WindowCaptureSource>();
                if (!w->start(windows.at(idx))) return nullptr;
                return w;
            });
        if (!backend)
            return std::make_unique<MissingSource>(MediaSource::Type::Window, name);
        SharedFrameSource::Producer p;
        p.keepAlive = backend;
        p.poll = [b = backend.get()] { return b->generation(); };
        p.frame = [b = backend.get()]() -> const QImage & { return b->frame(); };
        return std::make_unique<SharedFrameSource>(MediaSource::Type::Window, std::move(p), name);
#endif
    }
    case Kind::Canvas:
        if (desc.canvasFill == SourceDescriptor::CanvasFill::Transparent)
            return nullptr;
        return std::make_unique<CanvasSource>(
            desc.canvasFill == SourceDescriptor::CanvasFill::Color
                ? CanvasSource::Fill::SolidColor
                : CanvasSource::Fill::Checkered,
            QSize(desc.canvasWidth, desc.canvasHeight),
            desc.color);
    case Kind::Shader:
        return std::make_unique<ShaderSource>(desc.shaderCode);
    case Kind::AudioFile:
        // Audio-only: the deck's AudioPlayer plays it, there is no video source.
        return nullptr;
    case Kind::Text:
        return std::make_unique<TextSource>(desc);
    case Kind::Shape:
        return std::make_unique<ShapeSource>(desc);
    case Kind::SvgTemplate: {
        auto src = std::make_unique<SvgTemplateSource>(desc);
        if (!src->isReady()) return nullptr;
        return src;
    }
    case Kind::Scene: {
        std::unique_ptr<MediaSource> content;
        if (g_sceneResolver && !desc.sceneId.isEmpty() && g_sceneDepth < kMaxSceneDepth) {
            ++g_sceneDepth;
            content = g_sceneResolver(desc.sceneId);
            --g_sceneDepth;
        }
        return std::make_unique<SceneSource>(std::move(content), desc.displayName);
    }
    case Kind::Ndi: {
        const QString path = SharedSourcePool::instance().resolve(desc.path);
        const QString name = desc.displayName.isEmpty() ? path : desc.displayName;
        auto backend = SharedSourcePool::instance().acquire<NdiSource>(
            QStringLiteral("ndi:") + path, [&]() -> std::shared_ptr<NdiSource> {
                auto n = std::make_shared<NdiSource>();
                if (!n->connectTo(path)) return nullptr;
                return n;
            });
        if (!backend)
            return std::make_unique<MissingSource>(MediaSource::Type::Ndi, name);
        SharedFrameSource::Producer p;
        p.keepAlive = backend;
        p.poll = [b = backend.get()] { b->nextFrame(); return b->generation(); };
        p.frame = [b = backend.get()]() -> const QImage & { return b->frame(); };
        return std::make_unique<SharedFrameSource>(MediaSource::Type::Ndi, std::move(p), name);
    }
    case Kind::WebRtc: {
#ifdef PRISM_HAVE_WEBRTC
        auto src = std::make_unique<WebRtcSource>(SharedSourcePool::instance().resolve(desc.path));
        src->setName(desc.displayName.isEmpty() ? QObject::tr("Phone Camera") : desc.displayName);
        return src;
#else
        return nullptr;
#endif
    }
    }
    return nullptr;
}

void SourceFactory::setSceneParamUpdater(SceneParamUpdater updater)
{
    g_sceneParamUpdater = std::move(updater);
}

bool SourceFactory::updateLayerParams(MediaSource *top, const ClipGraph::ResolvedLayer &layer,
                                      ClipGraph *graph)
{
    // Sub-layer sources of a flattened composite are not addressable one-to-one.
    if (layer.composite) return false;
    MediaSource *base = ProcessEffects::updateSourceEffects(top, layer.sourceEffects);
    if (!base || !graph->hasNode(layer.inputNodeId)) return false;
    const SourceDescriptor desc = graph->sourceDescriptor(layer.inputNodeId);
    if (desc.kind != SourceDescriptor::Kind::Scene) return true;
    auto *scene = dynamic_cast<SceneSource *>(base);
    if (!scene) return false;
    if (!scene->content()) return true;
    if (!g_sceneParamUpdater || g_sceneDepth >= kMaxSceneDepth) return false;
    ++g_sceneDepth;
    const bool ok = g_sceneParamUpdater(desc.sceneId, scene->content());
    --g_sceneDepth;
    return ok;
}

std::unique_ptr<MediaSource> SourceFactory::buildLayerSource(const ClipGraph::ResolvedLayer &layer,
                                                             ClipGraph *graph, GraphRuntime *runtime) {
    using Kind = SourceDescriptor::Kind;

    // Flattened Layer node: composite its sub-layers (recursively) into one source.
    if (layer.composite) {
        std::vector<LayerCompositorSource::Layer> subs;
        for (const ClipGraph::ResolvedLayer &sl : layer.composite->layers) {
            auto s = buildLayerSource(sl, graph, runtime);
            if (!s) continue;
            LayerCompositorSource::Layer cl;
            cl.source = std::move(s);
            cl.bx = sl.baseX; cl.by = sl.baseY; cl.bw = sl.baseW; cl.bh = sl.baseH;
            cl.visible = sl.visible;
            cl.flipH = sl.flipH; cl.flipV = sl.flipV;
            subs.push_back(std::move(cl));
        }
        const QSize canvas(layer.composite->canvasWidth, layer.composite->canvasHeight);
        if (subs.empty() || canvas.isEmpty()) return nullptr;
        std::unique_ptr<MediaSource> comp =
            std::make_unique<LayerCompositorSource>(std::move(subs), canvas);
        return ProcessEffects::applySourceEffects(std::move(comp), layer.sourceEffects);
    }

    if (!graph->hasNode(layer.inputNodeId)) return nullptr;
    const SourceDescriptor desc = graph->sourceDescriptor(layer.inputNodeId);

    auto src = create(desc);
    if (!src) return nullptr;

    if (desc.kind == Kind::VideoFile) {
        auto *vfs = static_cast<VideoFileSource *>(src.get());
        const double startTime = graph->clipSettings(layer.inputNodeId).startTime;
        if (startTime > 0) vfs->seek(startTime);
        vfs->nextFrame();
    } else if (desc.kind == Kind::Image) {
        src->nextFrame();
    } else if (desc.kind == Kind::Text) {
        if (auto data = runtime->scriptOutputForDataNode(layer.inputNodeId)) {
            if (auto *textSrc = dynamic_cast<TextSource *>(src.get()))
                textSrc->setDataSource(data);
        }
    } else if (desc.kind == Kind::SvgTemplate) {
        if (auto data = runtime->scriptOutputForDataNode(layer.inputNodeId)) {
            if (auto *svgSrc = dynamic_cast<SvgTemplateSource *>(src.get()))
                svgSrc->setDataSource(data);
        }
    } else if (desc.kind == Kind::Shader) {
        if (auto data = runtime->scriptOutputForDataNode(layer.inputNodeId)) {
            if (auto *shaderSrc = dynamic_cast<ShaderSource *>(src.get()))
                shaderSrc->setDataSource(data);
        }
    }

    return ProcessEffects::applySourceEffects(std::move(src), layer.sourceEffects);
}

Compositor::NodeChainSource SourceFactory::makeLayerEntry(const ClipGraph::ResolvedLayer &layer,
                                                          ClipGraph *graph, GraphRuntime *runtime) {
    Compositor::NodeChainSource entry;
    entry.cropX = layer.cropX; entry.cropY = layer.cropY;
    entry.cropW = layer.cropW; entry.cropH = layer.cropH;
    entry.flipH = layer.flipH; entry.flipV = layer.flipV;
    entry.baseX = layer.baseX; entry.baseY = layer.baseY;
    entry.baseW = layer.baseW; entry.baseH = layer.baseH;
    entry.visible = layer.visible;

    entry.source = buildLayerSource(layer, graph, runtime);
    entry.playing = (entry.source != nullptr);
    return entry;
}

std::vector<Compositor::NodeChainSource>
SourceFactory::buildStream(const ClipGraph::ResolvedStream &stream, ClipGraph *graph,
                           GraphRuntime *runtime) {
    std::vector<Compositor::NodeChainSource> out;
    for (const ClipGraph::ResolvedLayer &layer : stream.layers) {
        auto entry = makeLayerEntry(layer, graph, runtime);
        entry.canvasWidth  = stream.canvasWidth;
        entry.canvasHeight = stream.canvasHeight;
        out.push_back(std::move(entry));
    }
    return out;
}
