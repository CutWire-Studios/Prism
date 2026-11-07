#include "core/render/Compositor.h"
#include "core/media/GpuDevice.h"
#include "core/media/HwAccel.h"
#include "core/sources/ImageSource.h"
#include "core/sources/VideoFileSource.h"
#include "app/transitions/Transition.h"
#include <QCoreApplication>
#include <QEvent>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOffscreenSurface>
#include <QPainter>
#include <QPaintDevice>
#include <QFont>
#include <QSurfaceFormat>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstring>

int Compositor::s_programWidth  = 1280;
Compositor::OutputScaleMode Compositor::s_outputScaleMode = Compositor::OutputScaleMode::Fit;
int Compositor::s_programHeight = 720;

Compositor::Compositor(QObject *parent)
    : QObject(parent) {
    m_frameTimer = new QTimer(this);
    m_frameTimer->setTimerType(Qt::PreciseTimer);
    connect(m_frameTimer, &QTimer::timeout, this, &Compositor::updateFrame);
    m_frameTimer->start(frameIntervalMs(m_programFps));
    // Create the context here, on the GUI thread: PreviewItem queries it from the scene-graph
    // render thread, and a context created there can never be made current on this one.
    isValid();
}

int Compositor::frameIntervalMs(int fps) {
    return std::max(1, qRound(1000.0 / std::clamp(fps, 1, 240)));
}

void Compositor::setProgramFps(int fps) {
    fps = std::clamp(fps, 1, 240);
    if (fps == m_programFps) return;
    m_programFps = fps;
    if (m_frameTimer && m_frameTimer->isActive())
        m_frameTimer->start(frameIntervalMs(fps));
}

