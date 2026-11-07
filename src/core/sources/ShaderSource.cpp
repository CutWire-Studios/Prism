#include "core/sources/ShaderSource.h"
#include "core/effects/Generators.h"
#include "core/effects/ShaderData.h"
#include "core/media/AudioAnalyzer.h"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QDebug>
#include <algorithm>
#include <cmath>

static const char *kVertexShader =
    "attribute vec2 position;\n"
    "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";

ShaderSource::ShaderSource(const QString &fragmentShader, QSize size)
    : m_shaderCode(fragmentShader), m_size(size)
{
    m_paramSpecs = ShaderEffects::parseHeaderParams(fragmentShader);
    m_params = ShaderEffects::defaultParamValues(m_paramSpecs);
    m_timer.start();
}

ShaderSource::~ShaderSource() {
    destroyGL();
}

void ShaderSource::setShaderCode(const QString &code) {
    m_shaderCode  = code;
    m_paramSpecs  = ShaderEffects::parseHeaderParams(code);
    m_params      = ShaderEffects::defaultParamValues(m_paramSpecs);
    m_shaderDirty = true;
    m_compiled    = false;
}

void ShaderSource::setPreset(const ShaderGeneratorSpec &spec) {
    setShaderCode(QString::fromUtf8(Generators::fragmentSource(spec)));
    m_paramSpecs = spec.params;
    m_params = spec.defaultParams();
}

bool ShaderSource::updateParams(const QJsonObject &params) {
    m_params = params;
    return true;
}

void ShaderSource::setAudioSource(const QString &filePath) {
    if (filePath == m_audioSourcePath)
        return;

    if (filePath.isEmpty()) {
        m_analyzer.reset();
        m_audioSourcePath.clear();
        clearAudioSyncState();
        return;
    }
    if (!m_analyzer)
        m_analyzer = std::make_unique<AudioAnalyzer>();
    if (!m_analyzer->open(filePath)) {
        m_analyzer.reset();
        m_audioSourcePath.clear();
        clearAudioSyncState();
        return;
    }
    m_audioSourcePath = filePath;
}

void ShaderSource::setAudioSyncState(double playbackTime, bool playing, double speed) {
    m_audioSyncEnabled = true;
    m_audioSyncTime = std::max(0.0, playbackTime);
    m_audioPlaying = playing;
    m_audioSyncSpeed = (speed > 0.01) ? speed : 1.0;
}

void ShaderSource::clearAudioSyncState() {
    m_audioSyncEnabled = false;
    m_audioSyncTime = 0.0;
    m_audioSyncSpeed = 1.0;
    m_audioPlaying = true;
}

void ShaderSource::setDataSource(std::shared_ptr<ScriptOutput> data) {
    m_dataSource = std::move(data);
    m_dataVersion = 0;
}

bool ShaderSource::nextFrame() {
    const qint64 nowMs = m_timer.elapsed();
    const double delta = (m_lastFrameMs > 0)
        ? (nowMs - m_lastFrameMs) * 0.001
        : 0.0;
    m_lastFrameMs = nowMs;

    if (m_analyzer) {
        if (m_audioSyncEnabled) {
            m_analyzer->setPlaybackSpeed(m_audioSyncSpeed);
            const double drift = std::abs(m_analyzer->currentTime() - m_audioSyncTime);
            if (drift > 0.08)
                m_analyzer->seek(m_audioSyncTime);
            if (m_audioPlaying && delta > 0.0)
                m_analyzer->advance(delta);
        } else if (delta > 0.0) {
            m_analyzer->setPlaybackSpeed(1.0);
            m_analyzer->advance(delta);
        }
    }

    if (!m_glInitialized)
        return initGL();

    m_context->makeCurrent(m_surface);

    if (m_shaderDirty) {
        compileProgram();
        m_shaderDirty = false;
        if (!m_compiled && !m_ready) {
            m_buffer.fill(0, m_size.width() * m_size.height() * 3);
            m_ready = true;
        }
    }

    if (m_compiled) {
        renderToBuffer();
        m_ready = true;
    }

    m_context->doneCurrent();
    return m_ready;
}

