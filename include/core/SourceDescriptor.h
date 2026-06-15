#pragma once

#include <QString>
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
        Shader,      // GLSL fragment shader — shaderCode field
    };

    Kind    kind    = Kind::VideoFile;
    QString path;                       // VideoFile / Image / Slideshow / Camera v4l2
    QString displayName;                // human-readable label shown on card
    QColor  color   = Qt::white;        // Canvas color fill
    int     cameraIndex      = 0;       // Camera kind
    int     screenIndex      = 0;       // Screen kind
    int     windowIndex      = 0;       // Window kind
    int     slideshowIntervalMs = 3000; // Slideshow kind
    int     canvasWidth      = 1280;    // Canvas kind
    int     canvasHeight     = 720;     // Canvas kind
    CanvasFill canvasFill    = CanvasFill::Checkered; // Canvas kind
    QString shaderCode;                 // Shader kind

    bool isLiveSource() const {
        return kind == Kind::Camera || kind == Kind::Screen ||
               kind == Kind::Canvas ||
               kind == Kind::Window ||
               kind == Kind::Shader;
    }
    bool isFileSource() const {
        return kind == Kind::VideoFile || kind == Kind::Image || kind == Kind::Slideshow;
    }
};
