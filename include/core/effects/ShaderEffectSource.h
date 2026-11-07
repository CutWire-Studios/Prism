#pragma once

#include "core/effects/EffectPassRunner.h"
#include "core/effects/ShaderEffects.h"
#include "core/sources/MediaSource.h"

#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <memory>

/// Generic decorator that runs a data-driven ShaderEffectSpec as one GPU pass. The program is
/// compiled once per source (cached in the runner) and params only touch uniforms.
/// needsPrevFrame / extraInputs are not honoured yet: the shader only sees u_input.
class ShaderEffectSource : public MediaSource {
public:
    ShaderEffectSource(std::unique_ptr<MediaSource> inner, const ShaderEffectSpec &spec,
                       const QJsonObject &params);

    /// Update parameter values; takes effect on the next frame without recompiling.
    void setParams(const QJsonObject &params) { m_params = params; m_dirty = true; }
    const QJsonObject &params() const { return m_params; }
    bool updateParams(const QJsonObject &params) override { setParams(params); return true; }
    MediaSource *inner() const override { return m_inner.get(); }

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
    void applyUniforms(class QOpenGLShaderProgram *p, QSize size) const;

    std::unique_ptr<MediaSource> m_inner;
    const ShaderEffectSpec *m_spec;
    QByteArray   m_fragment;
    QJsonObject  m_params;
    QElapsedTimer m_clock;
    double       m_presentAt = -1.0;
    bool         m_dirty = false;
    mutable EffectPassRunner m_runner;
    unsigned int m_outTex = 0;
    mutable QImage m_cpu;
    mutable bool   m_cpuValid = false;
};
