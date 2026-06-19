#include "ui/SourceFactory.h"
#include "core/VideoFileSource.h"
#include "core/ImageSource.h"
#include "core/SlideshowSource.h"
#include "core/CameraSource.h"
#include "core/ScreenSource.h"
#include "core/CanvasSource.h"
#include "core/ShaderSource.h"
#include "core/HtmlSource.h"
#include "core/TextSource.h"
#include "core/NdiSource.h"
#ifdef SWITCHX_HAVE_WEBRTC
#include "core/WebRtcSource.h"
#endif
#include <QObject>
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
        if (!src->start(ScreenSource::CaptureType::Monitor)) return nullptr;
        return src;
    }
    case Kind::Window: {
        auto src = std::make_unique<ScreenSource>();
        if (!src->start(ScreenSource::CaptureType::Window)) return nullptr;
        return src;
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
    case Kind::Html:
        return std::make_unique<HtmlSource>(desc.htmlContent, desc.path);
    case Kind::Text: {
        QFont font(desc.fontFamily, desc.fontSize);
        return std::make_unique<TextSource>(
            desc.textTemplate,
            font,
            desc.color,
            static_cast<Qt::Alignment>(desc.textAlign),
            QSize(desc.canvasWidth, desc.canvasHeight),
            desc.textBgTransparent,
            desc.textBgColor);
    }
    case Kind::Ndi: {
        auto src = std::make_unique<NdiSource>();
        if (!src->connectTo(desc.path)) return nullptr;
        src->setName(desc.displayName.isEmpty() ? desc.path : desc.displayName);
        return src;
    }
    case Kind::WebRtc: {
#ifdef SWITCHX_HAVE_WEBRTC
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

VideoWidget::NodeChainSource SourceFactory::makeChainEntry(ClipNodeModel *node,
                                                            ClipNodeEditor *editor) {
    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor &desc = node->sourceDescriptor();
    VideoWidget::NodeChainSource entry;
    entry.cropX = node->cropX(); entry.cropY = node->cropY();
    entry.cropW = node->cropW(); entry.cropH = node->cropH();

    if (!editor->clipTransform(node->nodeId(), entry.baseX, entry.baseY, entry.baseW, entry.baseH)) {
        entry.baseX = 0.f; entry.baseY = 0.f; entry.baseW = 1.f; entry.baseH = 1.f;
    }

    auto src = create(desc);
    if (src) {
        // Prime the source with one frame for chain display.
        if (desc.kind == Kind::VideoFile) {
            auto *vfs = static_cast<VideoFileSource *>(src.get());
            if (node->startTime() > 0) vfs->seek(node->startTime());
            vfs->nextFrame();
        } else if (desc.kind == Kind::Image) {
            src->nextFrame();
        }
        entry.playing = true;
        entry.source  = std::move(src);

        if (desc.kind == Kind::Text && editor) {
            if (auto data = editor->scriptOutputForDataNode(node->nodeId())) {
                if (auto *textSrc = dynamic_cast<TextSource *>(entry.source.get()))
                    textSrc->setDataSource(data);
            }
        }
    }
    return entry;
}

std::vector<VideoWidget::NodeChainSource>
SourceFactory::buildChain(const QVector<ClipNodeModel *> &chain,
                           ClipNodeEditor *editor,
                           int canvasWidth, int canvasHeight) {
    std::vector<VideoWidget::NodeChainSource> out;
    // Index 0 is the primary (deck) clip; overlays start at index 1.
    for (int i = 1; i < chain.size(); ++i) {
        auto entry = makeChainEntry(chain[i], editor);
        if (entry.source) {
            entry.canvasWidth  = canvasWidth;
            entry.canvasHeight = canvasHeight;
            out.push_back(std::move(entry));
        }
    }
    return out;
}