bool ShaderSource::initGL() {
    QSurfaceFormat fmt;
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);

    m_surface = new QOffscreenSurface();
    m_surface->setFormat(fmt);
    m_surface->create();
    if (!m_surface->isValid()) {
        qWarning() << "ShaderSource: failed to create offscreen surface";
        delete m_surface; m_surface = nullptr;
        return false;
    }

    m_context = new QOpenGLContext();
    m_context->setFormat(fmt);
    if (!m_context->create()) {
        qWarning() << "ShaderSource: failed to create GL context";
        delete m_context; m_context = nullptr;
        delete m_surface; m_surface = nullptr;
        return false;
    }

    m_context->makeCurrent(m_surface);

    m_fbo = new QOpenGLFramebufferObject(m_size);
    if (!m_fbo->isValid()) {
        qWarning() << "ShaderSource: failed to create FBO";
        delete m_fbo;     m_fbo     = nullptr;
        m_context->doneCurrent();
        delete m_context; m_context = nullptr;
        delete m_surface; m_surface = nullptr;
        return false;
    }

    auto *f = m_context->functions();
    f->glGenBuffers(1, reinterpret_cast<GLuint *>(&m_vbo));
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    static const float quad[] = { -1.f,-1.f, 1.f,-1.f, -1.f,1.f, 1.f,1.f };
    f->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);

    f->glGenTextures(1, &m_spectrumTex);
    f->glBindTexture(GL_TEXTURE_2D, m_spectrumTex);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                    AudioAnalyzer::kBins, 1, 0,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
    f->glBindTexture(GL_TEXTURE_2D, 0);

    m_glInitialized = true;

    compileProgram();
    m_shaderDirty = false;

    if (m_compiled)
        renderToBuffer();
    else
        m_buffer.fill(0, m_size.width() * m_size.height() * 3);

    m_ready = true;
    m_context->doneCurrent();
    return true;
}

void ShaderSource::destroyGL() {
    if (!m_glInitialized) return;
    m_context->makeCurrent(m_surface);
    if (m_spectrumTex) {
        m_context->functions()->glDeleteTextures(1, &m_spectrumTex);
        m_spectrumTex = 0;
    }
    if (m_vbo) {
        m_context->functions()->glDeleteBuffers(1, reinterpret_cast<GLuint *>(&m_vbo));
        m_vbo = 0;
    }
    delete m_program; m_program = nullptr;
    delete m_fbo;     m_fbo     = nullptr;
    m_context->doneCurrent();
    delete m_context; m_context = nullptr;
    delete m_surface; m_surface = nullptr;
    m_glInitialized = false;
}

bool ShaderSource::compileProgram() {
    ++m_compileCount;
    delete m_program;
    m_program = new QOpenGLShaderProgram();

    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)) {
        m_lastError = "Vertex error: " + m_program->log();
        m_compiled  = false;
        return false;
    }
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, m_shaderCode)) {
        m_lastError = m_program->log();
        m_compiled  = false;
        return false;
    }
    if (!m_program->link()) {
        m_lastError = m_program->log();
        m_compiled  = false;
        return false;
    }
    m_lastError.clear();
    m_compiled = true;
    return true;
}

void ShaderSource::applyDataUniforms(const QJsonObject &obj, bool &hasAudioBlock) {
    hasAudioBlock = ShaderData::applyUniforms(m_program, obj);

    const QByteArray spectrum = ShaderData::spectrumBytes(obj);
    const int n = static_cast<int>(spectrum.size());
    if (n > 0) {
        auto *f = m_context->functions();
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, m_spectrumTex);
        if (n != m_spectrumTexWidth) {
            f->glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, n, 1, 0,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, spectrum.constData());
            m_spectrumTexWidth = n;
        } else {
            f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, n, 1,
                               GL_LUMINANCE, GL_UNSIGNED_BYTE, spectrum.constData());
        }
        m_program->setUniformValue("u_spectrum", 0);
        m_program->setUniformValue("u_spectrumSize", n);
    }
}

