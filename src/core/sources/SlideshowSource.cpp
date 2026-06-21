#include "core/sources/SlideshowSource.h"
#include "core/sources/ImageSource.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QImageReader>
#include <QSurfaceFormat>
#include <QDebug>

static const char *kVertexShader =
    "attribute vec2 position;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = vec2(position.x * 0.5 + 0.5, 1.0 - (position.y * 0.5 + 0.5));\n"
    // Flip Y in clip space so the FBO's texel row 0 holds the image's top row,
    // matching VideoWidget's top-row-first texture convention when the FBO
    // texture is sampled directly (no CPU readback/flip).
    "  gl_Position = vec4(position.x, -position.y, 0.0, 1.0);\n"
    "}\n";

static const char *kFragmentHeader =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_from;\n"
    "uniform sampler2D u_to;\n"
    "uniform float u_progress;\n"
    "uniform float u_ratio;\n"
    "vec4 getFromColor(vec2 uv) { return texture2D(u_from, uv); }\n"
    "vec4 getToColor(vec2 uv) { return texture2D(u_to, uv); }\n";

static const char *kFragmentFooter =
    "\nvoid main() { gl_FragColor = transition(v_uv); }\n";

// Cap the slideshow working resolution. Native camera images (e.g. 6720×4480)
// offer no visual benefit on screen but make every transition frame do a huge
// GPU→CPU readback + pixel conversion, which stalls the UI. Clamp the longest
// side; all downstream buffers/textures/readbacks scale with this size.
static constexpr int kMaxSlideDimension = 1920;

static QSize cappedSlideSize(QSize s) {
    if (s.isEmpty()) return s;
    const int longest = qMax(s.width(), s.height());
    if (longest <= kMaxSlideDimension) return s;
    return s.scaled(kMaxSlideDimension, kMaxSlideDimension, Qt::KeepAspectRatio);
}

// ── Load helpers ──────────────────────────────────────────────────────────────

SlideshowSource::~SlideshowSource() {
    destroyGL();
}

void SlideshowSource::setEffect(Effect e) {
    if (m_effect == e) return;
    m_effect = e;
    if (m_transitioning && e == Effect::None)
        m_transitioning = false;
}

QStringList SlideshowSource::effectNames() {
    return {
        "None (hard cut)",
        "Cube 3D",
        "Flip 3D",
        "Page Curl",
        "Doorway",
        "Cross Zoom",
        "Ripple",
        "Glitch",
        "Mosaic Flip",
        "Swirl",
    };
}

QString SlideshowSource::effectShaderPath(Effect effect) {
    switch (effect) {
    case Effect::Cube3D:     return ":/shaders/slideshow/cube.glsl";
    case Effect::Flip3D:     return ":/shaders/slideshow/flip.glsl";
    case Effect::PageCurl:   return ":/shaders/slideshow/pagecurl.glsl";
    case Effect::Doorway:    return ":/shaders/slideshow/doorway.glsl";
    case Effect::CrossZoom:  return ":/shaders/slideshow/crosszoom.glsl";
    case Effect::Ripple:     return ":/shaders/slideshow/ripple.glsl";
    case Effect::Glitch:     return ":/shaders/slideshow/glitch.glsl";
    case Effect::MosaicFlip: return ":/shaders/slideshow/mosaicflip.glsl";
    case Effect::Swirl:      return ":/shaders/slideshow/swirl.glsl";
    default:                 return {};
    }
}

bool SlideshowSource::loadFolder(const QString &folderPath, int intervalMs) {
    m_name = QFileInfo(folderPath).fileName();

    QDir dir(folderPath);
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name | QDir::IgnoreCase);

    QStringList files;
    for (const QString &f : dir.entryList()) {
        if (ImageSource::isStaticImageFile(f))
            files << dir.absoluteFilePath(f);
    }

    return loadFiles(files, intervalMs);
}

bool SlideshowSource::loadFiles(const QStringList &filePaths, int intervalMs) {
    destroyGL();

    m_intervalMs = intervalMs;
    m_paths.clear();
    m_currentSlide = QImage();
    m_fromSlide = QImage();
    m_toSlide = QImage();
    m_current  = 0;
    m_frameSize = {};
    m_elapsed.invalidate();
    m_transitioning = false;

    for (const QString &path : filePaths) {
        QImageReader reader(path);
        if (!reader.canRead()) continue;

        if (m_frameSize.isEmpty()) {
            m_frameSize = cappedSlideSize(reader.size());
            m_currentSlide = loadSlideImage(path);
            if (m_currentSlide.isNull()) {
                m_frameSize = {};
                continue;
            }
        }

        m_paths.append(path);
    }

    return !m_paths.isEmpty();
}

