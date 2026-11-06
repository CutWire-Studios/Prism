#include "core/render/Compositor.h"
#include "core/media/GpuDevice.h"
#include "core/media/HwAccel.h"
#include "core/sources/ImageSource.h"
#include "core/sources/VideoFileSource.h"
#include "app/transitions/Transition.h"
#include <QCoreApplication>
#include <QEvent>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QPainter>
#include <QPaintDevice>
#include <QFont>
#include <QSurfaceFormat>
#include <QTimer>
#include <algorithm>

int Compositor::s_programWidth  = 1280;
int Compositor::s_deckPreviewWidth  = 640;
int Compositor::s_deckPreviewHeight = 360;
Compositor::OutputScaleMode Compositor::s_outputScaleMode = Compositor::OutputScaleMode::Stretch;
int Compositor::s_programHeight = 720;

Compositor::Compositor(QObject *parent)
    : QObject(parent) {
    m_frameTimer = new QTimer(this);
    connect(m_frameTimer, &QTimer::timeout, this, &Compositor::updateFrame);
    m_frameTimer->start(33); // ~30 FPS
}

Compositor::~Compositor() {
    blockSignals(true);
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
    releaseMediaSources();
    if (m_ctx && makeCurrent()) {
        destroyProgramFbo();
        if (m_textureA)       glDeleteTextures(1, &m_textureA);
        if (m_textureB)       glDeleteTextures(1, &m_textureB);
        clearChainTextures(m_chainTexA);
        clearChainTextures(m_chainTexB);
        doneCurrent();
    }
}

namespace {
// Letterbox/pillarbox the visible (cropped) part of a source inside `bounds` at its own aspect.
QRectF fitSourceRect(const QRectF &bounds, const MediaSource *src, float cw, float ch) {
    const QSize sz = src->frameSize();
    if (sz.isEmpty() || cw <= 0.f || ch <= 0.f || bounds.isEmpty()) return bounds;
    const double srcAR = (sz.width() * double(cw)) / (sz.height() * double(ch));
    const double s = std::min(bounds.width() / srcAR, bounds.height());
    const double w = s * srcAR, h = s;
    return QRectF(bounds.x() + (bounds.width() - w) / 2.0, bounds.y() + (bounds.height() - h) / 2.0, w, h);
}
} // namespace

void Compositor::shutdownPlayback() {
    releaseMediaSources();
}

void Compositor::releaseMediaSources() {
    if (m_frameTimer) {
        m_frameTimer->stop();
        disconnect(m_frameTimer, nullptr, this, nullptr);
    }
    m_playingA = false;
    m_playingB = false;
    m_sourceA.reset();
    m_sourceB.reset();
    m_chainA.clear();
    m_chainB.clear();
}

// ── GL context ────────────────────────────────────────────────────────────────

bool Compositor::isValid() {
    if (m_ctx) return true;
    if (m_contextFailed) return false;

    QOpenGLContext *shared = QOpenGLContext::globalShareContext();
    if (shared && !shared->isValid())
        shared = nullptr;

    QSurfaceFormat format = shared ? shared->format() : QSurfaceFormat::defaultFormat();

    auto surface = std::make_unique<QOffscreenSurface>();
    surface->setFormat(format);
    surface->create();
    if (!surface->isValid()) {
        qWarning("Compositor: failed to create offscreen surface");
        m_contextFailed = true;
        return false;
    }

    auto ctx = std::make_unique<QOpenGLContext>();
    if (shared)
        ctx->setShareContext(shared);
    ctx->setFormat(format);
    if (!ctx->create()) {
        qWarning("Compositor: failed to create OpenGL context");
        m_contextFailed = true;
        return false;
    }

    m_sharesWithGui = shared && QOpenGLContext::areSharing(ctx.get(), shared);
    m_surface = std::move(surface);
    m_ctx = std::move(ctx);
    return true;
}

bool Compositor::makeCurrent() {
    if (!isValid()) return false;
    if (m_ctxDepth == 0) {
        QOpenGLContext *prev = QOpenGLContext::currentContext();
        m_prevContext = prev != m_ctx.get() ? prev : nullptr;
        m_prevSurface = m_prevContext ? m_prevContext->surface() : nullptr;
    }
    if (!m_ctx->makeCurrent(m_surface.get()))
        return false;
    ++m_ctxDepth;
    if (!m_glReady) {
        initializeOpenGLFunctions();
        // Hardware decode picks its device by the GPU that draws; tell it which one that is before
        // any clip opens.
        if (prism::hwaccel::renderVendor().isEmpty()) {
            if (const char *vendor = reinterpret_cast<const char *>(glGetString(GL_VENDOR)))
                prism::hwaccel::setRenderVendor(QString::fromUtf8(vendor));
            prism::gpu::probeRenderDrmNode();
        }
        m_glReady = true;
    }
    return true;
}

void Compositor::doneCurrent() {
    if (m_ctxDepth <= 0) return;
    if (--m_ctxDepth > 0) return;
    if (m_prevContext && m_prevSurface)
        m_prevContext->makeCurrent(m_prevSurface);
    else
        m_ctx->doneCurrent();
    m_prevContext = nullptr;
    m_prevSurface = nullptr;
}

bool Compositor::renderNow() {
    if (!makeCurrent()) return false;
    composeProgramFrame();
    // Display contexts sample these textures; flush so the writes are visible to them.
    glFlush();
    doneCurrent();
    m_dirty = false;
    emit frameReady();
    return true;
}

bool Compositor::wantsProgramFrame() const {
    return m_programFrameConsumers > 0 || (!m_sharesWithGui && m_viewers[0] > 0);
}

bool Compositor::wantsDeckFrame() const {
    return m_deckFrameConsumers > 0 || (!m_sharesWithGui && (m_viewers[1] > 0 || m_viewers[2] > 0));
}

void Compositor::addViewer(int source) {
    if (source >= 0 && source < 3) ++m_viewers[source];
}

void Compositor::removeViewer(int source) {
    if (source >= 0 && source < 3) m_viewers[source] = std::max(0, m_viewers[source] - 1);
}

QSize Compositor::programTextureSize() const {
    return {s_programWidth, s_programHeight};
}

GLuint Compositor::deckTexture(bool deckA) const {
    return deckA ? m_deckColorTexA : m_deckColorTexB;
}

