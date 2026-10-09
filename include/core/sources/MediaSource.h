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
//   CanvasSource     — checkered/solid canvas      (Part 2)

class MediaSource {
public:
    enum class Type { VideoFile, Image, Slideshow, Camera, Screen, Canvas, Window, Shader, Ndi, WebRtc, Text, Shape, SvgTemplate };

    virtual ~MediaSource() = default;

    virtual Type    type()        const = 0;
    virtual bool    isReady()     const = 0;   // has valid frame data?
    virtual QSize   frameSize()   const = 0;
    virtual const uint8_t *frameData() const = 0;  // RGB24, row-major
    virtual int frameBytesPerLine() const {
        const QSize sz = frameSize();
        if (sz.isEmpty()) return 0;
        return sz.width() * (hasAlpha() ? 4 : 3);
    }

    // Advance to the next frame. Returns true if the frame buffer changed.
    // Static/live sources that never change return false.
    virtual bool    nextFrame()         = 0;

    // Show the frame due at `seconds` on the source's own timeline, skipping any in between.
    // Returns true if the frame changed. Sources with a real timeline override this; the default
    // steps nextFrame() until currentTime() catches up, bounded so a stall cannot spiral.
    virtual bool presentAt(double seconds) {
        bool changed = false;
        for (int steps = 0; steps < 8 && currentTime() < seconds; ++steps) {
            if (!nextFrame()) break;
            changed = true;
        }
        return changed;
    }

    // Timing — return 0 for live/static sources (no meaningful duration).
    virtual double  duration()    const { return 0.0; }
    virtual double  currentTime() const { return 0.0; }
    virtual void    seek(double)        {}

    // Playback control — no-op for sources that manage their own timing.
    virtual void    play()              {}
    virtual void    pause()             {}

    virtual QString displayName() const { return {}; }

    // Level-triggered: VideoWidget reports every tick whether the deck holding this source
    // contributes to program. Animated sources restart their In phase on the false -> true edge.
    virtual void setOnAir(bool) {}

    // Starts the source's Out animation and returns its length in seconds (0 = none, nothing to
    // wait for). The caller delays taking the source off air by that long.
    virtual double requestOut() { return 0.0; }

    // True if frameData() is RGBA32 rather than RGB24.
    // Affects GL texture format selection in VideoWidget.
    virtual bool hasAlpha() const { return false; }

    // If non-zero, the source has already rendered its current frame into this
    // GL texture (in a context sharing with VideoWidget's). VideoWidget binds it
    // directly and skips the frameData() upload — avoids a GPU→CPU→GPU round
    // trip. Returns 0 when the source has no GPU-resident frame this tick.
    virtual unsigned int glTexture() const { return 0; }
};
