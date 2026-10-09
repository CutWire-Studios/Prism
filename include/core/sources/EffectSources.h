#pragma once

#include "core/sources/MediaSource.h"
#include "core/effects/EffectPassRunner.h"

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QVector>
#include <memory>

// ── CPU frame-processing effect decorators ──────────────────────────────────
// Each class wraps an inner MediaSource, pulls its RGB(A) frames, applies a
// pixel-space transform, and re-exposes the result as an RGBA8888 frame. They
// follow the same decorator pattern as SegmentationSource and are wired into the
// node graph through the ProcessEffects registry.
//
// Output is always RGBA8888 (contiguous, no scanline padding) so frameData()
// can be uploaded directly by VideoWidget regardless of the source format.

/// Common plumbing for a single-frame CPU effect: delegates timing/playback to
/// the inner source and re-runs process() whenever a new frame arrives.
class FrameEffectSource : public MediaSource {
public:
    explicit FrameEffectSource(std::unique_ptr<MediaSource> inner)
        : m_inner(std::move(inner)) {}

    Type    type()        const override { return m_inner ? m_inner->type() : Type::Canvas; }
    bool    isReady()     const override { return m_inner && m_inner->isReady() && !m_output.isNull(); }
    QSize   frameSize()   const override { return m_output.size(); }
    const uint8_t *frameData() const override {
        return m_output.isNull() ? nullptr
                                  : reinterpret_cast<const uint8_t *>(m_output.constBits());
    }
    bool    nextFrame()         override;
    bool    presentAt(double s) override { m_presentAt = s; const bool r = nextFrame(); m_presentAt = -1.0; return r; }
    bool    hasAlpha()    const override { return true; }

    double  duration()    const override { return m_inner ? m_inner->duration() : 0.0; }
    double  currentTime() const override { return m_inner ? m_inner->currentTime() : 0.0; }
    void    seek(double s)      override { if (m_inner) m_inner->seek(s); }
    void    play()             override { if (m_inner) m_inner->play(); }
    void    pause()            override { if (m_inner) m_inner->pause(); }
    void    setOnAir(bool on)  override { if (m_inner) m_inner->setOnAir(on); }
    double  requestOut()       override { return m_inner ? m_inner->requestOut() : 0.0; }
    QString displayName() const override { return m_inner ? m_inner->displayName() : QString(); }

    MediaSource *inner() const { return m_inner.get(); }

protected:
    /// Transform one RGBA8888 input frame into the RGBA8888 output frame.
    virtual QImage process(const QImage &in) = 0;

    std::unique_ptr<MediaSource> m_inner;
    QImage m_output;
    double m_presentAt = -1.0;
};

/// Crops the frame to a normalised [0,1] sub-rectangle, changing the output size.
/// A decorator (rather than a draw-time texcoord fold) so its position in the node
/// chain is honoured relative to other effects (e.g. crop-then-blur ≠ blur-then-crop).
class CropSource : public FrameEffectSource {
public:
    CropSource(std::unique_ptr<MediaSource> inner, float x, float y, float w, float h)
        : FrameEffectSource(std::move(inner)), m_x(x), m_y(y), m_w(w), m_h(h) {}

protected:
    QImage process(const QImage &in) override;

private:
    float m_x = 0.f, m_y = 0.f, m_w = 1.f, m_h = 1.f;
};

/// Mirrors the frame horizontally and/or vertically. A decorator for the same
/// ordering reason as CropSource.
class FlipSource : public FrameEffectSource {
public:
    FlipSource(std::unique_ptr<MediaSource> inner, bool flipH, bool flipV)
        : FrameEffectSource(std::move(inner)), m_h(flipH), m_v(flipV) {}

protected:
    QImage process(const QImage &in) override;

private:
    bool m_h = false, m_v = false;
};

/// Rotates the frame by a fixed multiple of 90 degrees.
class RotateSource : public FrameEffectSource {
public:
    RotateSource(std::unique_ptr<MediaSource> inner, int angleDegrees)
        : FrameEffectSource(std::move(inner)), m_angle(((angleDegrees % 360) + 360) % 360) {}

protected:
    QImage process(const QImage &in) override;

private:
    int m_angle = 0;
};

/// Four-corner perspective (keystone) warp. Corners are normalised [0,1] in
/// order top-left, top-right, bottom-right, bottom-left.
class KeystoneSource : public FrameEffectSource {
public:
    KeystoneSource(std::unique_ptr<MediaSource> inner, const QVector<QPointF> &corners)
        : FrameEffectSource(std::move(inner)), m_corners(corners) {}

protected:
    QImage process(const QImage &in) override;

private:
    QVector<QPointF> m_corners;   // 4 points, normalised
};