std::pair<float,float> Compositor::computeDeckAlphas() const {
    const float t = std::clamp(m_crossfadeB, 0.f, 1.f);
    switch (m_transitionMode) {
    case TransitionMode::Crossfade:
    case TransitionMode::Additive:
    case TransitionMode::CrossZoom:
    case TransitionMode::VortexSpin:
    case TransitionMode::Gallery3D:
    case TransitionMode::Cube3D:
    case TransitionMode::Flip3D:
        return {1.f - t, t};
    case TransitionMode::Cut:
        return t < 0.5f ? std::make_pair(1.f, 0.f) : std::make_pair(0.f, 1.f);
    case TransitionMode::WipeLeft:
    case TransitionMode::WipeRight:
    case TransitionMode::WipeUp:
    case TransitionMode::WipeDown:
    case TransitionMode::SlideLeft:
    case TransitionMode::SlideRight:
    case TransitionMode::SlideUp:
    case TransitionMode::SlideDown:
    case TransitionMode::SplitDoor:
    case TransitionMode::SplitDoorVert:
    case TransitionMode::SplitQuadrants:
        // Both decks render at full opacity; masking is done geometrically.
        return {1.f, t > 0.f ? 1.f : 0.f};
    case TransitionMode::DipToBlack:
    case TransitionMode::DipToWhite:
        return {std::max(0.f, 1.f - 2.f * t), std::max(0.f, 2.f * t - 1.f)};
    }
    return {1.f - t, t};
}

