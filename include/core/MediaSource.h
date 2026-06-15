#pragma once

#include <QSize>
#include <QString>
#include <cstdint>

// Abstract interface for any media input that can feed a VideoWidget deck.
// All sources produce RGB24 frames. Implementors:
//   VideoFileSource  — FFmpeg video file
//   ImageSource      — static PNG/JPG/BMP via QImage
//   SlideshowSource  — folder of images + timer  (Part 2)
//   CameraSource     — webcam via Qt Multimedia   (Part 3)
//   ScreenSource     — screen capture             (Part 3)
//   ColorSource      — solid colour / test card   (Part 2)

class MediaSource {
public:
    enum class Type { VideoFile, Image, Slideshow, Camera, Screen, Color, Window };

    virtual ~MediaSource() = default;

    virtual Type    type()        const = 0;
    virtual bool    isReady()     const = 0;   // has valid frame data?
    virtual QSize   frameSize()   const = 0;
    virtual const uint8_t *frameData() const = 0;  // RGB24, row-major

    // Advance to the next frame. Returns true if the frame buffer changed.
    // Static/live sources that never change return false.
    virtual bool    nextFrame()         = 0;

    // Timing — return 0 for live/static sources (no meaningful duration).
    virtual double  duration()    const { return 0.0; }
    virtual double  currentTime() const { return 0.0; }
    virtual void    seek(double)        {}

    // Playback control — no-op for sources that manage their own timing.
    virtual void    play()              {}
    virtual void    pause()             {}

    virtual QString displayName() const { return {}; }
};
