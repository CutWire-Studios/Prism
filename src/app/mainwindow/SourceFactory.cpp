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
#include "core/sources/NdiSource.h"
#ifdef PRISM_HAVE_WEBRTC
#include "core/sources/WebRtcSource.h"
#endif
#include "core/graph/ProcessEffects.h"
#include "core/platform/MacPermissions.h"
#include <QObject>
#include <QtGlobal>
#include <QMediaDevices>
#include <QCameraDevice>

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
        auto src = std::make_unique<CameraSource>();
        if (desc.path.isEmpty()) {
            src->start({});
        } else {
            bool matched = false;
            for (const auto &dev : QMediaDevices::videoInputs()) {
                if (QString::fromUtf8(dev.id()) == desc.path) {
                    src->start(dev);
                    matched = true;
                    break;
                }
            }
            if (!matched)
                src->startDevice(desc.path);
        }
        return src;
    }
    case Kind::Screen: {
        auto src = std::make_unique<ScreenSource>();
#ifdef Q_OS_LINUX
        // Linux capture goes through xdg-desktop-portal, which remembers the
        // chosen monitor/window via the restore token keyed by captureId.
        src->setCaptureId(desc.captureId);
        if (!src->start(ScreenSource::CaptureType::Any)) return nullptr;
#else
        // macOS: QScreenCapture silently yields black frames without the Screen
        // Recording grant. Trigger the prompt; bail if not yet granted so the
        // caller can tell the user to grant it and relaunch.
        if (!MacPermissions::ensureScreenCaptureAccess()) return nullptr;
        if (!src->start(desc.screenIndex)) return nullptr;
#endif
        return src;
    }
    case Kind::Window: {
#ifdef Q_OS_LINUX
        // On Linux there is no reliable per-window capture outside the portal, so
        // window capture uses the same portal-backed ScreenSource with a
        // remembered selection.
        auto src = std::make_unique<ScreenSource>();
        src->setCaptureId(desc.captureId);
        if (!src->start(ScreenSource::CaptureType::Any)) return nullptr;
        return src;
#else
        if (!MacPermissions::ensureScreenCaptureAccess()) return nullptr;
        auto src = std::make_unique<WindowCaptureSource>();
        const auto windows = WindowCaptureSource::capturableWindows();
        if (windows.isEmpty()) return nullptr;
        const int idx = qBound(0, desc.windowIndex, windows.size() - 1);
        if (!src->start(windows.at(idx))) return nullptr;
        return src;
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
    case Kind::RemovedHtml:
        qWarning("Skipping source: HTML overlays are no longer supported");
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
    case Kind::Ndi: {
        auto src = std::make_unique<NdiSource>();
        if (!src->connectTo(desc.path)) return nullptr;
        src->setName(desc.displayName.isEmpty() ? desc.path : desc.displayName);
        return src;
    }
    case Kind::WebRtc: {
#ifdef PRISM_HAVE_WEBRTC
        auto src = std::make_unique<WebRtcSource>(desc.path);
        src->setName(desc.displayName.isEmpty() ? QObject::tr("Phone Camera") : desc.displayName);
        return src;
#else
        return nullptr;
#endif
    }
    }
    return nullptr;
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