void ShaderSource::applyParamUniforms() {
    for (const ShaderParam &p : std::as_const(m_paramSpecs)) {
        const QByteArray name = ShaderEffects::uniformName(p);
        const QJsonValue v = m_params.value(p.name);
        switch (p.type) {
        case ShaderParam::Bool:
            m_program->setUniformValue(name.constData(), GLint(v.toBool(p.def != 0.0)));
            break;
        case ShaderParam::Int:
        case ShaderParam::Choice:
            m_program->setUniformValue(name.constData(), GLint(std::lround(v.toDouble(p.def))));
            break;
        case ShaderParam::Color: {
            const QJsonArray a = v.toArray();
            const QColor d = p.defColor;
            m_program->setUniformValue(name.constData(), a.size() == 4
                ? QVector4D(a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble(), a.at(3).toDouble())
                : QVector4D(d.redF(), d.greenF(), d.blueF(), d.alphaF()));
            break;
        }
        case ShaderParam::Point: {
            const QJsonArray a = v.toArray();
            m_program->setUniformValue(name.constData(), a.size() == 2
                ? QVector2D(a.at(0).toDouble(), a.at(1).toDouble())
                : QVector2D(p.defPoint));
            break;
        }
        case ShaderParam::File:
            break;
        default:
            m_program->setUniformValue(name.constData(), GLfloat(v.toDouble(p.def)));
            break;
        }
    }
}

void ShaderSource::renderToBuffer() {
    auto *f = m_context->functions();

    m_fbo->bind();
    f->glViewport(0, 0, m_size.width(), m_size.height());
    f->glClearColor(0.f, 0.f, 0.f, 1.f);
    f->glClear(GL_COLOR_BUFFER_BIT);

    m_program->bind();
    m_program->setUniformValue("u_resolution",
        QVector2D(static_cast<float>(m_size.width()),
                  static_cast<float>(m_size.height())));
    m_program->setUniformValue("u_time",
        static_cast<float>(m_timer.elapsed() * 0.001));

    applyParamUniforms();

    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, m_spectrumTex);
    m_program->setUniformValue("u_spectrum", 0);

    // ── JSON data → shader uniforms ─────────────────────────────────────────
    // The script graph hands us an arbitrary JSON object; we expose everything
    // to the shader and let it pick what it needs. See docs/shader-data-interface.md.
    bool hasAudioBlock = false;
    if (m_dataSource) {
        m_dataVersion = m_dataSource->version.load(std::memory_order_acquire);

        const QJsonObject obj = ShaderData::current(m_dataSource);
        if (!obj.isEmpty())
            applyDataUniforms(obj, hasAudioBlock);
    }
    if (!hasAudioBlock)
        m_program->setUniformValue("u_hasAudio", false);

    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    int posLoc = m_program->attributeLocation("position");
    if (posLoc >= 0) {
        f->glEnableVertexAttribArray(static_cast<GLuint>(posLoc));
        f->glVertexAttribPointer(static_cast<GLuint>(posLoc),
                                 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (posLoc >= 0)
        f->glDisableVertexAttribArray(static_cast<GLuint>(posLoc));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_program->release();

    const int w = m_size.width(), h = m_size.height();
    QByteArray rgba(w * h * 4, 0);
    f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    f->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    // Flip vertically (GL bottom→top → top-to-bottom convention) and strip alpha
    m_buffer.resize(w * h * 3);
    auto       *dst = reinterpret_cast<uint8_t *>(m_buffer.data());
    const auto *src = reinterpret_cast<const uint8_t *>(rgba.constData());
    for (int y = 0; y < h; ++y) {
        const uint8_t *srcRow = src + (h - 1 - y) * w * 4;
        uint8_t       *dstRow = dst + y * w * 3;
        for (int x = 0; x < w; ++x) {
            dstRow[x*3    ] = srcRow[x*4    ];
            dstRow[x*3 + 1] = srcRow[x*4 + 1];
            dstRow[x*3 + 2] = srcRow[x*4 + 2];
        }
    }

    m_fbo->release();
}
