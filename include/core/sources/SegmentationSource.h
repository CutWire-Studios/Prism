#pragma once

#include "core/sources/MediaSource.h"
#include "core/segmentation/SelfieSegmenter.h"

#include <QImage>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

// Decorator MediaSource that runs ML background removal on the frames of an
// inner source. It pulls RGB frames from the wrapped source, computes a
// foreground mask on a background thread (latest-frame-wins so a slow model
// never stalls the render loop), and emits RGBA32 frames whose alpha channel is
// the mask — so the background composites through as transparent.
//
// If the segmenter fails to initialise (e.g. missing model), the source falls
// back to passing the inner frame through unchanged as RGBA (fully opaque).
class SegmentationSource : public MediaSource {
public:
    explicit SegmentationSource(std::unique_ptr<MediaSource> inner);
    ~SegmentationSource() override;

    Type    type()        const override { return m_inner ? m_inner->type() : Type::Camera; }
    bool    isReady()     const override;
    QSize   frameSize()   const override { return m_output.size(); }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_output.constBits());
    }
    bool    nextFrame()         override;
    bool    hasAlpha()    const override { return true; }

    double  duration()    const override { return m_inner ? m_inner->duration() : 0.0; }
    double  currentTime() const override { return m_inner ? m_inner->currentTime() : 0.0; }
    void    seek(double s)      override { if (m_inner) m_inner->seek(s); }
    void    play()             override { if (m_inner) m_inner->play(); }
    void    pause()            override { if (m_inner) m_inner->pause(); }
    QString displayName() const override { return m_inner ? m_inner->displayName() : QString(); }

    MediaSource *inner() const { return m_inner.get(); }

private:
    void workerLoop();
    void compose(const QImage &frameRgb, const QImage &mask);

    std::unique_ptr<MediaSource> m_inner;
    std::shared_ptr<SelfieSegmenter> m_segmenter;

    QImage m_currentFrame;   // latest inner frame (RGB888)
    QImage m_output;         // composited RGBA8888 frame exposed via frameData()

    // ── Worker synchronisation ────────────────────────────────────────────────
    std::thread             m_worker;
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    QImage   m_pendingInput;         // frame awaiting inference (guarded)
    bool     m_hasPending = false;
    bool     m_stop = false;
    QImage   m_latestMask;           // most recent mask (guarded)
    quint64  m_maskGen = 0;
    quint64  m_consumedMaskGen = 0;
    QImage   m_lastMaskUsed;         // last mask applied (render thread only)
};