QImage SlideshowSource::loadSlideImage(const QString &path) const {
    QImageReader reader(path);
    // Decode straight to the capped size — JPEG/etc. decoders scale during
    // decode, so we never allocate the full native-resolution image.
    if (!m_frameSize.isEmpty())
        reader.setScaledSize(m_frameSize);
    QImage img = reader.read();
    if (img.isNull()) return {};

    if (img.format() != QImage::Format_RGB888)
        img = img.convertToFormat(QImage::Format_RGB888);
    return img;
}

// ── Offscreen GL ──────────────────────────────────────────────────────────────

bool SlideshowSource::initGL() {
    if (m_frameSize.isEmpty()) return false;

    QSurfaceFormat fmt;
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);

    m_surface = new QOffscreenSurface();
    m_surface->setFormat(fmt);
    m_surface->create();
    if (!m_surface->isValid()) {
        qWarning() << "SlideshowSource: failed to create offscreen surface";
        delete m_surface; m_surface = nullptr;
        return false;
    }

    m_context = new QOpenGLContext();
    m_context->setFormat(fmt);
    // Share with the global context so the FBO texture is usable by VideoWidget.
    if (QOpenGLContext *share = QOpenGLContext::globalShareContext())
        m_context->setShareContext(share);
    if (!m_context->create()) {
        qWarning() << "SlideshowSource: failed to create GL context";
        delete m_surface; m_surface = nullptr;
        delete m_context; m_context = nullptr;
        return false;
    }

    m_context->makeCurrent(m_surface);

    m_fbo = new QOpenGLFramebufferObject(m_frameSize);
    if (!m_fbo->isValid()) {
        qWarning() << "SlideshowSource: failed to create FBO";
        delete m_fbo;     m_fbo     = nullptr;
        m_context->doneCurrent();
        delete m_context; m_context = nullptr;
        delete m_surface; m_surface = nullptr;
        return false;
    }

    auto *f = m_context->functions();
    f->glGenBuffers(1, &m_vbo);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    static const float quad[] = { -1.f,-1.f, 1.f,-1.f, -1.f,1.f, 1.f,1.f };
    f->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);

    f->glGenTextures(1, &m_texFrom);
    f->glGenTextures(1, &m_texTo);

    m_glInitialized = true;
    m_compiled = false;
    m_compiledEffect = Effect::None;

    m_context->doneCurrent();
    return true;
}

void SlideshowSource::destroyGL() {
    if (!m_glInitialized) return;
    m_context->makeCurrent(m_surface);
    auto *f = m_context->functions();
    if (m_vbo) {
        f->glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_texFrom) {
        f->glDeleteTextures(1, &m_texFrom);
        m_texFrom = 0;
    }
    if (m_texTo) {
        f->glDeleteTextures(1, &m_texTo);
        m_texTo = 0;
    }
    delete m_program; m_program = nullptr;
    delete m_fbo;     m_fbo     = nullptr;
    m_context->doneCurrent();
    delete m_context; m_context = nullptr;
    delete m_surface; m_surface = nullptr;
    m_glInitialized = false;
    m_compiled = false;
}

bool SlideshowSource::compileEffect(Effect effect) {
    if (effect == Effect::None) return false;
    if (m_compiled && m_compiledEffect == effect) return true;

    const QString path = effectShaderPath(effect);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "SlideshowSource: cannot open shader" << path;
        m_compiled = false;
        return false;
    }
    const QString body = QString::fromUtf8(file.readAll());
    const QString fragmentSource = QString(kFragmentHeader) + body + QString(kFragmentFooter);

    delete m_program;
    m_program = new QOpenGLShaderProgram();
    m_program->bindAttributeLocation("position", 0);

    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)) {
        qWarning() << "SlideshowSource vertex error:" << m_program->log();
        m_compiled = false;
        return false;
    }
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource)) {
        qWarning() << "SlideshowSource fragment error:" << m_program->log();
        m_compiled = false;
        return false;
    }
    if (!m_program->link()) {
        qWarning() << "SlideshowSource link error:" << m_program->log();
        m_compiled = false;
        return false;
    }

    m_compiled = true;
    m_compiledEffect = effect;
    return true;
}

void SlideshowSource::uploadSlideTexture(unsigned int tex, const QImage &img) {
    if (img.isNull()) return;

    QImage upload = img;
    if (upload.format() != QImage::Format_RGB888 || !upload.isDetached())
        upload = img.convertToFormat(QImage::Format_RGB888);

    auto *f = m_context->functions();
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, upload.width(), upload.height(), 0,
                    GL_RGB, GL_UNSIGNED_BYTE, upload.constBits());
    f->glBindTexture(GL_TEXTURE_2D, 0);
}

