#pragma once

#include "core/sources/MediaSource.h"
#include "core/effects/EffectPassRunner.h"

#include <QElapsedTimer>
#include <QImage>
#include <QSize>
#include <algorithm>
#include <memory>
#include <vector>

/// Flattens a Layer node's inputs into one GPU-resident frame.
///
/// Each sub-layer is drawn (bottom→top, source-over alpha) into a single shared-
/// context FBO at its normalised canvas placement, and the result is exposed via
/// glTexture() so a downstream process effect — and ultimately VideoWidget's
/// compositor — operates on the flattened composite rather than on each layer
/// independently. frameData() falls back to a lazy GPU→CPU read for non-GL
/// consumers (thumbnails) and CPU effects chained after it.
///
/// The bottom layer (index 0) is the "primary": the source delegates its timeline
/// (duration/currentTime/seek/play/pause) to it, so the deck's existing pacing
/// drives the bottom clip's scrub/speed/trim/repeat/audio with no special-casing.
/// The remaining (upper) layers are advanced on their own wall-clock so they play
/// at their native rate regardless of the deck speed applied to the bottom.
class LayerCompositorSource : public MediaSource {
public:
    struct Layer {
        std::unique_ptr<MediaSource> source;
        float bx = 0.f, by = 0.f, bw = 1.f, bh = 1.f;  // normalised canvas placement
        bool  visible = true;
        bool  flipH = false, flipV = false;
        // Self-pacing state for non-primary layers.
        QElapsedTimer clock;
        double        anchor = 0.0;
    };

    LayerCompositorSource(std::vector<Layer> layers, QSize canvas)
        : m_layers(std::move(layers)), m_canvas(canvas) {}

    Type    type()        const override { return Type::Canvas; }
    bool    isReady()     const override { return m_outTex != 0; }
    QSize   frameSize()   const override { return m_canvas; }
    const uint8_t *frameData() const override;
    bool    nextFrame()         override;
    bool    presentAt(double s) override { m_presentAt = s; const bool r = nextFrame(); m_presentAt = -1.0; return r; }
    bool    hasAlpha()    const override { return true; }
    unsigned int glTexture() const override { return m_outTex; }

    // Timeline delegates to the bottom (primary) layer.
    double  duration()    const override { return primary() ? primary()->duration() : 0.0; }
    double  currentTime() const override { return primary() ? primary()->currentTime() : 0.0; }
    void    seek(double s)      override { if (primary()) primary()->seek(s); }
    void    play()  override { if (primary()) primary()->play(); }
    void    pause() override { if (primary()) primary()->pause(); }
    void    setOnAir(bool on) override {
        for (Layer &l : m_layers) if (l.source) l.source->setOnAir(on);
    }
    double  requestOut() override {
        double longest = 0.0;
        for (Layer &l : m_layers) if (l.source) longest = std::max(longest, l.source->requestOut());
        return longest;
    }

private:
    MediaSource *primary() const {
        return m_layers.empty() ? nullptr : m_layers.front().source.get();
    }
    /// Advance one non-primary layer at its native 1× rate; loops at its end.
    static bool advanceTop(Layer &l);

    std::vector<Layer> m_layers;
    QSize              m_canvas;
    double             m_presentAt = -1.0;
    mutable EffectPassRunner m_runner;   // readback() in const frameData()
    unsigned int       m_outTex = 0;
    mutable QImage     m_cpu;
    mutable bool       m_cpuValid = false;
};
