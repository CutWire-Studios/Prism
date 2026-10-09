#pragma once

#include <QString>
#include <QStringList>
#include <QColor>

// Lightweight description of any media source that a ClipCard can hold.
// MainWindow reads this when an A/B button is clicked and creates the
// appropriate MediaSource subclass on the fly.

struct SourceDescriptor {
    enum class CanvasFill {
        Checkered,
        Transparent,
        Color,
    };

    enum class Kind {
        VideoFile,   // FFmpeg video file  — path = file path
        Image,       // static PNG/JPG     — path = file path
        Slideshow,   // image folder       — path = folder path
        Camera,      // webcam             — cameraIndex / v4l2Path
        Screen,      // display capture    — screenIndex into QGuiApplication::screens()
        Canvas,      // customizable canvas — canvas fields
        Window,      // window/tab capture — windowIndex into capturableWindows()
        Shader, // GLSL fragment shader — shaderCode field
        RemovedHtml, // retired HTML overlay; kept so saved kind values stay stable, never created
        Ndi,    // network NDI source   — path = NDI source name
        WebRtc, // phone camera (WebRTC) — path = session token; webrtcRelayUrl when using public relay
        Text,   // CPU-rendered text overlay — textTemplate field
        AudioFile, // audio-only media file — path = file path
        Shape,  // CPU-rendered vector shape — shapeStyleJson field
        SvgTemplate, // SVG with {token} placeholders — svgTemplateId / svgParamsJson
    };

    Kind    kind    = Kind::VideoFile;
    QString path;                       // VideoFile / Image / Slideshow / Camera v4l2
    QString displayName;                // human-readable label shown on card
    QColor  color   = Qt::white;        // Canvas color fill
    int     cameraIndex      = 0;       // Camera kind
    int     screenIndex      = 0;       // Screen kind
    int     windowIndex      = 0;       // Window kind
    QString captureId;                  // Screen/Window kind — stable id used to
                                        // remember the OS capture selection
                                        // (Linux: xdg-desktop-portal restore token)
    int     slideshowIntervalMs = 3000; // Slideshow kind
    int     slideshowEffect = 0;        // Slideshow kind — SlideshowSource::Effect index
    int     slideshowTransitionMs = 800; // Slideshow kind — transition duration
    int     canvasWidth      = 1280;    // Canvas kind
    int     canvasHeight     = 720;     // Canvas kind
    CanvasFill canvasFill    = CanvasFill::Checkered; // Canvas kind
    QString shaderCode;                 // Shader kind
    QString obsSceneName;               // OBS program scene to switch when clip is triggered
    QString textTemplate;               // Text kind — may contain {parameter} placeholders
    QString webrtcRelayUrl;             // WebRtc kind — wss://…/ws when using public signaling relay
    QString textStyleJson;              // Text kind — serialized prism::TextStyle
    QString shapeStyleJson;             // Shape kind — serialized prism::ShapeStyle
    QString svgTemplateId;              // SvgTemplate kind — built-in id, or path of a custom SVG file
    QString svgParamsJson;              // SvgTemplate kind — JSON object of the user's param overrides

    // Descriptor-held content that defines the rendered output (as opposed to
    // on-disk content referenced by `path`). Deck reuse keys include a hash of
    // this so that editing e.g. shader code, canvas fill or text styling
    // reloads any deck currently showing the source.
    QString contentKey() const {
        return QStringList{
            shaderCode, textTemplate, textStyleJson, shapeStyleJson, svgTemplateId, svgParamsJson,
            QString::number(color.rgba()),
            QString::number(cameraIndex),
            QString::number(screenIndex),
            QString::number(windowIndex),
            QString::number(slideshowIntervalMs),
            QString::number(slideshowEffect),
            QString::number(slideshowTransitionMs),
            QString::number(canvasWidth),
            QString::number(canvasHeight),
            QString::number(int(canvasFill)),
        }.join(QChar(0x1F));
    }

    bool isLiveSource() const {
        return kind == Kind::Camera || kind == Kind::Screen ||
               kind == Kind::Canvas || kind == Kind::Window ||
               kind == Kind::Shader ||
               kind == Kind::Ndi || kind == Kind::WebRtc ||
               kind == Kind::Text || kind == Kind::Shape || kind == Kind::SvgTemplate;
    }
    bool isFileSource() const {
        return kind == Kind::VideoFile || kind == Kind::Image || kind == Kind::Slideshow
            || kind == Kind::AudioFile;
    }
    bool isAudioOnlySource() const {
        return kind == Kind::AudioFile;
    }

    // ── Playback capabilities ────────────────────────────────────────────────
    // Drive which controls (repeat, timeline, play/pause, speed) the UI offers
    // for a clip of this kind.

    /// Can loop back to the start when it reaches the end.
    bool isRepeatable() const {
        return kind == Kind::VideoFile || kind == Kind::Slideshow || kind == Kind::AudioFile;
    }
    /// Has a finite timeline that can be scrubbed.
    bool isSeekable() const {
        return kind == Kind::VideoFile || kind == Kind::AudioFile;
    }
    /// Play/pause is meaningful (static images and canvases have no motion;
    /// live sources freeze on the current frame).
    bool isPausable() const {
        return kind != Kind::Image && kind != Kind::Canvas;
    }
    /// Playback rate can be changed.
    bool hasSpeedControl() const {
        return kind == Kind::VideoFile || kind == Kind::AudioFile;
    }
};