void Compositor::composeProgramFrame() {
    ensureProgramFbo();
    ensureDeckFbos();

    m_compW = s_programWidth;
    m_compH = s_programHeight;

    renderDeckToFbo(true);
    renderDeckToFbo(false);

    const bool wantDeckFrame   = wantsDeckFrame();
    const bool wantDeckPreview = m_deckPreviewConsumers > 0;
    if (wantDeckFrame) {
        cacheDeckFrameFromFbo(true);
        cacheDeckFrameFromFbo(false);
    }
    if (wantDeckPreview) {
        if (wantDeckFrame) {
            // Full-res deck frames were just read back; derive the previews from
            // them instead of a second glReadPixels per deck.
            m_deckPreviewA = m_deckFrameCacheA.scaled(s_deckPreviewWidth, s_deckPreviewHeight,
                                                      Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            m_deckPreviewB = m_deckFrameCacheB.scaled(s_deckPreviewWidth, s_deckPreviewHeight,
                                                      Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        } else {
            cacheDeckPreviewFromFbo(true);
            cacheDeckPreviewFromFbo(false);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_programFbo);
    glViewport(0, 0, m_compW, m_compH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, m_compW, m_compH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    renderCompositionGL();
    if (wantsProgramFrame())
        cacheProgramFrameFromFbo();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_compW = 0;
    m_compH = 0;
}

void Compositor::renderDeckToFbo(bool deckA) {
    const GLuint fbo = deckA ? m_deckFboA : m_deckFboB;
    QRectF &outRect = deckA ? m_videoRectProgramA : m_videoRectProgramB;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, s_programWidth, s_programHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, s_programWidth, s_programHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    outRect = QRectF();

    if (m_panicOverlay != PanicOverlay::None) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    const GLuint deckTex = deckA
        ? ((m_sourceA && m_sourceA->glTexture()) ? m_sourceA->glTexture() : m_textureA)
        : ((m_sourceB && m_sourceB->glTexture()) ? m_sourceB->glTexture() : m_textureB);
    MediaSource *src = deckA ? m_sourceA.get() : m_sourceB.get();
    const float cx = deckA ? m_cropXA : m_cropXB;
    const float cy = deckA ? m_cropYA : m_cropYB;
    const float cw = deckA ? m_cropWA : m_cropWB;
    const float ch = deckA ? m_cropHA : m_cropHB;
    const float baseX = deckA ? m_baseXA : m_baseXB;
    const float baseY = deckA ? m_baseYA : m_baseYB;
    const float baseW = deckA ? m_baseWA : m_baseWB;
    const float baseH = deckA ? m_baseHA : m_baseHB;
    const int canvasW = deckA ? m_canvasWidthA : m_canvasWidthB;
    const int canvasH = deckA ? m_canvasHeightA : m_canvasHeightB;
    auto &chain = deckA ? m_chainA : m_chainB;
    auto &chainTex = deckA ? m_chainTexA : m_chainTexB;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (deckTex && src && src->isReady()) {
        const int rw = renderW(), rh = renderH();
        QRectF canvasBounds(0, 0, rw, rh);
        if (canvasW > 0 && canvasH > 0) {
            float canvasAR = (float)canvasW / canvasH;
            float windowAR = rh > 0 ? (float)rw / rh : canvasAR;

            float canvasRW, canvasRH;
            if (canvasAR > windowAR) {
                canvasRW = rw;
                canvasRH = canvasRW / canvasAR;
            } else {
                canvasRH = rh;
                canvasRW = canvasRH * canvasAR;
            }
            canvasBounds = QRectF((rw - canvasRW) / 2, (rh - canvasRH) / 2, canvasRW, canvasRH);
        }

        QRectF bounds(canvasBounds.left() + baseX * canvasBounds.width(),
                      canvasBounds.top()  + baseY * canvasBounds.height(),
                      baseW * canvasBounds.width(),
                      baseH * canvasBounds.height());
        if (canvasW <= 0 || canvasH <= 0)
            bounds = fitSourceRect(bounds, src, cw, ch);
        outRect = bounds;

        const bool flipH = deckA ? m_flipHA : m_flipHB;
        const bool flipV = deckA ? m_flipVA : m_flipVB;
        glColor4f(1.f, 1.f, 1.f, 1.f);
        renderTexture(deckTex, cx, cy, cw, ch,
                      (float)bounds.x(),     (float)bounds.y(),
                      (float)bounds.width(), (float)bounds.height(),
                      flipH, flipV);
    }

    drawChainSources(chain, chainTex, 1.f, canvasW, canvasH);

    glDisable(GL_BLEND);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Compositor::renderCompositionGL() {
    if (m_panicOverlay == PanicOverlay::Blackout) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }

    if (m_panicOverlay == PanicOverlay::StayTuned) {
        glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }

    if (m_transitionMode == TransitionMode::DipToWhite && !m_singleStream) {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    const int rw = renderW(), rh = renderH();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Single-stream output: show deck A directly, no crossfade/transition.
    if (m_singleStream) {
        if (m_deckColorTexA)
            renderFboTexture(m_deckColorTexA, 0.f, 0.f, (float)rw, (float)rh, 1.f);
        glDisable(GL_BLEND);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        return;
    }

    const float t = std::clamp(m_crossfadeB, 0.f, 1.f);

    auto drawDeckFbo = [&](GLuint deckTex, float alpha) {
        if (alpha <= 0.f || !deckTex) return;
        renderFboTexture(deckTex, 0.f, 0.f, (float)rw, (float)rh, alpha);
    };

    auto drawSideA = [&](float alpha) { drawDeckFbo(m_deckColorTexA, alpha); };
    auto drawSideB = [&](float alpha) { drawDeckFbo(m_deckColorTexB, alpha); };

    const bool inB = m_transitionTowardB;
    const float p  = inB ? t : 1.f - t;

    Transition::Context ctx;
    ctx.width   = rw;
    ctx.height  = rh;
    ctx.t       = t;
    ctx.p       = p;
    ctx.drawOut = [&](float a) { inB ? drawSideA(a) : drawSideB(a); };
    ctx.drawIn  = [&](float a) { inB ? drawSideB(a) : drawSideA(a); };
    ctx.texA    = m_deckColorTexA;
    ctx.texB    = m_deckColorTexB;
    // Deck FBO upright sampling for 2D is handled in renderFboTexture(). 3D quads are
    // Y-up (+Y = top), so use source-style V coords (texFlipped=false) — not the 2D flip.
    ctx.texFlipped = false;
    ctx.readyA  = m_deckColorTexA && m_sourceA && m_sourceA->isReady();
    ctx.readyB  = m_deckColorTexB && m_sourceB && m_sourceB->isReady();
    Transition::forMode(m_transitionMode).paint(ctx);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDisable(GL_BLEND);
    glColor4f(1.f, 1.f, 1.f, 1.f);

}

void Compositor::ensureProgramFbo() {
    if (m_programFbo) return;

    glGenFramebuffers(1, &m_programFbo);
    glGenTextures(1, &m_programColorTex);

    glBindTexture(GL_TEXTURE_2D, m_programColorTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s_programWidth, s_programHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_programFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_programColorTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Compositor::ensureDeckFbos() {
    if (m_deckFboA) return;

    auto setupDeckFbo = [&](GLuint &fbo, GLuint &colorTex) {
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &colorTex);

        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s_programWidth, s_programHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };

    setupDeckFbo(m_deckFboA, m_deckColorTexA);
    setupDeckFbo(m_deckFboB, m_deckColorTexB);
}

void Compositor::destroyProgramFbo() {
    if (m_deckColorTexA) { glDeleteTextures(1, &m_deckColorTexA); m_deckColorTexA = 0; }
    if (m_deckFboA)      { glDeleteFramebuffers(1, &m_deckFboA); m_deckFboA = 0; }
    if (m_deckColorTexB) { glDeleteTextures(1, &m_deckColorTexB); m_deckColorTexB = 0; }
    if (m_deckFboB)      { glDeleteFramebuffers(1, &m_deckFboB); m_deckFboB = 0; }
    if (m_programColorTex) { glDeleteTextures(1, &m_programColorTex); m_programColorTex = 0; }
    if (m_programFbo)      { glDeleteFramebuffers(1, &m_programFbo); m_programFbo = 0; }
}

void Compositor::setProgramResolution(int width, int height) {
    if (width == s_programWidth && height == s_programHeight)
        return;

    s_programWidth  = width;
    s_programHeight = height;
    if (width >= height) {
        s_deckPreviewWidth  = 640;
        s_deckPreviewHeight = std::max(1, qRound(640.0 * height / width));
    } else {
        s_deckPreviewHeight = 640;
        s_deckPreviewWidth  = std::max(1, qRound(640.0 * width / height));
    }

    // Program/deck FBOs are lazily (re)created at the new size on the next
    // composeProgramFrame() (see ensureProgramFbo/ensureDeckFbos).
    if (m_ctx && makeCurrent()) {
        destroyProgramFbo();
        doneCurrent();
    }
    m_programFrameCache = QImage();
    m_deckFrameCacheA   = QImage();
    m_deckFrameCacheB   = QImage();
    m_deckPreviewA      = QImage();
    m_deckPreviewB      = QImage();
    markDirty();
}

void Compositor::setOutputScaleMode(OutputScaleMode mode) {
    s_outputScaleMode = mode;
    markDirty();
}

QRectF Compositor::programRectOnSurface(const QSizeF &surface, const QSizeF &program, OutputScaleMode mode) {
    const QRectF full(QPointF(0, 0), surface);
    if (mode == OutputScaleMode::Stretch || program.isEmpty() || surface.isEmpty())
        return full;
    const double sx = surface.width()  / program.width();
    const double sy = surface.height() / program.height();
    const double s = mode == OutputScaleMode::Fit ? std::min(sx, sy) : std::max(sx, sy);
    const double w = program.width() * s;
    const double h = program.height() * s;
    return QRectF((surface.width() - w) / 2.0, (surface.height() - h) / 2.0, w, h);
}

void Compositor::addProgramFrameConsumer() {
    ++m_programFrameConsumers;
}

void Compositor::removeProgramFrameConsumer() {
    m_programFrameConsumers = std::max(0, m_programFrameConsumers - 1);
}

void Compositor::setProgramFrameConsumerCount(int count) {
    m_programFrameConsumers = std::max(0, count);
}

void Compositor::addDeckPreviewConsumer() {
    ++m_deckPreviewConsumers;
}

void Compositor::removeDeckPreviewConsumer() {
    m_deckPreviewConsumers = std::max(0, m_deckPreviewConsumers - 1);
}

void Compositor::setDeckPreviewConsumerCount(int count) {
    m_deckPreviewConsumers = std::max(0, count);
}

void Compositor::setDeckFrameConsumerCount(int count) {
    m_deckFrameConsumers = std::max(0, count);
}

void Compositor::cacheDeckFrameFromFbo(bool deckA) {
    const GLuint fbo = deckA ? m_deckFboA : m_deckFboB;
    if (!fbo) return;

    QImage &cache = deckA ? m_deckFrameCacheA : m_deckFrameCacheB;
    cache = QImage(s_programWidth, s_programHeight, QImage::Format_RGBA8888);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, s_programWidth, s_programHeight,
                 GL_RGBA, GL_UNSIGNED_BYTE, cache.bits());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    cache = cache.mirrored(false, true);
}

void Compositor::cacheProgramFrameFromFbo() {
    if (!m_programFbo) return;

    m_programFrameCache = QImage(s_programWidth, s_programHeight, QImage::Format_RGBA8888);
    glReadPixels(0, 0, s_programWidth, s_programHeight,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_programFrameCache.bits());
}

void Compositor::cacheDeckPreviewFromFbo(bool deckA) {
    const GLuint fbo = deckA ? m_deckFboA : m_deckFboB;
    if (!fbo) return;

    QImage full(s_programWidth, s_programHeight, QImage::Format_RGBA8888);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, s_programWidth, s_programHeight,
                 GL_RGBA, GL_UNSIGNED_BYTE, full.bits());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    full = full.mirrored(false, true);
    QImage &cache = deckA ? m_deckPreviewA : m_deckPreviewB;
    cache = full.scaled(s_deckPreviewWidth, s_deckPreviewHeight,
                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// ── Texture rendering ─────────────────────────────────────────────────────────

void Compositor::renderTexture(GLuint tex, float cx, float cy, float cw, float ch,
                                float dstX, float dstY, float dstW, float dstH,
                                bool flipH, bool flipV) {
    float u0 = cx, u1 = cx + cw;
    float v0 = cy, v1 = cy + ch;
    if (flipH) std::swap(u0, u1);
    if (flipV) std::swap(v0, v1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(dstX,        dstY);
    glTexCoord2f(u1, v0); glVertex2f(dstX + dstW, dstY);
    glTexCoord2f(u1, v1); glVertex2f(dstX + dstW, dstY + dstH);
    glTexCoord2f(u0, v1); glVertex2f(dstX,        dstY + dstH);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Compositor::renderFboTexture(GLuint tex, float dstX, float dstY, float dstW, float dstH,
                                   float alpha) {
    if (!tex || alpha <= 0.f) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1.f, 1.f, 1.f, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0.f, 0.f); glVertex2f(dstX,      dstY);
    glTexCoord2f(1.f, 0.f); glVertex2f(dstX + dstW, dstY);
    glTexCoord2f(1.f, 1.f); glVertex2f(dstX + dstW, dstY + dstH);
    glTexCoord2f(0.f, 1.f); glVertex2f(dstX,      dstY + dstH);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Compositor::renderOverlays(QPainter &p, const QList<OverlayItem> &overlays,
                                 const QRectF &videoRect, float globalAlpha) {
    if (globalAlpha <= 0.f) return;

    const QRectF &vr = videoRect.isEmpty()
                     ? QRectF(0, 0, p.device()->width(), p.device()->height())
                     : videoRect;

    for (const auto &ov : overlays) {
        if (!ov.visible) continue;
        QRectF r(vr.left() + ov.x * vr.width(),
                 vr.top()  + ov.y * vr.height(),
                 ov.w * vr.width(),
                 ov.h * vr.height());
        p.setOpacity(static_cast<double>(ov.opacity * globalAlpha));
        if (ov.type == OverlayItem::Type::Text) {
            QFont f;
            f.setPixelSize(std::max(8, static_cast<int>(ov.fontSize * vr.width() / 1280.0)));
            p.setFont(f);
            p.setPen(ov.color);
            p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, ov.content);
        } else {
            const QSize targetSize = r.size().toSize();
            if (!targetSize.isEmpty()) {
                const QString key = ov.content + QLatin1Char('@')
                                  + QString::number(targetSize.width()) + QLatin1Char('x')
                                  + QString::number(targetSize.height());
                auto it = m_overlayScaledCache.constFind(key);
                if (it == m_overlayScaledCache.constEnd()) {
                    if (!m_overlayPixCache.contains(ov.content))
                        m_overlayPixCache.insert(ov.content, QPixmap(ov.content));
                    const QPixmap &orig = m_overlayPixCache[ov.content];
                    QPixmap scaled = orig.isNull()
                        ? QPixmap()
                        : orig.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    it = m_overlayScaledCache.insert(key, scaled);
                }
                if (!it.value().isNull())
                    p.drawPixmap(r.toRect(), it.value());
            }
        }
    }
    p.setOpacity(1.0);
}

// ── Load ─────────────────────────────────────────────────────────────────────

// Private helper: detect type, construct the right MediaSource, upload first frame.
void Compositor::loadSourceInternal(const QString &filePath,
                                     std::unique_ptr<MediaSource> &target,
                                     GLuint &tex, bool &playing) {
    playing = false;

    std::unique_ptr<MediaSource> source;

    if (ImageSource::isStaticImageFile(filePath)) {
        auto img = std::make_unique<ImageSource>();
        if (!img->load(filePath)) return;
        source = std::move(img);
    } else {
        auto vid = std::make_unique<VideoFileSource>();
        if (!vid->open(filePath)) return;
        source = std::move(vid);
    }

    target = std::move(source);

    if (!isValid()) {
        markDirty();
        return;
    }
    makeCurrent();
    setupTextureGL(tex, target->frameSize());
    target->nextFrame();                     // prime first frame (no-op for images)
    uploadSourceFrameGL(tex, target.get());
    doneCurrent();
    markDirty();
}

void Compositor::loadVideoA(const QString &filePath) {
    loadSourceInternal(filePath, m_sourceA, m_textureA, m_playingA);
}

void Compositor::loadVideoB(const QString &filePath) {
    loadSourceInternal(filePath, m_sourceB, m_textureB, m_playingB);
}

void Compositor::setSourceA(std::unique_ptr<MediaSource> source) {
    m_playingA = false;
    m_clockDirtyA = true;
    m_sourceA  = std::move(source);
    if (!m_sourceA) return;
    if (isValid()) {
        makeCurrent();
        // frameSize() may be (0,0) for async sources like Camera; uploadSourceFrameGL
        // handles resizing the texture when the first real frame arrives.
        setupTextureGL(m_textureA, m_sourceA->frameSize());
        if (m_sourceA->nextFrame()) {
            // nextFrame() may switch GL contexts internally (e.g. DynamicInterfaceSource
            // creates its own context for offscreen QML rendering). Re-assert ours.
            m_ctx->makeCurrent(m_surface.get());
            uploadSourceFrameGL(m_textureA, m_sourceA.get());
        }
        doneCurrent();
    }
    // Live sources deliver frames continuously via the frame timer.
    const auto t = m_sourceA->type();
    m_playingA = (t == MediaSource::Type::Camera    ||
                  t == MediaSource::Type::Screen     ||
                  t == MediaSource::Type::Window     ||
                  t == MediaSource::Type::Slideshow  ||
                  t == MediaSource::Type::Canvas     ||
                  t == MediaSource::Type::Ndi);
    markDirty();
}

void Compositor::setSourceB(std::unique_ptr<MediaSource> source) {
    m_playingB = false;
    m_clockDirtyB = true;
    m_sourceB  = std::move(source);
    if (!m_sourceB) return;
    if (isValid()) {
        makeCurrent();
        setupTextureGL(m_textureB, m_sourceB->frameSize());
        if (m_sourceB->nextFrame()) {
            m_ctx->makeCurrent(m_surface.get());
            uploadSourceFrameGL(m_textureB, m_sourceB.get());
        }
        doneCurrent();
    }
    const auto t = m_sourceB->type();
    m_playingB = (t == MediaSource::Type::Camera    ||
                  t == MediaSource::Type::Screen     ||
                  t == MediaSource::Type::Window     ||
                  t == MediaSource::Type::Slideshow  ||
                  t == MediaSource::Type::Canvas     ||
                  t == MediaSource::Type::Ndi);
    markDirty();
}

void Compositor::clearDeckA() {
    setSourceA(nullptr);
    setNodeChainA({});
}

void Compositor::clearDeckB() {
    setSourceB(nullptr);
    setNodeChainB({});
}

void Compositor::adoptSourceA(std::unique_ptr<MediaSource> source) {
    m_playingA = false;
    m_sourceA  = std::move(source);
}

void Compositor::adoptSourceB(std::unique_ptr<MediaSource> source) {
    m_playingB = false;
    m_sourceB  = std::move(source);
}

// ── Playback control ──────────────────────────────────────────────────────────

void Compositor::playA()  { if (m_sourceA) { m_playingA = true; m_clockDirtyA = true; } }
void Compositor::pauseA() { m_playingA = false; }
void Compositor::playB()  { if (m_sourceB) { m_playingB = true; m_clockDirtyB = true; } }
void Compositor::pauseB() { m_playingB = false; }

void Compositor::stop() {
    m_playingA = false;
    m_playingB = false;
}

void Compositor::setDeckSpeed(bool deckA, double speed) {
    if (speed <= 0.0) speed = 1.0;
    // Re-anchor the play clock so already-elapsed time isn't re-scaled by the
    // new rate (which would jump the position).
    if (deckA) { m_speedA = speed; m_clockDirtyA = true; }
    else       { m_speedB = speed; m_clockDirtyB = true; }
}

void Compositor::seekA(double seconds) {
    if (!m_sourceA || !m_sourceA->isReady()) return;
    m_clockDirtyA = true;
    m_sourceA->seek(seconds);
    if (m_sourceA->nextFrame()) {
        if (makeCurrent()) {
            uploadSourceFrameGL(m_textureA, m_sourceA.get());
            doneCurrent();
        }
        markDirty();
    }
}

void Compositor::seekB(double seconds) {
    if (!m_sourceB || !m_sourceB->isReady()) return;
    m_clockDirtyB = true;
    m_sourceB->seek(seconds);
    if (m_sourceB->nextFrame()) {
        if (makeCurrent()) {
            uploadSourceFrameGL(m_textureB, m_sourceB.get());
            doneCurrent();
        }
        markDirty();
    }
}

// ── Trim ─────────────────────────────────────────────────────────────────────

void Compositor::setTrimPointsA(double s, double e) { m_trimStartA = s; m_trimEndA = e; }
void Compositor::setTrimPointsB(double s, double e) { m_trimStartB = s; m_trimEndB = e; }

// ── Crop ─────────────────────────────────────────────────────────────────────

void Compositor::setCropA(float x, float y, float w, float h) {
    m_cropXA = x; m_cropYA = y; m_cropWA = w; m_cropHA = h;
    markDirty();
}

void Compositor::setCropB(float x, float y, float w, float h) {
    m_cropXB = x; m_cropYB = y; m_cropWB = w; m_cropHB = h;
    markDirty();
}

void Compositor::setBaseA(float x, float y, float w, float h) {
    m_baseXA = x; m_baseYA = y; m_baseWA = w; m_baseHA = h;
    markDirty();
}

void Compositor::setBaseB(float x, float y, float w, float h) {
    m_baseXB = x; m_baseYB = y; m_baseWB = w; m_baseHB = h;
    markDirty();
}

void Compositor::setFlipA(bool flipH, bool flipV) {
    m_flipHA = flipH; m_flipVA = flipV;
    markDirty();
}

void Compositor::setFlipB(bool flipH, bool flipV) {
    m_flipHB = flipH; m_flipVB = flipV;
    markDirty();
}

void Compositor::setSingleStreamMode(bool enabled) {
    if (m_singleStream == enabled) return;
    m_singleStream = enabled;
    markDirty();
}

void Compositor::swapDeckContents() {
    using std::swap;
    swap(m_sourceA, m_sourceB);
    swap(m_textureA, m_textureB);
    swap(m_playingA, m_playingB);
    swap(m_repeatA, m_repeatB);
    swap(m_trimStartA, m_trimStartB);
    swap(m_trimEndA, m_trimEndB);
    swap(m_playClockA, m_playClockB);
    swap(m_clockAnchorA, m_clockAnchorB);
    // Speed is a deck property and does not swap with the content; re-anchor
    // both clocks so accumulated elapsed time isn't re-scaled by the other
    // deck's rate.
    m_clockDirtyA = true;
    m_clockDirtyB = true;
    swap(m_cropXA, m_cropXB); swap(m_cropYA, m_cropYB);
    swap(m_cropWA, m_cropWB); swap(m_cropHA, m_cropHB);
    swap(m_flipHA, m_flipHB); swap(m_flipVA, m_flipVB);
    swap(m_baseXA, m_baseXB); swap(m_baseYA, m_baseYB);
    swap(m_baseWA, m_baseWB); swap(m_baseHA, m_baseHB);
    swap(m_canvasWidthA, m_canvasWidthB);
    swap(m_canvasHeightA, m_canvasHeightB);
    swap(m_overlaysA, m_overlaysB);
    swap(m_chainA, m_chainB);
    swap(m_chainTexA, m_chainTexB);
    markDirty();
}

void Compositor::setCanvasSizeA(int width, int height) {
    m_canvasWidthA = width;
    m_canvasHeightA = height;
    markDirty();
}

void Compositor::setCanvasSizeB(int width, int height) {
    m_canvasWidthB = width;
    m_canvasHeightB = height;
    markDirty();
}

// ── Overlays ─────────────────────────────────────────────────────────────────

void Compositor::setOverlaysA(const QList<OverlayItem> &overlays) {
    m_overlaysA = overlays;
    m_overlayPixCache.clear();
    m_overlayScaledCache.clear();
    markDirty();
}

void Compositor::setOverlaysB(const QList<OverlayItem> &overlays) {
    m_overlaysB = overlays;
    m_overlayPixCache.clear();
    m_overlayScaledCache.clear();
    markDirty();
}

// ── Node chain compositing ────────────────────────────────────────────────────

void Compositor::clearChainTextures(std::vector<GLuint> &texList) {
    for (GLuint t : texList)
        if (t) { glDeleteTextures(1, &t); m_texSizes.remove(t); }
    texList.clear();
}

void Compositor::primeChainSources(std::vector<NodeChainSource> &chain,
                                     std::vector<GLuint> &texList) {
    texList.resize(chain.size(), 0);
    for (size_t i = 0; i < chain.size(); ++i) {
        auto *src = chain[i].source.get();
        if (!src) continue;
        if (!src->isReady()) src->nextFrame(); // prime first frame if possible
        if (src->isReady())
            setupTextureGL(texList[i], src->frameSize(), src->hasAlpha());
        if (src->isReady())
            uploadSourceFrameGL(texList[i], src);
    }
}

void Compositor::setNodeChainA(std::vector<NodeChainSource> chain) {
    if (isValid()) {
        makeCurrent();
        clearChainTextures(m_chainTexA);
        m_chainA = std::move(chain);
        primeChainSources(m_chainA, m_chainTexA);
        doneCurrent();
        markDirty();
    } else {
        m_chainA = std::move(chain);
    }
}

void Compositor::setNodeChainB(std::vector<NodeChainSource> chain) {
    if (isValid()) {
        makeCurrent();
        clearChainTextures(m_chainTexB);
        m_chainB = std::move(chain);
        primeChainSources(m_chainB, m_chainTexB);
        doneCurrent();
        markDirty();
    } else {
        m_chainB = std::move(chain);
    }
}

void Compositor::reorderChain(bool deckA, const std::vector<int> &perm) {
    auto &chain = deckA ? m_chainA : m_chainB;
    auto &texList = deckA ? m_chainTexA : m_chainTexB;
    if (perm.size() != chain.size()) return;
    std::vector<NodeChainSource> newChain(chain.size());
    std::vector<GLuint> newTex(texList.size(), 0);
    for (size_t i = 0; i < perm.size(); ++i) {
        const int from = perm[i];
        if (from < 0 || from >= (int)chain.size()) return;
        newChain[i] = std::move(chain[from]);
        if (from < (int)texList.size()) newTex[i] = texList[from];
    }
    chain = std::move(newChain);
    texList = std::move(newTex);
    markDirty();
}

void Compositor::setChainVisibility(bool deckA, int chainIndex, bool visible) {
    auto &chain = deckA ? m_chainA : m_chainB;
    if (chainIndex < 0 || chainIndex >= (int)chain.size()) return;
    chain[chainIndex].visible = visible;
    markDirty();
}

void Compositor::setChainPlacement(bool deckA, int chainIndex,
                                    float cropX, float cropY, float cropW, float cropH,
                                    bool flipH, bool flipV,
                                    float baseX, float baseY, float baseW, float baseH,
                                    bool visible) {
    auto &chain = deckA ? m_chainA : m_chainB;
    if (chainIndex < 0 || chainIndex >= (int)chain.size()) return;
    auto &e = chain[chainIndex];
    e.cropX = cropX; e.cropY = cropY; e.cropW = cropW; e.cropH = cropH;
    e.flipH = flipH; e.flipV = flipV;
    e.baseX = baseX; e.baseY = baseY; e.baseW = baseW; e.baseH = baseH;
    e.visible = visible;
    markDirty();
}

void Compositor::drawChainSources(std::vector<NodeChainSource> &chain,
                                    std::vector<GLuint> &texList, float alpha,
                                    int canvasW, int canvasH) {
    if (alpha <= 0.f) return;

    // Use per-entry canvas size if available, otherwise fall back to the deck's canvas.
    if (!chain.empty() && chain[0].canvasWidth > 0 && chain[0].canvasHeight > 0) {
        canvasW = chain[0].canvasWidth;
        canvasH = chain[0].canvasHeight;
    }

    // Compute canvas bounds relative to the render target — same logic as drawDeck.
    const int rw = renderW(), rh = renderH();
    QRectF canvasBounds(0, 0, rw, rh);
    if (canvasW > 0 && canvasH > 0) {
        float canvasAR = (float)canvasW / canvasH;
        float windowAR = rh > 0 ? (float)rw / rh : canvasAR;
        float cW, cH;
        if (canvasAR > windowAR) {
            cW = rw;
            cH = cW / canvasAR;
        } else {
            cH = rh;
            cW = cH * canvasAR;
        }
        canvasBounds = QRectF((rw - cW) / 2.f, (rh - cH) / 2.f, cW, cH);
    }

    for (size_t i = 0; i < chain.size() && i < texList.size(); ++i) {
        if (!chain[i].visible) continue;
        auto *src = chain[i].source.get();
        if (!src || !src->isReady()) continue;
        GLuint tex = src->glTexture() ? src->glTexture() : texList[i];
        if (!tex) continue;
        const float cx = chain[i].cropX, cy = chain[i].cropY;
        const float cw = chain[i].cropW, ch = chain[i].cropH;
        QRectF placement(canvasBounds.left() + chain[i].baseX * canvasBounds.width(),
                         canvasBounds.top()  + chain[i].baseY * canvasBounds.height(),
                         chain[i].baseW * canvasBounds.width(),
                         chain[i].baseH * canvasBounds.height());
        if (canvasW <= 0 || canvasH <= 0)
            placement = fitSourceRect(placement, src, cw, ch);
        glColor4f(1.f, 1.f, 1.f, alpha);
        renderTexture(tex, cx, cy, cw, ch,
                      (float)placement.x(), (float)placement.y(), (float)placement.width(), (float)placement.height(),
                      chain[i].flipH, chain[i].flipV);
    }
}

void Compositor::advanceChainSources(std::vector<NodeChainSource> &chain,
                                       std::vector<GLuint> &texList, bool &anyDecoded) {
    for (size_t i = 0; i < chain.size() && i < texList.size(); ++i) {
        auto *src = chain[i].source.get();
        if (!src) continue;
        // Live/async sources that aren't yet ready: try to prime them
        if (!src->isReady()) {
            src->nextFrame();
            if (src->isReady() && makeCurrent()) {
                setupTextureGL(texList[i], src->frameSize(), src->hasAlpha());
                uploadSourceFrameGL(texList[i], src);
                doneCurrent();
                anyDecoded = true;
            }
            continue;
        }
        if (!chain[i].playing) {
            chain[i].clock.invalidate();
            continue;
        }
        bool advanced = false;
        if (src->duration() > 0.0) {
            // Timeline sources play at their native rate, not one frame per tick.
            if (!chain[i].clock.isValid()) {
                chain[i].anchor = src->currentTime();
                chain[i].clock.start();
            }
            advanced = src->presentAt(chain[i].anchor + chain[i].clock.elapsed() / 1000.0);
        } else {
            advanced = src->nextFrame();
        }
        if (advanced && makeCurrent()) {
            uploadSourceFrameGL(texList[i], src);
            doneCurrent();
            anyDecoded = true;
        }
    }
}

// ── Query ─────────────────────────────────────────────────────────────────────

double Compositor::getCurrentTimeA() const {
    return m_sourceA ? m_sourceA->currentTime() : 0.0;
}
double Compositor::getDurationA() const {
    return m_sourceA ? m_sourceA->duration() : 0.0;
}
double Compositor::getCurrentTimeB() const {
    return m_sourceB ? m_sourceB->currentTime() : 0.0;
}
double Compositor::getDurationB() const {
    return m_sourceB ? m_sourceB->duration() : 0.0;
}

void Compositor::setCrossfade(float mixB) {
    mixB = std::clamp(mixB, 0.f, 1.f);
    // Commit the transition direction only when leaving an end stop: moving off
    // full-A makes B the incoming deck, moving off full-B makes A incoming.
    // In between we keep the committed direction, so reversing the fader mid-way
    // rewinds the current transition rather than swapping which deck animates.
    if (mixB > m_crossfadeB && m_crossfadeB <= 0.001f)
        m_transitionTowardB = true;
    else if (mixB < m_crossfadeB && m_crossfadeB >= 0.999f)
        m_transitionTowardB = false;
    m_crossfadeB = mixB;
    markDirty();
}

void Compositor::setPanicOverlay(PanicOverlay overlay) {
    if (m_panicOverlay == overlay) return;
    m_panicOverlay = overlay;
    markDirty();
}

void Compositor::setOutputFrozen(bool frozen) {
    if (m_outputFrozen == frozen) return;
    m_outputFrozen = frozen;
    markDirty();
}

QImage Compositor::captureProgramFrame() {
    if (makeCurrent()) {
        composeProgramFrame();
        glFlush();
        doneCurrent();
    }
    return m_programFrameCache.copy();
}

void Compositor::captureOutputFrameNow() {
    if (m_programFrameConsumers <= 0 && m_deckFrameConsumers <= 0)
        return;
    if (!makeCurrent())
        return;
    composeProgramFrame();
    glFlush();
    doneCurrent();
    emit programFrameReady();
}

void Compositor::holdLayerAsStill(bool deckA, int chainIndex, const QImage &frame) {
    if (frame.isNull())
        return;

    auto still = std::make_unique<ImageSource>();
    if (!still->setImage(frame, QStringLiteral("Frozen Frame")))
        return;

    const bool glOk = makeCurrent();
    if (chainIndex < 0) {
        if (deckA) {
            pauseA();
            setSourceA(std::move(still));
        } else {
            pauseB();
            setSourceB(std::move(still));
        }
    } else {
        auto &chain   = deckA ? m_chainA : m_chainB;
        auto &texList = deckA ? m_chainTexA : m_chainTexB;
        if (chainIndex >= static_cast<int>(chain.size())) {
            doneCurrent();
            return;
        }
        chain[static_cast<size_t>(chainIndex)].playing = false;
        chain[static_cast<size_t>(chainIndex)].source    = std::move(still);
        if (glOk && chainIndex < static_cast<int>(texList.size()))
            uploadSourceFrameGL(texList[static_cast<size_t>(chainIndex)],
                                chain[static_cast<size_t>(chainIndex)].source.get());
    }
    doneCurrent();
    markDirty();
}

QImage Compositor::deckPreviewWithOverlays(bool deckA) const {
    const QImage &base = deckA ? m_deckPreviewA : m_deckPreviewB;
    if (base.isNull()) return {};

    const QList<OverlayItem> &overlays = deckA ? m_overlaysA : m_overlaysB;
    if (overlays.isEmpty()) return base;

    QImage frame = base.copy();
    QPainter p(&frame);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF &programRect = deckA ? m_videoRectProgramA : m_videoRectProgramB;
    const double sx = (double)s_deckPreviewWidth  / s_programWidth;
    const double sy = (double)s_deckPreviewHeight / s_programHeight;
    const QRectF vr = programRect.isEmpty()
        ? QRectF(0, 0, s_deckPreviewWidth, s_deckPreviewHeight)
        : QRectF(programRect.x() * sx, programRect.y() * sy,
                 programRect.width() * sx, programRect.height() * sy);
    const_cast<Compositor *>(this)->renderOverlays(p, overlays, vr, 1.f);
    return frame;
}

QImage Compositor::deckProgramFrameWithOverlays(bool deckA) const {
    const QImage &base = deckA ? m_deckFrameCacheA : m_deckFrameCacheB;
    if (base.isNull()) return {};

    const QList<OverlayItem> &overlays = deckA ? m_overlaysA : m_overlaysB;
    if (overlays.isEmpty()) return base;

    QImage frame = base.copy();
    QPainter p(&frame);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF &programRect = deckA ? m_videoRectProgramA : m_videoRectProgramB;
    const QRectF vr = programRect.isEmpty()
        ? QRectF(0, 0, s_programWidth, s_programHeight)
        : programRect;
    const_cast<Compositor *>(this)->renderOverlays(p, overlays, vr, 1.f);
    return frame;
}

QImage Compositor::deckProgramFrame(bool deckA) const {
    return deckProgramFrameWithOverlays(deckA);
}

QImage Compositor::getFrameA() const {
    return deckPreviewWithOverlays(true);
}

QImage Compositor::getFrameB() const {
    return deckPreviewWithOverlays(false);
}

// ── GL helpers ────────────────────────────────────────────────────────────────

void Compositor::setupTextureGL(GLuint &tex, QSize sz, bool alpha) {
    if (tex) { glDeleteTextures(1, &tex); m_texSizes.remove(tex); tex = 0; }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const GLenum fmt = alpha ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, sz.width(), sz.height(),
                 0, fmt, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_texSizes.insert(tex, sz);
}

void Compositor::uploadSourceFrameGL(GLuint &tex, MediaSource *source) {
    if (!source || !source->isReady()) return;
    // Source already has its frame in a shared GL texture — nothing to upload.
    if (source->glTexture()) return;
    QSize sz = source->frameSize();
    if (sz.isEmpty()) return;

    const bool alpha = source->hasAlpha();
    const GLenum fmt = alpha ? GL_RGBA : GL_RGB;

    // Recreate the texture only when the frame size changes. The size is tracked
    // in m_texSizes to avoid a per-frame glGetTexLevelParameteriv, which forces a
    // GPU→CPU sync stall. Handles async sources (Camera, Screen) whose first
    // frame size may differ from the (0,0) placeholder at construction time.
    if (!tex || m_texSizes.value(tex) != sz)
        setupTextureGL(tex, sz, alpha);

    glBindTexture(GL_TEXTURE_2D, tex);
    const int rowStride = source->frameBytesPerLine();
    const int bpp       = alpha ? 4 : 3;
    if (rowStride > 0 && rowStride != sz.width() * bpp) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowStride / bpp);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sz.width(), sz.height(),
                        fmt, GL_UNSIGNED_BYTE, source->frameData());
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sz.width(), sz.height(),
                        fmt, GL_UNSIGNED_BYTE, source->frameData());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ── Frame loop ────────────────────────────────────────────────────────────────

// Advances a source by one frame, respecting trim and repeat.
// Returns true if a new frame was decoded and should be uploaded.
bool Compositor::advanceSource(MediaSource *source, bool &playing, bool repeat,
                                double trimStart, double trimEnd) {
    if (!source) return false;
    // Allow lazy-init sources (e.g. DynamicInterfaceSource) to initialize on
    // the first timer tick where no VideoWidget GL context is current.
    if (!source->isReady()) {
        source->nextFrame();
        return false;
    }

    double endLimit = (trimEnd > 0) ? trimEnd : source->duration();
    if (endLimit > 0 && source->currentTime() >= endLimit) {
        if (repeat)
            source->seek(trimStart);
        else {
            playing = false;
            return false;
        }
    }

    return source->nextFrame();
}

bool Compositor::advanceDeckPaced(bool deckA) {
    MediaSource *source = deckA ? m_sourceA.get() : m_sourceB.get();
    bool &playing       = deckA ? m_playingA : m_playingB;
    if (!source) return false;

    if (!source->isReady()) {
        source->nextFrame();   // let lazy/live sources initialize
        return false;
    }

    const double dur = source->duration();
    // Live/static sources (camera, screen, image, …) have no real timeline:
    // keep the simple one-frame-per-tick behaviour.
    if (dur <= 0.0)
        return advanceSource(source, playing,
                             deckA ? m_repeatA : m_repeatB,
                             deckA ? m_trimStartA : m_trimStartB,
                             deckA ? m_trimEndA   : m_trimEndB);

    QElapsedTimer &clock = deckA ? m_playClockA   : m_playClockB;
    double &anchor       = deckA ? m_clockAnchorA : m_clockAnchorB;
    bool   &dirty        = deckA ? m_clockDirtyA  : m_clockDirtyB;
    const bool   repeat    = deckA ? m_repeatA    : m_repeatB;
    const double trimStart = deckA ? m_trimStartA : m_trimStartB;
    const double trimEnd   = deckA ? m_trimEndA   : m_trimEndB;

    if (dirty || !clock.isValid()) {
        anchor = source->currentTime();
        clock.start();
        dirty = false;
    }

    const double speed = deckA ? m_speedA : m_speedB;
    double desired = anchor + clock.elapsed() / 1000.0 * speed;
    const double endLimit = (trimEnd > 0) ? trimEnd : dur;
    bool reachedEnd = false;

    if (endLimit > 0 && desired >= endLimit) {
        if (repeat) {
            source->seek(trimStart);
            anchor  = trimStart;
            clock.restart();
            desired = trimStart;
        } else {
            // Play out to the end, then stop once we've actually reached it.
            desired = endLimit;
            reachedEnd = true;
        }
    }

    // Show whichever frame is due at the wall-clock target. Video files decode
    // ahead on their own thread, so a slow decoder drops frames here rather than
    // falling behind the clock (and the audio).
    const bool decoded = source->presentAt(desired);

    // Frame timestamps stop short of a trim point (the last frame shown starts before it), so
    // the clock reaching the end is what stops the deck.
    if (!repeat && endLimit > 0 && (reachedEnd || source->currentTime() >= endLimit - 1e-3))
        playing = false;

    return decoded;
}

void Compositor::syncOnAir() {
    const float t = std::clamp(m_crossfadeB, 0.f, 1.f);
    const bool cut = m_transitionMode == TransitionMode::Cut;
    const bool onAirA = m_singleStream || (cut ? t < 0.5f : t < 0.999f);
    const bool onAirB = !m_singleStream && (cut ? t >= 0.5f : t > 0.001f);
    if (m_sourceA) m_sourceA->setOnAir(onAirA);
    if (m_sourceB) m_sourceB->setOnAir(onAirB);
    for (const NodeChainSource &c : m_chainA)
        if (c.source) c.source->setOnAir(onAirA);
    for (const NodeChainSource &c : m_chainB)
        if (c.source) c.source->setOnAir(onAirB);
}

double Compositor::requestDeckOut(bool deckA) {
    MediaSource *primary = deckA ? m_sourceA.get() : m_sourceB.get();
    double longest = primary ? primary->requestOut() : 0.0;
    for (const NodeChainSource &c : deckA ? m_chainA : m_chainB)
        if (c.source) longest = std::max(longest, c.source->requestOut());
    return longest;
}

void Compositor::updateFrame() {
    syncOnAir();
    const bool externalCapture = m_programFrameConsumers > 0 || m_deckFrameConsumers > 0;
    const bool needsCapture = externalCapture || wantsProgramFrame() || wantsDeckFrame();

    bool decodedA = false, decodedB = false;
    bool chainADecoded = false, chainBDecoded = false;
    bool waitingForLiveSource = false;

    if (!(m_outputFrozen && !needsCapture)) {
        if (m_playingA)
            decodedA = advanceDeckPaced(true);
        if (m_playingB)
            decodedB = advanceDeckPaced(false);

        advanceChainSources(m_chainA, m_chainTexA, chainADecoded);
        advanceChainSources(m_chainB, m_chainTexB, chainBDecoded);

        if ((decodedA || decodedB) && makeCurrent()) {
            if (decodedA)       uploadSourceFrameGL(m_textureA,       m_sourceA.get());
            if (decodedB)       uploadSourceFrameGL(m_textureB,       m_sourceB.get());
            doneCurrent();
        }

        waitingForLiveSource =
            (m_playingA       && m_sourceA    && !m_sourceA->isReady())    ||
            (m_playingB       && m_sourceB    && !m_sourceB->isReady());
    }

    if (!(decodedA || decodedB || chainADecoded || chainBDecoded
          || needsCapture || waitingForLiveSource || m_dirty))
        return;

    // Composing from the timer keeps capture working when no display surface is exposed.
    if (renderNow() && externalCapture)
        emit programFrameReady();
}