/// Keys out pixels close to a target colour, writing them to transparent.
class ChromaKeySource : public FrameEffectSource {
public:
    ChromaKeySource(std::unique_ptr<MediaSource> inner, QColor key,
                    double threshold, double smoothness)
        : FrameEffectSource(std::move(inner)), m_key(key),
          m_threshold(threshold), m_smoothness(smoothness) {}

protected:
    QImage process(const QImage &in) override;

private:
    QColor m_key;
    double m_threshold  = 0.25;
    double m_smoothness = 0.1;
};

/// Separable box blur (repeated to approximate a Gaussian).
class BlurSource : public FrameEffectSource {
public:
    BlurSource(std::unique_ptr<MediaSource> inner, int radius)
        : FrameEffectSource(std::move(inner)), m_radius(radius) {}

protected:
    QImage process(const QImage &in) override;

private:
    int m_radius = 4;
};

/// Unsharp-mask style sharpen; amount scales the high-frequency boost.
class SharpenSource : public FrameEffectSource {
public:
    SharpenSource(std::unique_ptr<MediaSource> inner, double amount)
        : FrameEffectSource(std::move(inner)), m_amount(amount) {}

protected:
    QImage process(const QImage &in) override;

private:
    double m_amount = 1.0;
};

/// Masks the frame to a polygon; pixels outside the polygon become transparent
/// (or inside, when inverted).
class PolygonMaskSource : public FrameEffectSource {
public:
    PolygonMaskSource(std::unique_ptr<MediaSource> inner,
                      const QVector<QPointF> &points, bool invert)
        : FrameEffectSource(std::move(inner)), m_points(points), m_invert(invert) {}

protected:
    QImage process(const QImage &in) override;

private:
    QVector<QPointF> m_points;   // normalised polygon vertices
    bool m_invert = false;
};

/// GPU separable Gaussian-ish blur (proof-of-concept for the shader effect path).
/// Unlike the CPU FrameEffectSource decorators, this renders the inner frame through
/// two fragment-shader passes in an offscreen FBO and exposes the result via
/// glTexture(), so VideoWidget's compositor binds it directly with no CPU readback.
/// frameData() falls back to a lazy GPU→CPU read for CPU effects chained after it.
class GpuBlurSource : public MediaSource {
public:
    GpuBlurSource(std::unique_ptr<MediaSource> inner, int radius)
        : m_inner(std::move(inner)), m_radius(radius) {}

    Type    type()        const override { return m_inner ? m_inner->type() : Type::Canvas; }
    bool    isReady()     const override { return m_inner && m_inner->isReady() && m_outTex != 0; }
    QSize   frameSize()   const override { return m_inner ? m_inner->frameSize() : QSize(); }
    const uint8_t *frameData() const override;
    bool    nextFrame()         override;
    bool    presentAt(double s) override { m_presentAt = s; const bool r = nextFrame(); m_presentAt = -1.0; return r; }
    bool    hasAlpha()    const override { return true; }
    unsigned int glTexture() const override { return m_outTex; }

    double  duration()    const override { return m_inner ? m_inner->duration() : 0.0; }
    double  currentTime() const override { return m_inner ? m_inner->currentTime() : 0.0; }
    void    seek(double s)      override { if (m_inner) m_inner->seek(s); }
    void    play()             override { if (m_inner) m_inner->play(); }
    void    pause()            override { if (m_inner) m_inner->pause(); }
    void    setOnAir(bool on)  override { if (m_inner) m_inner->setOnAir(on); }
    double  requestOut()       override { return m_inner ? m_inner->requestOut() : 0.0; }
    QString displayName() const override { return m_inner ? m_inner->displayName() : QString(); }

private:
    std::unique_ptr<MediaSource> m_inner;
    int          m_radius = 6;
    double       m_presentAt = -1.0;
    mutable EffectPassRunner m_runner;   // readback() in const frameData()
    unsigned int m_outTex = 0;
    mutable QImage m_cpu;        // lazy readback cache for frameData()
    mutable bool   m_cpuValid = false;
};

/// Multiplies the frame alpha by a constant opacity factor.
class OpacitySource : public FrameEffectSource {
public:
    OpacitySource(std::unique_ptr<MediaSource> inner, double opacity)
        : FrameEffectSource(std::move(inner)), m_opacity(opacity) {}

protected:
    QImage process(const QImage &in) override;

private:
    double m_opacity = 1.0;
};
