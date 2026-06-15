#include "core/ShaderSource.h"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QDebug>

static const char *kVertexShader =
    "attribute vec2 position;\n"
    "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";

ShaderSource::ShaderSource(const QString &fragmentShader, QSize size)
    : m_shaderCode(fragmentShader), m_size(size)
{
    m_timer.start();
}

ShaderSource::~ShaderSource() {
    destroyGL();
}

void ShaderSource::setShaderCode(const QString &code) {
    m_shaderCode  = code;
    m_shaderDirty = true;
    m_compiled    = false;
}

bool ShaderSource::nextFrame() {
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
        delete m_surface; m_surface = nullptr;
        delete m_context; m_context = nullptr;
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
