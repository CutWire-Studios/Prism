#include "core/effects/ShaderEffectSource.h"

#include <QJsonArray>
#include <QOpenGLShaderProgram>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

ShaderEffectSource::ShaderEffectSource(std::unique_ptr<MediaSource> inner,
                                       const ShaderEffectSpec &spec, const QJsonObject &params)
    : m_inner(std::move(inner)), m_spec(&spec),
      m_fragment(ShaderEffects::fragmentSource(spec)), m_params(params) {
    m_clock.start();
}

void ShaderEffectSource::applyUniforms(QOpenGLShaderProgram *p, QSize size) const {
    p->setUniformValue("u_time", static_cast<float>(m_clock.elapsed() * 0.001));
    p->setUniformValue("u_resolution", QVector2D(size.width(), size.height()));

    // Effect decorators have no audio feed yet; the block stays declared so shaders compile.
    p->setUniformValue("u_bands", QVector3D(0.f, 0.f, 0.f));
    p->setUniformValue("u_bass", 0.f);
    p->setUniformValue("u_mid", 0.f);
    p->setUniformValue("u_treble", 0.f);
    p->setUniformValue("u_audioLevel", 0.f);
    p->setUniformValue("u_beat", 0.f);
    p->setUniformValue("u_hasAudio", 0);

    for (const ShaderParam &sp : m_spec->params) {
        const QByteArray name = ShaderEffects::uniformName(sp);
        const QJsonValue v = m_params.value(sp.name);
        switch (sp.type) {
        case ShaderParam::Float:
            p->setUniformValue(name.constData(), static_cast<float>(v.toDouble(sp.def)));
            break;
        case ShaderParam::Int:
        case ShaderParam::Choice:
            p->setUniformValue(name.constData(), static_cast<int>(v.toDouble(sp.def)));
            break;
        case ShaderParam::Bool:
            p->setUniformValue(name.constData(), v.toBool(sp.def != 0.0) ? 1 : 0);
            break;
        case ShaderParam::Color: {
            const QJsonArray a = v.toArray();
            const QColor d = sp.defColor;
            p->setUniformValue(name.constData(),
                a.size() == 4 ? QVector4D(a.at(0).toDouble(), a.at(1).toDouble(),
                                          a.at(2).toDouble(), a.at(3).toDouble())
                              : QVector4D(d.redF(), d.greenF(), d.blueF(), d.alphaF()));
            break;
        }
        case ShaderParam::Point: {
            const QJsonArray a = v.toArray();
            p->setUniformValue(name.constData(),
                a.size() == 2 ? QVector2D(a.at(0).toDouble(), a.at(1).toDouble())
                              : QVector2D(sp.defPoint.x(), sp.defPoint.y()));
            break;
        }
        }
    }
}

bool ShaderEffectSource::nextFrame() {
    if (!m_inner) return false;

    const bool reprocess = m_dirty && m_outTex != 0;
    const bool innerNew = reprocess ? false
                        : m_presentAt >= 0.0 ? m_inner->presentAt(m_presentAt)
                                             : m_inner->nextFrame();
    if (!innerNew && !reprocess && m_outTex != 0) return false;
    if (!m_inner->isReady()) return false;
    m_dirty = false;

    const QSize sz = m_inner->frameSize();
    if (sz.isEmpty() || m_fragment.isEmpty()) return false;

    if (!m_runner.begin(sz)) return false;

    unsigned int inTex = m_inner->glTexture();
    if (inTex == 0) {
        const uint8_t *data = m_inner->frameData();
        if (!data) { m_runner.end(); return false; }
        const QImage wrapped(data, sz.width(), sz.height(),
                             m_inner->hasAlpha() ? QImage::Format_RGBA8888
                                                 : QImage::Format_RGB888);
        inTex = m_runner.uploadInput(wrapped.convertToFormat(QImage::Format_RGBA8888));
    }

    QOpenGLShaderProgram *prog = m_runner.program(m_spec->id, m_fragment.constData());
    if (!prog) { m_runner.end(); return false; }

    m_outTex = m_runner.runPass(prog, inTex, [this, sz](QOpenGLShaderProgram *p) {
        applyUniforms(p, sz);
    });
    m_runner.end();
    m_cpuValid = false;
    return true;
}

const uint8_t *ShaderEffectSource::frameData() const {
    if (m_outTex == 0)
        return m_inner ? m_inner->frameData() : nullptr;
    if (!m_cpuValid) {
        m_cpu = m_runner.readback();
        m_cpuValid = true;
    }
    return m_cpu.isNull() ? nullptr
                          : reinterpret_cast<const uint8_t *>(m_cpu.constBits());
}
