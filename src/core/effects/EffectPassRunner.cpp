#include "core/effects/EffectPassRunner.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector4D>
#include <QDebug>

// Full-screen quad. Matches SlideshowSource's convention: the vertex shader flips
// Y in clip space so the FBO's texel row 0 holds the image's top row, which is what
// VideoWidget expects when it samples the FBO texture directly (no CPU readback).
static const char *kVertexShader =
    "attribute vec2 position;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = vec2(position.x * 0.5 + 0.5, 1.0 - (position.y * 0.5 + 0.5));\n"
    "  gl_Position = vec4(position.x, -position.y, 0.0, 1.0);\n"
    "}\n";

EffectPassRunner::~EffectPassRunner() {
    destroyGL();
}

bool EffectPassRunner::initGL() {
    if (m_glInit) return true;

    QSurfaceFormat fmt;
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);

    m_surface = new QOffscreenSurface();
    m_surface->setFormat(fmt);
    m_surface->create();
    if (!m_surface->isValid()) {
        qWarning() << "EffectPassRunner: failed to create offscreen surface";
        delete m_surface; m_surface = nullptr;
        return false;
    }

    m_context = new QOpenGLContext();
    m_context->setFormat(fmt);
    if (QOpenGLContext *share = QOpenGLContext::globalShareContext())
        m_context->setShareContext(share);
    if (!m_context->create()) {
        qWarning() << "EffectPassRunner: failed to create GL context";
        delete m_surface; m_surface = nullptr;
        delete m_context; m_context = nullptr;
        return false;
    }

    m_context->makeCurrent(m_surface);
    auto *f = m_context->functions();

    f->glGenBuffers(1, &m_vbo);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    static const float quad[] = { -1.f,-1.f, 1.f,-1.f, -1.f,1.f, 1.f,1.f };
    f->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);

    f->glGenTextures(1, &m_inputTex);

    m_glInit = true;
    return true;
}

void EffectPassRunner::destroyGL() {
    if (!m_glInit) {
        delete m_surface; m_surface = nullptr;
        delete m_context; m_context = nullptr;
        return;
    }
    m_context->makeCurrent(m_surface);
    auto *f = m_context->functions();
    if (m_vbo)      { f->glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_inputTex) { f->glDeleteTextures(1, &m_inputTex); m_inputTex = 0; }
    for (unsigned int t : std::as_const(m_slotTex)) f->glDeleteTextures(1, &t);
    m_slotTex.clear();
    if (m_transparentTex) { f->glDeleteTextures(1, &m_transparentTex); m_transparentTex = 0; }
    qDeleteAll(m_programs);
    m_programs.clear();
    delete m_compositeProg; m_compositeProg = nullptr;
    delete m_fbo[0]; m_fbo[0] = nullptr;
    delete m_fbo[1]; m_fbo[1] = nullptr;
    m_context->doneCurrent();
    delete m_context; m_context = nullptr;
    delete m_surface; m_surface = nullptr;
    m_glInit = false;
    m_size = {};
}

