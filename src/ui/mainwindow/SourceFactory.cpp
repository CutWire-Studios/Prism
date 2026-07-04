#include "ui/mainwindow/SourceFactory.h"
#include "core/sources/VideoFileSource.h"
#include "core/sources/ImageSource.h"
#include "core/sources/SlideshowSource.h"
#include "core/sources/CameraSource.h"
#include "core/sources/ScreenSource.h"
#include "core/sources/WindowCaptureSource.h"
#include "core/sources/CanvasSource.h"
#include "core/sources/ShaderSource.h"
#include "core/sources/HtmlSource.h"
#include "core/sources/HtmlWorkspace.h"
#include "core/sources/TextSource.h"
#include "core/sources/NdiSource.h"
#ifdef PRISM_HAVE_WEBRTC
#include "core/sources/WebRtcSource.h"
#endif
#include "ui/nodes/ProcessEffects.h"
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
    case Kind::Html: {
        QString html = desc.htmlContent;
        QString path = desc.path;
        if (!desc.htmlWorkspace.isEmpty()) {
            html = HtmlWorkspaceBuilder::buildFromJson(desc.htmlWorkspace);
            path = {};
        }
        return std::make_unique<HtmlSource>(html, path);
    }
    case Kind::Text:
        return std::make_unique<TextSource>(desc);
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

VideoWidget::NodeChainSource SourceFactory::makeLayerEntry(const ResolvedLayer &layer,
                                                           ClipNodeEditor *editor) {
    using Kind = SourceDescriptor::Kind;
    VideoWidget::NodeChainSource entry;
    entry.cropX = layer.cropX; entry.cropY = layer.cropY;
    entry.cropW = layer.cropW; entry.cropH = layer.cropH;
    entry.flipH = layer.flipH; entry.flipV = layer.flipV;
    entry.baseX = layer.baseX; entry.baseY = layer.baseY;
    entry.baseW = layer.baseW; entry.baseH = layer.baseH;
    entry.visible = layer.visible;

    ClipNodeModel *node = editor->nodeAt(layer.inputNodeId);
    if (!node) return entry;
    const SourceDescriptor &desc = node->sourceDescriptor();

    auto src = create(desc);
    if (src) {
        if (desc.kind == Kind::VideoFile) {
            auto *vfs = static_cast<VideoFileSource *>(src.get());
            if (node->startTime() > 0) vfs->seek(node->startTime());
            vfs->nextFrame();
        } else if (desc.kind == Kind::Image) {
            src->nextFrame();
        }
        entry.playing = true;
        entry.source  = std::move(src);

        if (desc.kind == Kind::Text) {
            if (auto data = editor->scriptOutputForDataNode(layer.inputNodeId)) {
                if (auto *textSrc = dynamic_cast<TextSource *>(entry.source.get()))
                    textSrc->setDataSource(data);
            }
        }

        entry.source = ProcessEffects::applySourceEffects(std::move(entry.source),
                                                          layer.sourceEffects);
    }
    return entry;
}

std::vector<VideoWidget::NodeChainSource>
SourceFactory::buildStream(const ResolvedStream &stream, ClipNodeEditor *editor) {
    std::vector<VideoWidget::NodeChainSource> out;
    for (const ResolvedLayer &layer : stream.layers) {
        auto entry = makeLayerEntry(layer, editor);
        entry.canvasWidth  = stream.canvasWidth;
        entry.canvasHeight = stream.canvasHeight;
        out.push_back(std::move(entry));
    }
    return out;
}
