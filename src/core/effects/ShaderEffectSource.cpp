#include "core/effects/ShaderEffectSource.h"

#include "core/effects/LutLoader.h"

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
    m_aux.resize(size_t(qMax(0, spec.extraInputs)));
    m_auxTex.assign(m_aux.size(), 0);
}

void ShaderEffectSource::setAux(int i, std::unique_ptr<MediaSource> src) {
    if (i < 0 || i >= int(m_aux.size())) return;
    m_aux[size_t(i)] = std::move(src);
    m_auxTex[size_t(i)] = 0;
    m_dirty = true;
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
        case ShaderParam::File:
            break;
        }
    }
}

bool ShaderEffectSource::nextFrame() {
    if (!m_inner) return false;

    std::vector<char> auxNew(m_aux.size(), 0);
    bool anyAuxNew = false;
    for (size_t i = 0; i < m_aux.size(); ++i)
        if (m_aux[i] && m_aux[i]->nextFrame()) { auxNew[i] = 1; anyAuxNew = true; }

    const bool reprocess = m_dirty && m_outTex != 0;
    const bool innerNew = reprocess ? false
                        : m_presentAt >= 0.0 ? m_inner->presentAt(m_presentAt)
                                             : m_inner->nextFrame();
    const bool timeBased = m_spec->needsPrevFrame || m_spec->historyFrames > 0;
    if (!innerNew && !anyAuxNew && !reprocess && !timeBased && m_outTex != 0) return false;
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

    if (timeBased && !m_runner.configureHistory(sz, m_spec->historyFrames, m_spec->needsPrevFrame)) {
        m_outTex = m_runner.passThrough(inTex);
        m_runner.end();
        m_cpuValid = false;
        return true;
    }

    QOpenGLShaderProgram *prog = m_runner.program(m_spec->id, m_fragment.constData());
    if (!prog) { m_runner.end(); return false; }

    std::vector<EffectPassRunner::TexBinding> extras;
    for (size_t i = 0; i < m_aux.size(); ++i) {
        MediaSource *a = m_aux[i].get();
        unsigned int tex = 0;
        if (a && a->isReady()) {
            tex = a->glTexture();
            if (tex == 0) {
                const QSize asz = a->frameSize();
                const uint8_t *d = a->frameData();
                if (m_auxTex[i] != 0 && !auxNew[i]) {
                    tex = m_auxTex[i];
                } else if (d && !asz.isEmpty()) {
                    const QImage wrapped(d, asz.width(), asz.height(), a->frameBytesPerLine(),
                                         a->hasAlpha() ? QImage::Format_RGBA8888
                                                       : QImage::Format_RGB888);
                    tex = m_auxTex[i] = m_runner.uploadSlot(int(i), wrapped);
                }
            }
        }
        if (tex == 0) tex = m_runner.transparentTexture();
        extras.push_back({"u_input" + QByteArray::number(int(i) + 1), tex});
    }
    for (int i = 0; i < m_spec->params.size(); ++i) {
        const ShaderParam &sp = m_spec->params.at(i);
        if (sp.type != ShaderParam::File) continue;
        const QString path = m_params.value(sp.name).toString();
        FileTex &ft = m_files[sp.name];
        if (!ft.loaded || ft.path != path) {
            ft.loaded = true;
            ft.path = path;
            const QImage lut = Lut::load(path);
            ft.tex = lut.isNull() ? 0 : m_runner.uploadSlot(100 + i, lut);
        }
        extras.push_back({ShaderEffects::uniformName(sp),
                          ft.tex != 0 ? ft.tex : m_runner.transparentTexture()});
    }

    const EffectPassRunner::HistoryPlan &hp = m_runner.historyPlan();
    if (m_spec->needsPrevFrame)
        extras.push_back({"u_prev", m_runner.prevTexture()});
    if (m_spec->historyFrames > 0)
        extras.push_back({"u_history", m_runner.historyAtlas()});
    m_outTex = m_runner.runPass(prog, inTex, [this, sz, &hp](QOpenGLShaderProgram *p) {
        applyUniforms(p, sz);
        if (m_spec->historyFrames > 0) {
            p->setUniformValue("u_historyInfo", QVector4D(hp.cols, hp.rows, hp.frames, m_runner.historyHead()));
            p->setUniformValue("u_historyAtlas", QVector2D(hp.atlas.width(), hp.atlas.height()));
            p->setUniformValue("u_historyTile", QVector2D(hp.tile.width(), hp.tile.height()));
        }
    }, extras);
    if (timeBased) m_runner.commitHistory(inTex);
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