bool EffectPassRunner::ensureFbos(QSize size) {
    if (size.isEmpty()) return false;
    if (m_fbo[0] && m_fbo[1] && m_size == size) return true;

    delete m_fbo[0]; m_fbo[0] = nullptr;
    delete m_fbo[1]; m_fbo[1] = nullptr;
    m_lastFbo = nullptr;

    auto *f = m_context->functions();
    for (int i = 0; i < 2; ++i) {
        m_fbo[i] = new QOpenGLFramebufferObject(size);
        if (!m_fbo[i]->isValid()) {
            qWarning() << "EffectPassRunner: failed to create FBO";
            delete m_fbo[0]; m_fbo[0] = nullptr;
            delete m_fbo[1]; m_fbo[1] = nullptr;
            return false;
        }
        f->glBindTexture(GL_TEXTURE_2D, m_fbo[i]->texture());
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    f->glBindTexture(GL_TEXTURE_2D, 0);
    m_size = size;
    return true;
}

bool EffectPassRunner::begin(QSize size) {
    if (!initGL()) return false;
    m_context->makeCurrent(m_surface);
    if (!ensureFbos(size)) { m_context->doneCurrent(); return false; }
    m_ping = 0;
    m_lastTex = 0;
    return true;
}

void EffectPassRunner::end() {
    if (!m_glInit || !m_context) return;
    // Flush so the FBO texture is complete before another shared context samples it.
    m_context->functions()->glFlush();
    m_context->doneCurrent();
}

unsigned int EffectPassRunner::uploadInput(const QImage &frame) {
    if (!m_glInit || frame.isNull()) return 0;
    QImage rgba = frame.format() == QImage::Format_RGBA8888
                      ? frame : frame.convertToFormat(QImage::Format_RGBA8888);
    auto *f = m_context->functions();
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    f->glBindTexture(GL_TEXTURE_2D, m_inputTex);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba.width(), rgba.height(), 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
    f->glBindTexture(GL_TEXTURE_2D, 0);
    return m_inputTex;
}

unsigned int EffectPassRunner::uploadSlot(int slot, const QImage &frame) {
    if (!m_glInit || frame.isNull()) return 0;
    QImage rgba = frame.format() == QImage::Format_RGBA8888
                      ? frame : frame.convertToFormat(QImage::Format_RGBA8888);
    auto *f = m_context->functions();
    unsigned int &tex = m_slotTex[slot];
    if (tex == 0) f->glGenTextures(1, &tex);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba.width(), rgba.height(), 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
    f->glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

unsigned int EffectPassRunner::transparentTexture() {
    if (!m_glInit) return 0;
    if (m_transparentTex == 0) {
        QImage px(1, 1, QImage::Format_RGBA8888);
        px.fill(Qt::transparent);
        m_transparentTex = uploadSlot(-1, px);
        m_slotTex.remove(-1);
    }
    return m_transparentTex;
}

QOpenGLShaderProgram *EffectPassRunner::program(int key, const char *fragmentSrc) {
    if (auto it = m_programs.constFind(key); it != m_programs.constEnd())
        return it.value();

    auto *prog = new QOpenGLShaderProgram();
    prog->bindAttributeLocation("position", 0);
    if (!prog->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !prog->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSrc) ||
        !prog->link()) {
        qWarning() << "EffectPassRunner: shader build failed:" << prog->log();
        delete prog;
        m_programs.insert(key, nullptr);
        return nullptr;
    }
    m_programs.insert(key, prog);
    return prog;
}

unsigned int EffectPassRunner::runPass(
    QOpenGLShaderProgram *prog, unsigned int inputTex,
    const std::function<void(QOpenGLShaderProgram *)> &setUniforms,
    const std::vector<TexBinding> &extras) {
    if (!prog || !m_fbo[0] || !m_fbo[1]) return inputTex;

    QOpenGLFramebufferObject *target = m_fbo[m_ping];
    auto *f = m_context->functions();

    target->bind();
    f->glViewport(0, 0, m_size.width(), m_size.height());
    f->glClearColor(0.f, 0.f, 0.f, 0.f);
    f->glClear(GL_COLOR_BUFFER_BIT);

    prog->bind();
    prog->setUniformValue("u_texel",
                          QVector2D(1.f / m_size.width(), 1.f / m_size.height()));
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, inputTex);
    prog->setUniformValue("u_input", 0);
    for (size_t i = 0; i < extras.size(); ++i) {
        f->glActiveTexture(GL_TEXTURE1 + static_cast<unsigned int>(i));
        f->glBindTexture(GL_TEXTURE_2D, extras[i].tex);
        prog->setUniformValue(extras[i].uniform.constData(), static_cast<int>(i) + 1);
    }
    f->glActiveTexture(GL_TEXTURE0);
    if (setUniforms) setUniforms(prog);

    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const int posLoc = prog->attributeLocation("position");
    if (posLoc >= 0) {
        f->glEnableVertexAttribArray(static_cast<unsigned int>(posLoc));
        f->glVertexAttribPointer(static_cast<unsigned int>(posLoc),
                                 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (posLoc >= 0)
        f->glDisableVertexAttribArray(static_cast<unsigned int>(posLoc));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    prog->release();
    target->release();

    m_lastTex = target->texture();
    m_lastFbo = target;
    m_ping = 1 - m_ping;
    return m_lastTex;
}

// ── Compositing ─────────────────────────────────────────────────────────────

// Placement vertex shader. Maps the [-1,1] quad into the layer's normalised canvas
// rect u_rect=(bx,by,bw,bh) (y from top). Derived so that a full-canvas layer
// (bx=0,by=0,bw=1,bh=1) reduces exactly to kVertexShader — keeping the composite
// FBO's orientation identical to the blur/blit path VideoWidget already samples.
static const char *kCompositeVertexShader =
    "attribute vec2 position;\n"
    "uniform vec4 u_rect;\n"        // bx, by, bw, bh
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = vec2(position.x * 0.5 + 0.5, 1.0 - (position.y * 0.5 + 0.5));\n"
    "  float ndcx = (2.0 * u_rect.x + u_rect.z - 1.0) + u_rect.z * position.x;\n"
    "  float ndcy = (2.0 * u_rect.y + u_rect.w - 1.0) - u_rect.w * position.y;\n"
    "  gl_Position = vec4(ndcx, ndcy, 0.0, 1.0);\n"
    "}\n";

static const char *kCompositeFragmentShader =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_input;\n"
    "uniform vec2 u_flip;\n"        // (flipH, flipV) as 0/1
    "void main() {\n"
    "  vec2 uv = v_uv;\n"
    "  if (u_flip.x > 0.5) uv.x = 1.0 - uv.x;\n"
    "  if (u_flip.y > 0.5) uv.y = 1.0 - uv.y;\n"
    "  gl_FragColor = texture2D(u_input, uv);\n"
    "}\n";

bool EffectPassRunner::beginComposite(QSize canvas) {
    if (!initGL()) return false;
    m_context->makeCurrent(m_surface);
    if (!ensureFbos(canvas)) { m_context->doneCurrent(); return false; }

    if (!m_compositeProg) {
        m_compositeProg = new QOpenGLShaderProgram();
        m_compositeProg->bindAttributeLocation("position", 0);
        if (!m_compositeProg->addShaderFromSourceCode(QOpenGLShader::Vertex, kCompositeVertexShader) ||
            !m_compositeProg->addShaderFromSourceCode(QOpenGLShader::Fragment, kCompositeFragmentShader) ||
            !m_compositeProg->link()) {
            qWarning() << "EffectPassRunner: composite shader failed:" << m_compositeProg->log();
            delete m_compositeProg; m_compositeProg = nullptr;
            m_context->doneCurrent();
            return false;
        }
    }

    auto *f = m_context->functions();
    m_fbo[0]->bind();
    f->glViewport(0, 0, m_size.width(), m_size.height());
    f->glClearColor(0.f, 0.f, 0.f, 0.f);
    f->glClear(GL_COLOR_BUFFER_BIT);
    f->glEnable(GL_BLEND);
    f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_lastFbo = m_fbo[0];
    m_lastTex = m_fbo[0]->texture();
    return true;
}

void EffectPassRunner::drawCompositeLayer(unsigned int tex, float bx, float by,
                                          float bw, float bh, bool flipH, bool flipV) {
    if (!m_compositeProg || !m_lastFbo || tex == 0) return;
    auto *f = m_context->functions();

    m_compositeProg->bind();
    m_compositeProg->setUniformValue("u_rect", QVector4D(bx, by, bw, bh));
    m_compositeProg->setUniformValue("u_flip",
                                     QVector2D(flipH ? 1.f : 0.f, flipV ? 1.f : 0.f));
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    m_compositeProg->setUniformValue("u_input", 0);

    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const int posLoc = m_compositeProg->attributeLocation("position");
    if (posLoc >= 0) {
        f->glEnableVertexAttribArray(static_cast<unsigned int>(posLoc));
        f->glVertexAttribPointer(static_cast<unsigned int>(posLoc),
                                 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (posLoc >= 0)
        f->glDisableVertexAttribArray(static_cast<unsigned int>(posLoc));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_compositeProg->release();
}

void EffectPassRunner::endComposite() {
    if (!m_glInit || !m_context) return;
    auto *f = m_context->functions();
    f->glDisable(GL_BLEND);
    if (m_lastFbo) m_lastFbo->release();
    f->glFlush();
    m_context->doneCurrent();
}

QImage EffectPassRunner::readback() {
    if (!m_glInit || !m_lastFbo || m_size.isEmpty()) return {};
    m_context->makeCurrent(m_surface);
    m_lastFbo->bind();
    QImage img(m_size, QImage::Format_RGBA8888);
    m_context->functions()->glReadPixels(0, 0, m_size.width(), m_size.height(),
                                         GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    m_lastFbo->release();
    m_context->doneCurrent();
    // No vertical mirror: our passes render with the kVertexShader Y-flip, so the
    // FBO texel row 0 already holds the image's top row. glReadPixels reads the
    // framebuffer bottom-first, which under this convention is the top row — the
    // resulting QImage is already top-first (matching the glTexture() path).
    return img;
}