Compositor::~Compositor() {
    blockSignals(true);
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
    releaseMediaSources();
    if (m_ctx && makeCurrent()) {
        destroyProgramFbo();
        if (m_textureLive)       glDeleteTextures(1, &m_textureLive);
        if (m_textureStaging)       glDeleteTextures(1, &m_textureStaging);
        clearChainTextures(m_chainTexLive);
        clearChainTextures(m_chainTexStaging);
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
    m_playingLive = false;
    m_playingStaging = false;
    m_sourceLive.reset();
    m_sourceStaging.reset();
    m_chainLive.clear();
    m_chainStaging.clear();
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
    if (m_ctxDepth > 0) {
        // Sources can switch contexts inside nextFrame() (e.g. offscreen QML); only re-assert then.
        if (QOpenGLContext::currentContext() != m_ctx.get() && !m_ctx->makeCurrent(m_surface.get()))
            return false;
        ++m_ctxDepth;
        return true;
    }
    {
        QOpenGLContext *prev = QOpenGLContext::currentContext();
        m_prevContext = prev != m_ctx.get() ? prev : nullptr;
        m_prevSurface = m_prevContext ? m_prevContext->surface() : nullptr;
    }
    if (!m_ctx->makeCurrent(m_surface.get()))
        return false;
    ++m_ctxDepth;
    if (m_holdContext && !m_contextHeld) {
        m_contextHeld = true;
        ++m_ctxDepth;
    }
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

GLuint Compositor::deckTexture(Deck deck) const {
    return isLive(deck) ? m_deckColorTexLive : m_deckColorTexStaging;
}

std::pair<float,float> Compositor::computeDeckAlphas() const {
    const float t = std::clamp(m_crossfade, 0.f, 1.f);
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

    for (const Deck deck : {Deck::Live, Deck::Staging}) {
        if (!deckNeedsRender(deck))
            continue;
        renderDeckToFbo(deck);
        if (m_deckFrameConsumers > 0 || (!m_sharesWithGui && m_viewers[isLive(deck) ? 1 : 2] > 0))
            cacheDeckFrameFromFbo(deck);
        else
            resetReadback(isLive(deck) ? m_readbackDeckLive : m_readbackDeckStaging);
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
    else
        resetReadback(m_readbackProgram);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_compW = 0;
    m_compH = 0;
}

std::pair<float,float> Compositor::audioGains(TransitionMode mode, float mix) {
    const float t = std::clamp(mix, 0.f, 1.f);
    constexpr float kPi = 3.14159265358979f;
    switch (mode) {
    case TransitionMode::Cut:
        return t < 0.5f ? std::make_pair(1.f, 0.f) : std::make_pair(0.f, 1.f);
    case TransitionMode::DipToBlack:
    case TransitionMode::DipToWhite:
        return {t < 0.5f ? std::max(0.f, std::cos(kPi * t)) : 0.f, t > 0.5f ? std::sin(kPi * (t - 0.5f)) : 0.f};
    default:
        return {std::max(0.f, std::cos(kPi * 0.5f * t)), std::sin(kPi * 0.5f * t)};
    }
}

bool Compositor::deckContributesToProgram(Deck deck) const {
    if (m_panicOverlay != PanicOverlay::None)
        return false;
    const float t = std::clamp(m_crossfade, 0.f, 1.f);
    if (m_transitionMode == TransitionMode::Cut)
        return isLive(deck) ? t < 0.5f : t >= 0.5f;
    return isLive(deck) ? t < 0.999f : t > 0.001f;
}

bool Compositor::deckNeedsRender(Deck deck) const {
    return deckContributesToProgram(deck) || m_deckFrameConsumers > 0 || m_viewers[isLive(deck) ? 1 : 2] > 0;
}

void Compositor::renderDeckToFbo(Deck deck) {
    const GLuint fbo = isLive(deck) ? m_deckFboLive : m_deckFboStaging;
    QRectF &outRect = isLive(deck) ? m_videoRectProgramLive : m_videoRectProgramStaging;

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

    const GLuint deckTex = isLive(deck)
        ? ((m_sourceLive && m_sourceLive->glTexture()) ? m_sourceLive->glTexture() : m_textureLive)
        : ((m_sourceStaging && m_sourceStaging->glTexture()) ? m_sourceStaging->glTexture() : m_textureStaging);
    MediaSource *src = isLive(deck) ? m_sourceLive.get() : m_sourceStaging.get();
    const float cx = isLive(deck) ? m_cropXLive : m_cropXStaging;
    const float cy = isLive(deck) ? m_cropYLive : m_cropYStaging;
    const float cw = isLive(deck) ? m_cropWLive : m_cropWStaging;
    const float ch = isLive(deck) ? m_cropHLive : m_cropHStaging;
    const float baseX = isLive(deck) ? m_baseXLive : m_baseXStaging;
    const float baseY = isLive(deck) ? m_baseYLive : m_baseYStaging;
    const float baseW = isLive(deck) ? m_baseWLive : m_baseWStaging;
    const float baseH = isLive(deck) ? m_baseHLive : m_baseHStaging;
    const int canvasW = isLive(deck) ? m_canvasWidthLive : m_canvasWidthStaging;
    const int canvasH = isLive(deck) ? m_canvasHeightLive : m_canvasHeightStaging;
    auto &chain = isLive(deck) ? m_chainLive : m_chainStaging;
    auto &chainTex = isLive(deck) ? m_chainTexLive : m_chainTexStaging;

    glEnable(GL_TEXTURE_2D);
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

        const bool flipH = isLive(deck) ? m_flipHLive : m_flipHStaging;
        const bool flipV = isLive(deck) ? m_flipVLive : m_flipVStaging;
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

    if (m_transitionMode == TransitionMode::DipToWhite) {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    const int rw = renderW(), rh = renderH();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float t = std::clamp(m_crossfade, 0.f, 1.f);

    auto drawDeckFbo = [&](GLuint deckTex, float alpha) {
        if (alpha <= 0.f || !deckTex) return;
        renderFboTexture(deckTex, 0.f, 0.f, (float)rw, (float)rh, alpha);
    };

    auto drawSideLive = [&](float alpha) { drawDeckFbo(m_deckColorTexLive, alpha); };
    auto drawSideStaging = [&](float alpha) { drawDeckFbo(m_deckColorTexStaging, alpha); };

    Transition::Context ctx;
    ctx.width   = rw;
    ctx.height  = rh;
    ctx.t       = t;
    ctx.p       = t;
    ctx.drawOut = drawSideLive;
    ctx.drawIn  = drawSideStaging;
    ctx.texLive    = m_deckColorTexLive;
    ctx.texStaging    = m_deckColorTexStaging;
    // Deck FBO upright sampling for 2D is handled in renderFboTexture(). 3D quads are
    // Y-up (+Y = top), so use source-style V coords (texFlipped=false) — not the 2D flip.
    ctx.texFlipped = false;
    ctx.readyLive  = m_deckColorTexLive && m_sourceLive && m_sourceLive->isReady();
    ctx.readyStaging  = m_deckColorTexStaging && m_sourceStaging && m_sourceStaging->isReady();
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
    if (m_deckFboLive) return;

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

    setupDeckFbo(m_deckFboLive, m_deckColorTexLive);
    setupDeckFbo(m_deckFboStaging, m_deckColorTexStaging);
}

void Compositor::destroyProgramFbo() {
    for (Readback *rb : {&m_readbackProgram, &m_readbackDeckLive, &m_readbackDeckStaging}) {
        if (rb->pbo[0]) m_ctx->extraFunctions()->glDeleteBuffers(2, rb->pbo);
        *rb = Readback();
    }
    if (m_deckColorTexLive) { glDeleteTextures(1, &m_deckColorTexLive); m_deckColorTexLive = 0; }
    if (m_deckFboLive)      { glDeleteFramebuffers(1, &m_deckFboLive); m_deckFboLive = 0; }
    if (m_deckColorTexStaging) { glDeleteTextures(1, &m_deckColorTexStaging); m_deckColorTexStaging = 0; }
    if (m_deckFboStaging)      { glDeleteFramebuffers(1, &m_deckFboStaging); m_deckFboStaging = 0; }
    if (m_programColorTex) { glDeleteTextures(1, &m_programColorTex); m_programColorTex = 0; }
    if (m_programFbo)      { glDeleteFramebuffers(1, &m_programFbo); m_programFbo = 0; }
}

void Compositor::setProgramResolution(int width, int height) {
    if (width == s_programWidth && height == s_programHeight)
        return;

    s_programWidth  = width;
    s_programHeight = height;

    // Program/deck FBOs are lazily (re)created at the new size on the next
    // composeProgramFrame() (see ensureProgramFbo/ensureDeckFbos).
    if (m_ctx && makeCurrent()) {
        destroyProgramFbo();
        doneCurrent();
    }
    m_programFrameCache = QImage();
    m_deckFrameCacheLive   = QImage();
    m_deckFrameCacheStaging   = QImage();
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

void Compositor::setDeckFrameConsumerCount(int count) {
    m_deckFrameConsumers = std::max(0, count);
}

void Compositor::resetReadback(Readback &rb) {
    rb.pending[0] = rb.pending[1] = false;
}

// Double-buffered: this frame's pixels land in one PBO while the previous frame's are copied out
// of the other, so the GPU never stalls the CPU. The first frame after a reset (and any
// captureProgramFrame) reads the PBO it just filled instead, so callers always get a frame.
void Compositor::readbackToCache(Readback &rb, QImage &cache, bool flipRows) {
    QOpenGLExtraFunctions *gl = m_ctx->extraFunctions();
    const QSize sz(s_programWidth, s_programHeight);
    const qsizetype stride = qsizetype(sz.width()) * 4;
    const qsizetype bytes = stride * sz.height();

    if (rb.size != sz || !rb.pbo[0]) {
        if (rb.pbo[0]) gl->glDeleteBuffers(2, rb.pbo);
        gl->glGenBuffers(2, rb.pbo);
        for (GLuint pbo : rb.pbo) {
            gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
            gl->glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
        }
        rb.size = sz;
        rb.next = 0;
        resetReadback(rb);
    }

    const int write = rb.next;
    const int prev = write ^ 1;
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo[write]);
    gl->glReadPixels(0, 0, sz.width(), sz.height(), GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    rb.pending[write] = true;
    rb.next = prev;

    const bool useWrite = m_syncReadback || !rb.pending[prev];
    const int slot = useWrite ? write : prev;
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo[slot]);
    const auto *src = static_cast<const uchar *>(
        gl->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT));
    if (src) {
        if (cache.size() != sz || cache.format() != QImage::Format_RGBA8888)
            cache = QImage(sz, QImage::Format_RGBA8888);
        uchar *dst = cache.bits();
        if (flipRows) {
            for (int y = 0; y < sz.height(); ++y)
                memcpy(dst + y * stride, src + (sz.height() - 1 - y) * stride, size_t(stride));
        } else {
            memcpy(dst, src, size_t(bytes));
        }
        gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (!useWrite)
        rb.pending[prev] = false;
}

void Compositor::cacheDeckFrameFromFbo(Deck deck) {
    const GLuint fbo = isLive(deck) ? m_deckFboLive : m_deckFboStaging;
    if (!fbo) return;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    readbackToCache(isLive(deck) ? m_readbackDeckLive : m_readbackDeckStaging,
                    isLive(deck) ? m_deckFrameCacheLive : m_deckFrameCacheStaging, true);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Compositor::cacheProgramFrameFromFbo() {
    if (!m_programFbo) return;

    readbackToCache(m_readbackProgram, m_programFrameCache, false);
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

// ── Load ─────────────────────────────────────────────────────────────────────

// Private helper: detect type, construct the right MediaSource, upload first frame.
void Compositor::loadSourceInternal(const QString &filePath,
                                     std::unique_ptr<MediaSource> &target,
                                     GLuint &tex, bool &playing, bool &pending) {
    playing = false;
    pending = false;

    std::unique_ptr<MediaSource> source;

    if (ImageSource::isStaticImageFile(filePath)) {
        auto img = std::make_unique<ImageSource>();
        if (!img->load(filePath)) return;
        source = std::move(img);
    } else {
        auto vid = std::make_unique<VideoFileSource>();
        vid->setAsync(true);
        if (!vid->open(filePath)) return;
        source = std::move(vid);
    }

    target = std::move(source);
    pending = target->type() == MediaSource::Type::VideoFile;

    if (!isValid()) {
        markDirty();
        return;
    }
    makeCurrent();
    setupTextureGL(tex, target->frameSize());
    target->nextFrame();                     // prime first frame (no-op for images)
    uploadSourceFrameGL(tex, target.get());
    pending = pending && !target->isReady();
    doneCurrent();
    markDirty();
}

void Compositor::loadVideoLive(const QString &filePath) {
    m_readyLive = false;
    loadSourceInternal(filePath, m_sourceLive, m_textureLive, m_playingLive, m_pendingFrameLive);
}

void Compositor::loadVideoStaging(const QString &filePath) {
    m_readyStaging = false;
    loadSourceInternal(filePath, m_sourceStaging, m_textureStaging, m_playingStaging, m_pendingFrameStaging);
}

void Compositor::setSourceLive(std::unique_ptr<MediaSource> source) {
    m_playingLive = false;
    m_clockDirtyLive = true;
    m_pendingFrameLive = false;
    m_readyLive = false;
    m_sourceLive  = std::move(source);
    if (!m_sourceLive) return;
    if (isValid()) {
        makeCurrent();
        // frameSize() may be (0,0) for async sources like Camera; uploadSourceFrameGL
        // handles resizing the texture when the first real frame arrives.
        setupTextureGL(m_textureLive, m_sourceLive->frameSize());
        const bool primed = m_sourceLive->nextFrame();
        m_pendingFrameLive = !primed && m_sourceLive->type() == MediaSource::Type::VideoFile;
        if (primed) {
            // nextFrame() may switch GL contexts internally (e.g. DynamicInterfaceSource
            // creates its own context for offscreen QML rendering). Re-assert ours.
            m_ctx->makeCurrent(m_surface.get());
            uploadSourceFrameGL(m_textureLive, m_sourceLive.get());
        }
        doneCurrent();
    }
    // Live sources deliver frames continuously via the frame timer.
    const auto t = m_sourceLive->type();
    m_playingLive = (t == MediaSource::Type::Camera    ||
                  t == MediaSource::Type::Screen     ||
                  t == MediaSource::Type::Window     ||
                  t == MediaSource::Type::Slideshow  ||
                  t == MediaSource::Type::Canvas     ||
                  t == MediaSource::Type::Ndi);
    markDirty();
}

void Compositor::setSourceStaging(std::unique_ptr<MediaSource> source) {
    m_playingStaging = false;
    m_clockDirtyStaging = true;
    m_pendingFrameStaging = false;
    m_readyStaging = false;
    m_sourceStaging  = std::move(source);
    if (!m_sourceStaging) return;
    if (isValid()) {
        makeCurrent();
        setupTextureGL(m_textureStaging, m_sourceStaging->frameSize());
        const bool primed = m_sourceStaging->nextFrame();
        m_pendingFrameStaging = !primed && m_sourceStaging->type() == MediaSource::Type::VideoFile;
        if (primed) {
            m_ctx->makeCurrent(m_surface.get());
            uploadSourceFrameGL(m_textureStaging, m_sourceStaging.get());
        }
        doneCurrent();
    }
    const auto t = m_sourceStaging->type();
    m_playingStaging = (t == MediaSource::Type::Camera    ||
                  t == MediaSource::Type::Screen     ||
                  t == MediaSource::Type::Window     ||
                  t == MediaSource::Type::Slideshow  ||
                  t == MediaSource::Type::Canvas     ||
                  t == MediaSource::Type::Ndi);
    markDirty();
}

void Compositor::clearDeckLive() {
    setSourceLive(nullptr);
    setNodeChainLive({});
}

void Compositor::clearDeckStaging() {
    setSourceStaging(nullptr);
    setNodeChainStaging({});
}

void Compositor::adoptSourceLive(std::unique_ptr<MediaSource> source) {
    m_playingLive = false;
    m_pendingFrameLive = false;
    m_readyLive = false;
    m_sourceLive  = std::move(source);
}

void Compositor::adoptSourceStaging(std::unique_ptr<MediaSource> source) {
    m_playingStaging = false;
    m_pendingFrameStaging = false;
    m_readyStaging = false;
    m_sourceStaging  = std::move(source);
}

// ── Playback control ──────────────────────────────────────────────────────────

void Compositor::playLive()  { if (m_sourceLive) { m_playingLive = true; m_clockDirtyLive = true; } }
void Compositor::pauseLive() { m_playingLive = false; }
void Compositor::playStaging()  { if (m_sourceStaging) { m_playingStaging = true; m_clockDirtyStaging = true; } }
void Compositor::pauseStaging() { m_playingStaging = false; }

void Compositor::setDeckSpeed(Deck deck, double speed) {
    if (speed <= 0.0) speed = 1.0;
    // Re-anchor the play clock so already-elapsed time isn't re-scaled by the
    // new rate (which would jump the position).
    if (isLive(deck)) { m_speedLive = speed; m_clockDirtyLive = true; }
    else       { m_speedStaging = speed; m_clockDirtyStaging = true; }
}

void Compositor::seekLive(double seconds) {
    if (!m_sourceLive) return;
    // Video sources open asynchronously and take the seek before their first frame arrives.
    const bool video = m_sourceLive->type() == MediaSource::Type::VideoFile;
    if (!m_sourceLive->isReady() && !video) return;
    m_clockDirtyLive = true;
    m_sourceLive->seek(seconds);
    if (m_sourceLive->nextFrame()) {
        m_pendingFrameLive = false;
        if (makeCurrent()) {
            uploadSourceFrameGL(m_textureLive, m_sourceLive.get());
            doneCurrent();
        }
        markDirty();
    } else {
        m_pendingFrameLive = video;
    }
}

void Compositor::seekStaging(double seconds) {
    if (!m_sourceStaging) return;
    // Video sources open asynchronously and take the seek before their first frame arrives.
    const bool video = m_sourceStaging->type() == MediaSource::Type::VideoFile;
    if (!m_sourceStaging->isReady() && !video) return;
    m_clockDirtyStaging = true;
    m_sourceStaging->seek(seconds);
    if (m_sourceStaging->nextFrame()) {
        m_pendingFrameStaging = false;
        if (makeCurrent()) {
            uploadSourceFrameGL(m_textureStaging, m_sourceStaging.get());
            doneCurrent();
        }
        markDirty();
    } else {
        m_pendingFrameStaging = video;
    }
}

// ── Trim ─────────────────────────────────────────────────────────────────────

void Compositor::setTrimPointsLive(double s, double e) { m_trimStartLive = s; m_trimEndLive = e; }
void Compositor::setTrimPointsStaging(double s, double e) { m_trimStartStaging = s; m_trimEndStaging = e; }

// ── Crop ─────────────────────────────────────────────────────────────────────

void Compositor::setCropLive(float x, float y, float w, float h) {
    m_cropXLive = x; m_cropYLive = y; m_cropWLive = w; m_cropHLive = h;
    markDirty();
}

void Compositor::setCropStaging(float x, float y, float w, float h) {
    m_cropXStaging = x; m_cropYStaging = y; m_cropWStaging = w; m_cropHStaging = h;
    markDirty();
}

void Compositor::setBaseLive(float x, float y, float w, float h) {
    m_baseXLive = x; m_baseYLive = y; m_baseWLive = w; m_baseHLive = h;
    markDirty();
}

void Compositor::setBaseStaging(float x, float y, float w, float h) {
    m_baseXStaging = x; m_baseYStaging = y; m_baseWStaging = w; m_baseHStaging = h;
    markDirty();
}

void Compositor::setFlipLive(bool flipH, bool flipV) {
    m_flipHLive = flipH; m_flipVLive = flipV;
    markDirty();
}

void Compositor::setFlipStaging(bool flipH, bool flipV) {
    m_flipHStaging = flipH; m_flipVStaging = flipV;
    markDirty();
}

void Compositor::swapDeckContents() {
    using std::swap;
    swap(m_sourceLive, m_sourceStaging);
    swap(m_textureLive, m_textureStaging);
    swap(m_playingLive, m_playingStaging);
    swap(m_pendingFrameLive, m_pendingFrameStaging);
    swap(m_readyLive, m_readyStaging);
    swap(m_repeatLive, m_repeatStaging);
    swap(m_trimStartLive, m_trimStartStaging);
    swap(m_trimEndLive, m_trimEndStaging);
    swap(m_playClockLive, m_playClockStaging);
    swap(m_clockAnchorLive, m_clockAnchorStaging);
    swap(m_speedLive, m_speedStaging);
    // Re-anchor both clocks so elapsed time is measured against the new owner.
    m_clockDirtyLive = true;
    m_clockDirtyStaging = true;
    swap(m_cropXLive, m_cropXStaging); swap(m_cropYLive, m_cropYStaging);
    swap(m_cropWLive, m_cropWStaging); swap(m_cropHLive, m_cropHStaging);
    swap(m_flipHLive, m_flipHStaging); swap(m_flipVLive, m_flipVStaging);
    swap(m_baseXLive, m_baseXStaging); swap(m_baseYLive, m_baseYStaging);
    swap(m_baseWLive, m_baseWStaging); swap(m_baseHLive, m_baseHStaging);
    swap(m_canvasWidthLive, m_canvasWidthStaging);
    swap(m_canvasHeightLive, m_canvasHeightStaging);
    swap(m_chainLive, m_chainStaging);
    swap(m_chainTexLive, m_chainTexStaging);
    markDirty();
}

void Compositor::setCanvasSizeLive(int width, int height) {
    m_canvasWidthLive = width;
    m_canvasHeightLive = height;
    markDirty();
}

void Compositor::setCanvasSizeStaging(int width, int height) {
    m_canvasWidthStaging = width;
    m_canvasHeightStaging = height;
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

void Compositor::setNodeChainLive(std::vector<NodeChainSource> chain) {
    if (isValid()) {
        makeCurrent();
        clearChainTextures(m_chainTexLive);
        m_chainLive = std::move(chain);
        primeChainSources(m_chainLive, m_chainTexLive);
        doneCurrent();
        markDirty();
    } else {
        m_chainLive = std::move(chain);
    }
}

void Compositor::setNodeChainStaging(std::vector<NodeChainSource> chain) {
    if (isValid()) {
        makeCurrent();
        clearChainTextures(m_chainTexStaging);
        m_chainStaging = std::move(chain);
        primeChainSources(m_chainStaging, m_chainTexStaging);
        doneCurrent();
        markDirty();
    } else {
        m_chainStaging = std::move(chain);
    }
}

void Compositor::reorderChain(Deck deck, const std::vector<int> &perm) {
    auto &chain = isLive(deck) ? m_chainLive : m_chainStaging;
    auto &texList = isLive(deck) ? m_chainTexLive : m_chainTexStaging;
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

void Compositor::setChainVisibility(Deck deck, int chainIndex, bool visible) {
    auto &chain = isLive(deck) ? m_chainLive : m_chainStaging;
    if (chainIndex < 0 || chainIndex >= (int)chain.size()) return;
    chain[chainIndex].visible = visible;
    markDirty();
}

void Compositor::setChainPlacement(Deck deck, int chainIndex,
                                    float cropX, float cropY, float cropW, float cropH,
                                    bool flipH, bool flipV,
                                    float baseX, float baseY, float baseW, float baseH,
                                    bool visible) {
    auto &chain = isLive(deck) ? m_chainLive : m_chainStaging;
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

double Compositor::getCurrentTimeLive() const {
    return m_sourceLive ? m_sourceLive->currentTime() : 0.0;
}
double Compositor::getDurationLive() const {
    return m_sourceLive ? m_sourceLive->duration() : 0.0;
}
double Compositor::getCurrentTimeStaging() const {
    return m_sourceStaging ? m_sourceStaging->currentTime() : 0.0;
}
double Compositor::getDurationStaging() const {
    return m_sourceStaging ? m_sourceStaging->duration() : 0.0;
}

void Compositor::setCrossfade(float mix) {
    mix = std::clamp(mix, 0.f, 1.f);
    m_crossfade = mix;
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
        m_syncReadback = true;
        composeProgramFrame();
        m_syncReadback = false;
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

void Compositor::holdLayerAsStill(Deck deck, int chainIndex, const QImage &frame) {
    if (frame.isNull())
        return;

    auto still = std::make_unique<ImageSource>();
    if (!still->setImage(frame, QStringLiteral("Frozen Frame")))
        return;

    const bool glOk = makeCurrent();
    if (chainIndex < 0) {
        if (isLive(deck)) {
            pauseLive();
            setSourceLive(std::move(still));
        } else {
            pauseStaging();
            setSourceStaging(std::move(still));
        }
    } else {
        auto &chain   = isLive(deck) ? m_chainLive : m_chainStaging;
        auto &texList = isLive(deck) ? m_chainTexLive : m_chainTexStaging;
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

QImage Compositor::deckProgramFrame(Deck deck) const {
    return isLive(deck) ? m_deckFrameCacheLive : m_deckFrameCacheStaging;
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

bool Compositor::advanceDeckPaced(Deck deck) {
    MediaSource *source = isLive(deck) ? m_sourceLive.get() : m_sourceStaging.get();
    bool &playing       = isLive(deck) ? m_playingLive : m_playingStaging;
    if (!source) return false;

    if (!source->isReady()) {
        return source->nextFrame();   // lets lazy/live sources initialize and async ones deliver their first frame
    }

    const double dur = source->duration();
    // Live/static sources (camera, screen, image, …) have no real timeline:
    // keep the simple one-frame-per-tick behaviour.
    if (dur <= 0.0)
        return advanceSource(source, playing,
                             isLive(deck) ? m_repeatLive : m_repeatStaging,
                             isLive(deck) ? m_trimStartLive : m_trimStartStaging,
                             isLive(deck) ? m_trimEndLive   : m_trimEndStaging);

    QElapsedTimer &clock = isLive(deck) ? m_playClockLive   : m_playClockStaging;
    double &anchor       = isLive(deck) ? m_clockAnchorLive : m_clockAnchorStaging;
    bool   &dirty        = isLive(deck) ? m_clockDirtyLive  : m_clockDirtyStaging;
    const bool   repeat    = isLive(deck) ? m_repeatLive    : m_repeatStaging;
    const double trimStart = isLive(deck) ? m_trimStartLive : m_trimStartStaging;
    const double trimEnd   = isLive(deck) ? m_trimEndLive   : m_trimEndStaging;

    if (dirty || !clock.isValid()) {
        anchor = source->currentTime();
        clock.start();
        dirty = false;
    }

    const double speed = isLive(deck) ? m_speedLive : m_speedStaging;
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
    const float t = std::clamp(m_crossfade, 0.f, 1.f);
    const bool cut = m_transitionMode == TransitionMode::Cut;
    const bool onAirLive = (cut ? t < 0.5f : t < 0.999f);
    const bool onAirStaging = (cut ? t >= 0.5f : t > 0.001f);
    if (m_sourceLive) m_sourceLive->setOnAir(onAirLive);
    if (m_sourceStaging) m_sourceStaging->setOnAir(onAirStaging);
    for (const NodeChainSource &c : m_chainLive)
        if (c.source) c.source->setOnAir(onAirLive);
    for (const NodeChainSource &c : m_chainStaging)
        if (c.source) c.source->setOnAir(onAirStaging);
}

double Compositor::requestDeckOut(Deck deck) {
    MediaSource *primary = isLive(deck) ? m_sourceLive.get() : m_sourceStaging.get();
    double longest = primary ? primary->requestOut() : 0.0;
    for (const NodeChainSource &c : isLive(deck) ? m_chainLive : m_chainStaging)
        if (c.source) longest = std::max(longest, c.source->requestOut());
    return longest;
}

void Compositor::updateFrame() {
    // One make-current per tick: the first GL need below takes the context and it stays current
    // until the tick ends.
    struct FrameContext {
        Compositor *c;
        explicit FrameContext(Compositor *comp) : c(comp) { c->m_holdContext = true; }
        ~FrameContext() {
            c->m_holdContext = false;
            if (c->m_contextHeld) {
                c->m_contextHeld = false;
                c->doneCurrent();
            }
        }
    } frameContext(this);

    emit frameTick();
    syncOnAir();
    const bool externalCapture = m_programFrameConsumers > 0 || m_deckFrameConsumers > 0;
    const bool needsCapture = externalCapture || wantsProgramFrame() || wantsDeckFrame();

    bool decodedLive = false, decodedStaging = false;
    bool chainLiveDecoded = false, chainStagingDecoded = false;
    bool waitingForLiveSource = false;

    if (!(m_outputFrozen && !needsCapture)) {
        if (m_playingLive)
            decodedLive = advanceDeckPaced(Deck::Live);
        if (m_playingStaging)
            decodedStaging = advanceDeckPaced(Deck::Staging);

        // A paused async deck still waits for its first (or post-seek) frame.
        if (m_playingLive) m_pendingFrameLive = false;
        else if (m_pendingFrameLive && m_sourceLive && m_sourceLive->nextFrame()) { decodedLive = true; m_pendingFrameLive = false; }
        if (m_playingStaging) m_pendingFrameStaging = false;
        else if (m_pendingFrameStaging && m_sourceStaging && m_sourceStaging->nextFrame()) { decodedStaging = true; m_pendingFrameStaging = false; }

        advanceChainSources(m_chainLive, m_chainTexLive, chainLiveDecoded);
        advanceChainSources(m_chainStaging, m_chainTexStaging, chainStagingDecoded);

        if ((decodedLive || decodedStaging) && makeCurrent()) {
            if (decodedLive)       uploadSourceFrameGL(m_textureLive,       m_sourceLive.get());
            if (decodedStaging)       uploadSourceFrameGL(m_textureStaging,       m_sourceStaging.get());
            doneCurrent();
        }

        waitingForLiveSource =
            (m_playingLive       && m_sourceLive    && !m_sourceLive->isReady())    ||
            (m_playingStaging       && m_sourceStaging    && !m_sourceStaging->isReady());
    }

    for (const Deck deck : {Deck::Live, Deck::Staging}) {
        const MediaSource *src = isLive(deck) ? m_sourceLive.get() : m_sourceStaging.get();
        bool &ready = isLive(deck) ? m_readyLive : m_readyStaging;
        const bool nowReady = src && src->isReady();
        if (nowReady && !ready) {
            ready = true;
            markDirty();
            emit deckReady(deck);
        } else if (!nowReady) {
            ready = false;
        }
    }

    if (!(decodedLive || decodedStaging || chainLiveDecoded || chainStagingDecoded
          || needsCapture || waitingForLiveSource || m_dirty))
        return;

    // Composing from the timer keeps capture working when no display surface is exposed.
    if (renderNow() && externalCapture)
        emit programFrameReady();
}