void SlideshowSource::renderTransition(float progress) {
    if (!m_compiled || !m_program) return;

    auto *f = m_context->functions();
    const int w = m_frameSize.width(), h = m_frameSize.height();
    const float ratio = h > 0 ? static_cast<float>(w) / h : 1.f;

    m_fbo->bind();
    f->glViewport(0, 0, w, h);
    f->glClearColor(0.f, 0.f, 0.f, 1.f);
    f->glClear(GL_COLOR_BUFFER_BIT);

    m_program->bind();
    m_program->setUniformValue("u_progress", progress);
    m_program->setUniformValue("u_ratio", ratio);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, m_texFrom);
    m_program->setUniformValue("u_from", 0);
    f->glActiveTexture(GL_TEXTURE1);
    f->glBindTexture(GL_TEXTURE_2D, m_texTo);
    m_program->setUniformValue("u_to", 1);
    f->glActiveTexture(GL_TEXTURE0);

    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const int posLoc = m_program->attributeLocation("position");
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

    m_fbo->release();

    // Flush so the FBO texture is complete before VideoWidget's shared context
    // samples it. No pixel readback — the texture is consumed directly on the GPU.
    f->glFlush();
}

// ── MediaSource interface ─────────────────────────────────────────────────────

const uint8_t *SlideshowSource::frameData() const {
    // During a transition the live frame lives in the FBO texture (glTexture());
    // frameData() returns the CPU slide only as a fallback for non-GL consumers
    // (e.g. deck preview thumbnails), which is the outgoing slide mid-transition.
    if (m_paths.isEmpty() || m_currentSlide.isNull()) return nullptr;
    return reinterpret_cast<const uint8_t *>(m_currentSlide.constBits());
}

unsigned int SlideshowSource::glTexture() const {
    if (m_transitioning && m_effect != Effect::None && m_glInitialized && m_fbo)
        return m_fbo->texture();
    return 0;
}

bool SlideshowSource::nextFrame() {
    if (m_paths.isEmpty()) return false;

    if (!m_elapsed.isValid()) {
        m_elapsed.start();
        return false;
    }

    if (m_paths.size() < 2) return false;

    if (m_transitioning && m_effect != Effect::None) {
        if (!m_glInitialized && !initGL()) {
            m_transitioning = false;
            m_fromSlide = QImage();
            m_toSlide = QImage();
            return true;
        }

        m_context->makeCurrent(m_surface);
        if (m_compiledEffect != m_effect)
            compileEffect(m_effect);

        const float progress = qMin(1.0f,
            static_cast<float>(m_transElapsed.elapsed()) / qMax(1, m_transitionMs));
        if (m_compiled)
            renderTransition(progress);
        m_context->doneCurrent();

        if (progress >= 1.0f) {
            m_transitioning = false;
            m_currentSlide = std::move(m_toSlide);
            m_fromSlide = QImage();
        }
        return true;
    }

    if (m_elapsed.elapsed() >= m_intervalMs) {
        m_prevIndex = m_current;
        m_current = (m_current + 1) % m_paths.size();
        m_elapsed.restart();

        if (m_effect != Effect::None) {
            m_fromSlide = loadSlideImage(m_paths[m_prevIndex]);
            m_toSlide = loadSlideImage(m_paths[m_current]);
            if (m_fromSlide.isNull() || m_toSlide.isNull()) {
                if (!m_toSlide.isNull())
                    m_currentSlide = std::move(m_toSlide);
                m_fromSlide = QImage();
                return true;
            }

            if (!m_glInitialized && !initGL()) {
                m_transitioning = false;
                m_currentSlide = std::move(m_toSlide);
                m_fromSlide = QImage();
                return true;
            }

            m_context->makeCurrent(m_surface);
            if (!compileEffect(m_effect)) {
                m_context->doneCurrent();
                m_transitioning = false;
                m_currentSlide = std::move(m_toSlide);
                m_fromSlide = QImage();
                return true;
            }
            uploadSlideTexture(m_texFrom, m_fromSlide);
            uploadSlideTexture(m_texTo, m_toSlide);
            m_transitioning = true;
            m_transElapsed.start();
            renderTransition(0.f);
            m_context->doneCurrent();
            return true;
        }

        m_currentSlide = loadSlideImage(m_paths[m_current]);
        return true;
    }

    return false;
}
