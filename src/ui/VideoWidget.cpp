#include "ui/VideoWidget.h"
#include "ui/Transition.h"
#include "core/VideoFileSource.h"
#include "core/ImageSource.h"
#include <QTimer>
#include <QDebug>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QPainter>
#include <QFont>
#include <QFileInfo>
#include <algorithm>

VideoWidget::VideoWidget(QWidget *parent)
    : QOpenGLWidget(parent) {
    setAcceptDrops(true);
    setStyleSheet("background-color: #000;");

    m_frameTimer = new QTimer(this);
    connect(m_frameTimer, &QTimer::timeout, this, &VideoWidget::updateFrame);
    m_frameTimer->start(33); // ~30 FPS
}

VideoWidget::~VideoWidget() {
    m_frameTimer->stop();
    makeCurrent();
    destroyProgramFbo();
    if (m_textureA)       glDeleteTextures(1, &m_textureA);
    if (m_textureB)       glDeleteTextures(1, &m_textureB);
    if (m_textureOverlay) glDeleteTextures(1, &m_textureOverlay);
    clearChainTextures(m_chainTexA);
    clearChainTextures(m_chainTexB);
    doneCurrent();
}

// ── OpenGL callbacks ──────────────────────────────────────────────────────────

void VideoWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
}

void VideoWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

std::pair<float,float> VideoWidget::computeDeckAlphas() const {
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

void VideoWidget::paintGL() {
    ensureProgramFbo();

    // Compose the program mix at fixed resolution (single source of truth).
    m_compW = kProgramWidth;
    m_compH = kProgramHeight;
    glBindFramebuffer(GL_FRAMEBUFFER, m_programFbo);
    glViewport(0, 0, m_compW, m_compH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, m_compW, m_compH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    renderCompositionGL();
    if (m_programFrameConsumers > 0)
        cacheProgramFrameFromFbo();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_compW = 0;
    m_compH = 0;

    blitProgramToScreen();
    emit programFrameReady();
}

void VideoWidget::renderCompositionGL() {
    if (m_panicOverlay == PanicOverlay::Blackout) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_videoRectA = QRectF();
        m_videoRectB = QRectF();
        return;
    }

    if (m_panicOverlay == PanicOverlay::StayTuned) {
        glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_videoRectA = QRectF();
        m_videoRectB = QRectF();
        return;
    }

    if (m_transitionMode == TransitionMode::DipToWhite) {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_videoRectA = QRectF();
    m_videoRectB = QRectF();

    const float t = std::clamp(m_crossfadeB, 0.f, 1.f);

    const GLuint deckTexA = (m_sourceA && m_sourceA->glTexture())
                          ? m_sourceA->glTexture() : m_textureA;
    const GLuint deckTexB = (m_sourceB && m_sourceB->glTexture())
                          ? m_sourceB->glTexture() : m_textureB;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto drawSide = [&](GLuint tex, MediaSource *src,
                        float cx, float cy, float cw, float ch,
                        float baseX, float baseY, float baseW, float baseH,
                        float alpha, QRectF &outRect, int canvasW, int canvasH,
                        std::vector<NodeChainSource> &chain,
                        std::vector<GLuint> &chainTex) {
        if (alpha <= 0.f) return;

        if (tex && src && src->isReady()) {
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

            const QRectF bounds(canvasBounds.left() + baseX * canvasBounds.width(),
                                canvasBounds.top()  + baseY * canvasBounds.height(),
                                baseW * canvasBounds.width(),
                                baseH * canvasBounds.height());
            outRect = bounds;

            if (m_transitionMode == TransitionMode::Gallery3D) {
                glDisable(GL_TEXTURE_2D);
                glColor4f(0.9f, 0.9f, 0.9f, alpha);
                float bx = std::max(4.f, (float)bounds.width() * 0.015f);
                float by = std::max(4.f, (float)bounds.height() * 0.015f);
                glBegin(GL_QUADS);
                glVertex2f(bounds.x() - bx, bounds.y() - by);
                glVertex2f(bounds.x() + bounds.width() + bx, bounds.y() - by);
                glVertex2f(bounds.x() + bounds.width() + bx, bounds.y() + bounds.height() + by);
                glVertex2f(bounds.x() - bx, bounds.y() + bounds.height() + by);
                glEnd();

                glColor4f(0.1f, 0.1f, 0.1f, alpha);
                float ix = std::max(1.f, bx * 0.2f);
                float iy = std::max(1.f, by * 0.2f);
                glBegin(GL_QUADS);
                glVertex2f(bounds.x() - ix, bounds.y() - iy);
                glVertex2f(bounds.x() + bounds.width() + ix, bounds.y() - iy);
                glVertex2f(bounds.x() + bounds.width() + ix, bounds.y() + bounds.height() + iy);
                glVertex2f(bounds.x() - ix, bounds.y() + bounds.height() + iy);
                glEnd();
                glEnable(GL_TEXTURE_2D);
            }

            glColor4f(1.f, 1.f, 1.f, alpha);
            renderTexture(tex, cx, cy, cw, ch,
                          (float)bounds.x(),     (float)bounds.y(),
                          (float)bounds.width(), (float)bounds.height());
        }

        drawChainSources(chain, chainTex, alpha, canvasW, canvasH);
    };

    auto drawSideA = [&](float alpha) {
        drawSide(deckTexA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, alpha,
                 m_videoRectA, m_canvasWidthA, m_canvasHeightA, m_chainA, m_chainTexA);
    };
    auto drawSideB = [&](float alpha) {
        drawSide(deckTexB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, alpha,
                 m_videoRectB, m_canvasWidthB, m_canvasHeightB, m_chainB, m_chainTexB);
    };

    const bool inB = m_transitionTowardB;
    const float p  = inB ? t : 1.f - t;

    Transition::Context ctx;
    ctx.width   = renderW();
    ctx.height  = renderH();
    ctx.t       = t;
    ctx.p       = p;
    ctx.drawOut = [&](float a) { inB ? drawSideA(a) : drawSideB(a); };
    ctx.drawIn  = [&](float a) { inB ? drawSideB(a) : drawSideA(a); };
    ctx.texA   = deckTexA;
    ctx.texB   = deckTexB;
    ctx.readyA = deckTexA && m_sourceA && m_sourceA->isReady();
    ctx.readyB = deckTexB && m_sourceB && m_sourceB->isReady();
    Transition::forMode(m_transitionMode).paint(ctx);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (m_textureOverlay && m_htmlOverlay && m_htmlOverlay->isReady()) {
        const QRectF screenBounds(0, 0, renderW(), renderH());
        QRectF ovlRect = computeContainedRect(m_htmlOverlay->frameSize(), 1, 1, screenBounds);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        renderTexture(m_textureOverlay, 0, 0, 1, 1,
                      (float)ovlRect.x(),     (float)ovlRect.y(),
                      (float)ovlRect.width(), (float)ovlRect.height());
    }

    glDisable(GL_BLEND);
    glColor4f(1.f, 1.f, 1.f, 1.f);

    // Scale video rects from program space to widget space for QPainter overlays.
    m_videoRectA = scaleRectToWidget(m_videoRectA);
    m_videoRectB = scaleRectToWidget(m_videoRectB);
}

void VideoWidget::ensureProgramFbo() {
    if (m_programFbo) return;

    glGenFramebuffers(1, &m_programFbo);
    glGenTextures(1, &m_programColorTex);

    glBindTexture(GL_TEXTURE_2D, m_programColorTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kProgramWidth, kProgramHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_programFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_programColorTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void VideoWidget::destroyProgramFbo() {
    if (m_programColorTex) { glDeleteTextures(1, &m_programColorTex); m_programColorTex = 0; }
    if (m_programFbo)      { glDeleteFramebuffers(1, &m_programFbo); m_programFbo = 0; }
}

void VideoWidget::blitProgramToScreen() {
    glViewport(0, 0, width(), height());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width(), height(), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glBindTexture(GL_TEXTURE_2D, m_programColorTex);
    glBegin(GL_QUADS);
    glTexCoord2f(0.f, 0.f); glVertex2f(0.f,          0.f);
    glTexCoord2f(1.f, 0.f); glVertex2f((float)width(), 0.f);
    glTexCoord2f(1.f, 1.f); glVertex2f((float)width(), (float)height());
    glTexCoord2f(0.f, 1.f); glVertex2f(0.f,          (float)height());
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
}

QRectF VideoWidget::scaleRectToWidget(const QRectF &programRect) const {
    if (programRect.isEmpty() || width() <= 0 || height() <= 0)
        return programRect;
    const double sx = (double)width()  / kProgramWidth;
    const double sy = (double)height() / kProgramHeight;
    return QRectF(programRect.x() * sx, programRect.y() * sy,
                  programRect.width() * sx, programRect.height() * sy);
}

void VideoWidget::addProgramFrameConsumer() {
    ++m_programFrameConsumers;
}

void VideoWidget::removeProgramFrameConsumer() {
    m_programFrameConsumers = std::max(0, m_programFrameConsumers - 1);
}

void VideoWidget::setProgramFrameConsumerCount(int count) {
    m_programFrameConsumers = std::max(0, count);
}

void VideoWidget::cacheProgramFrameFromFbo() {
    if (!m_programFbo) return;

    m_programFrameCache = QImage(kProgramWidth, kProgramHeight, QImage::Format_RGBA8888);
    glReadPixels(0, 0, kProgramWidth, kProgramHeight,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_programFrameCache.bits());
    m_programFrameCache = m_programFrameCache.flipped(Qt::Vertical);
}

void VideoWidget::paintEvent(QPaintEvent *e) {
    QOpenGLWidget::paintEvent(e);

    if (m_panicOverlay == PanicOverlay::StayTuned) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);

        const QRect r = rect();
        p.setPen(QPen(QColor(42, 140, 160, 120), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(24, 24, -24, -24), 12, 12);

        p.setPen(QColor(230, 230, 235));
        QFont titleFont(QStringLiteral("Segoe UI"), 42, QFont::Bold);
        titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        p.setFont(titleFont);
        p.drawText(r.adjusted(0, -20, 0, 0), Qt::AlignCenter, QStringLiteral("Stay Tuned"));
        return;
    }

    if (m_panicOverlay != PanicOverlay::None)
        return;

    const auto [alphaA, alphaB] = computeDeckAlphas();
    if (alphaA <= 0.f && m_overlaysA.isEmpty()) {
        if (alphaB <= 0.f && m_overlaysB.isEmpty()) return;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (!m_overlaysA.isEmpty() && alphaA > 0.f) {
        const QRectF vr = m_videoRectA.isEmpty()
                        ? QRectF(0, 0, width(), height()) : m_videoRectA;
        renderOverlays(p, m_overlaysA, vr, alphaA);
    }
    if (!m_overlaysB.isEmpty() && alphaB > 0.f) {
        const QRectF vr = m_videoRectB.isEmpty()
                        ? QRectF(0, 0, width(), height()) : m_videoRectB;
        renderOverlays(p, m_overlaysB, vr, alphaB);
    }
}

// ── Texture rendering ─────────────────────────────────────────────────────────

void VideoWidget::renderTexture(GLuint tex, float cx, float cy, float cw, float ch,
                                float dstX, float dstY, float dstW, float dstH) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_QUADS);
    glTexCoord2f(cx,      cy);      glVertex2f(dstX,        dstY);
    glTexCoord2f(cx + cw, cy);      glVertex2f(dstX + dstW, dstY);
    glTexCoord2f(cx + cw, cy + ch); glVertex2f(dstX + dstW, dstY + dstH);
    glTexCoord2f(cx,      cy + ch); glVertex2f(dstX,        dstY + dstH);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
}

QRectF VideoWidget::computeContainedRect(QSize fs, float cw, float ch, const QRectF &bounds) const {
    if (fs.isEmpty()) return bounds.isEmpty() ? QRectF(0, 0, width(), height()) : bounds;

    QRectF box = bounds.isEmpty() ? QRectF(0, 0, width(), height()) : bounds;
    float videoAR  = (fs.width()  * cw) / (fs.height() * ch);
    float widgetAR = box.height() > 0.f ? (float)box.width() / (float)box.height() : videoAR;

    float dstW, dstH, dstX, dstY;
    if (videoAR > widgetAR) {           // wider — letterbox top/bottom
        dstW = box.width();
        dstH = dstW / videoAR;
        dstX = box.left();
        dstY = box.top() + (box.height() - dstH) / 2.f;
    } else {                            // taller — pillarbox left/right
        dstH = box.height();
        dstW = dstH * videoAR;
        dstX = box.left() + (box.width() - dstW) / 2.f;
        dstY = box.top();
    }
    return QRectF(dstX, dstY, dstW, dstH);
}

void VideoWidget::renderOverlays(QPainter &p, const QList<OverlayItem> &overlays,
                                 const QRectF &videoRect, float globalAlpha) {
    if (globalAlpha <= 0.f) return;

    const QRectF &vr = videoRect.isEmpty()
                     ? QRectF(0, 0, width(), height())
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
            if (!m_overlayPixCache.contains(ov.content))
                m_overlayPixCache.insert(ov.content, QPixmap(ov.content));
            const QPixmap &pm = m_overlayPixCache[ov.content];
            if (!pm.isNull())
                p.drawPixmap(r.toRect(),
                    pm.scaled(r.size().toSize(),
                              Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
    p.setOpacity(1.0);
}

// ── Load ─────────────────────────────────────────────────────────────────────

// Private helper: detect type, construct the right MediaSource, upload first frame.
void VideoWidget::loadSourceInternal(const QString &filePath,
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

    makeCurrent();
    setupTextureGL(tex, target->frameSize());
    target->nextFrame();                     // prime first frame (no-op for images)
    uploadSourceFrameGL(tex, target.get());
    doneCurrent();
    update();
}

void VideoWidget::loadVideoA(const QString &filePath) {
    loadSourceInternal(filePath, m_sourceA, m_textureA, m_playingA);
}

void VideoWidget::loadVideoB(const QString &filePath) {
    loadSourceInternal(filePath, m_sourceB, m_textureB, m_playingB);
}

void VideoWidget::setSourceA(std::unique_ptr<MediaSource> source) {
    m_playingA = false;
    m_sourceA  = std::move(source);
    if (!m_sourceA) return;
    makeCurrent();
    // frameSize() may be (0,0) for async sources like Camera; uploadSourceFrameGL
    // handles resizing the texture when the first real frame arrives.
    setupTextureGL(m_textureA, m_sourceA->frameSize());
    if (m_sourceA->nextFrame()) {
        // nextFrame() may switch GL contexts internally (e.g. DynamicInterfaceSource
        // creates its own context for offscreen QML rendering). Re-assert ours.
        makeCurrent();
        uploadSourceFrameGL(m_textureA, m_sourceA.get());
    }
    doneCurrent();
    // Live sources deliver frames continuously via the frame timer.
    const auto t = m_sourceA->type();
    m_playingA = (t == MediaSource::Type::Camera    ||
                  t == MediaSource::Type::Screen     ||
                  t == MediaSource::Type::Window     ||
                  t == MediaSource::Type::Slideshow  ||
                  t == MediaSource::Type::Canvas     ||
                  t == MediaSource::Type::Html       ||
                  t == MediaSource::Type::Ndi);
    update();
}

void VideoWidget::setSourceB(std::unique_ptr<MediaSource> source) {
    m_playingB = false;
    m_sourceB  = std::move(source);
    if (!m_sourceB) return;
    makeCurrent();
    setupTextureGL(m_textureB, m_sourceB->frameSize());
    if (m_sourceB->nextFrame()) {
        makeCurrent();
        uploadSourceFrameGL(m_textureB, m_sourceB.get());
    }
    doneCurrent();
    const auto t = m_sourceB->type();
    m_playingB = (t == MediaSource::Type::Camera    ||
                  t == MediaSource::Type::Screen     ||
                  t == MediaSource::Type::Window     ||
                  t == MediaSource::Type::Slideshow  ||
                  t == MediaSource::Type::Canvas     ||
                  t == MediaSource::Type::Html       ||
                  t == MediaSource::Type::Ndi);
    update();
}

void VideoWidget::setHtmlOverlay(std::unique_ptr<MediaSource> source) {
    m_playingOverlay = false;
    m_htmlOverlay    = std::move(source);
    if (!m_htmlOverlay) {
        makeCurrent();
        if (m_textureOverlay) {
            glDeleteTextures(1, &m_textureOverlay);
            m_textureOverlay = 0;
        }
        doneCurrent();
        update();
        return;
    }
    makeCurrent();
    // Overlay textures are always RGBA so transparent HTML parts show the video underneath.
    setupTextureGL(m_textureOverlay, m_htmlOverlay->frameSize(), true);
    if (m_htmlOverlay->nextFrame()) {
        makeCurrent();
        uploadSourceFrameGL(m_textureOverlay, m_htmlOverlay.get());
    }
    doneCurrent();
    m_playingOverlay = true;
    update();
}

void VideoWidget::clearHtmlOverlay() {
    setHtmlOverlay(nullptr);
}

// ── Playback control ──────────────────────────────────────────────────────────

void VideoWidget::playA()  { if (m_sourceA) m_playingA = true; }
void VideoWidget::pauseA() { m_playingA = false; }
void VideoWidget::playB()  { if (m_sourceB) m_playingB = true; }
void VideoWidget::pauseB() { m_playingB = false; }

void VideoWidget::stop() {
    m_playingA = false;
    m_playingB = false;
}

void VideoWidget::seekA(double seconds) {
    if (!m_sourceA || !m_sourceA->isReady()) return;
    m_sourceA->seek(seconds);
    if (m_sourceA->nextFrame()) {
        makeCurrent();
        uploadSourceFrameGL(m_textureA, m_sourceA.get());
        doneCurrent();
        update();
    }
}

void VideoWidget::seekB(double seconds) {
    if (!m_sourceB || !m_sourceB->isReady()) return;
    m_sourceB->seek(seconds);
    if (m_sourceB->nextFrame()) {
        makeCurrent();
        uploadSourceFrameGL(m_textureB, m_sourceB.get());
        doneCurrent();
        update();
    }
}

// ── Trim ─────────────────────────────────────────────────────────────────────

void VideoWidget::setTrimPointsA(double s, double e) { m_trimStartA = s; m_trimEndA = e; }
void VideoWidget::setTrimPointsB(double s, double e) { m_trimStartB = s; m_trimEndB = e; }

// ── Crop ─────────────────────────────────────────────────────────────────────

void VideoWidget::setCropA(float x, float y, float w, float h) {
    m_cropXA = x; m_cropYA = y; m_cropWA = w; m_cropHA = h;
    update();
}

void VideoWidget::setCropB(float x, float y, float w, float h) {
    m_cropXB = x; m_cropYB = y; m_cropWB = w; m_cropHB = h;
    update();
}

void VideoWidget::setBaseA(float x, float y, float w, float h) {
    m_baseXA = x; m_baseYA = y; m_baseWA = w; m_baseHA = h;
    update();
}

void VideoWidget::setBaseB(float x, float y, float w, float h) {
    m_baseXB = x; m_baseYB = y; m_baseWB = w; m_baseHB = h;
    update();
}

void VideoWidget::setCanvasSizeA(int width, int height) {
    m_canvasWidthA = width;
    m_canvasHeightA = height;
    update();
}

void VideoWidget::setCanvasSizeB(int width, int height) {
    m_canvasWidthB = width;
    m_canvasHeightB = height;
    update();
}

// ── Overlays ─────────────────────────────────────────────────────────────────

void VideoWidget::setOverlaysA(const QList<OverlayItem> &overlays) {
    m_overlaysA = overlays;
    m_overlayPixCache.clear();
    update();
}

void VideoWidget::setOverlaysB(const QList<OverlayItem> &overlays) {
    m_overlaysB = overlays;
    m_overlayPixCache.clear();
    update();
}

// ── Node chain compositing ────────────────────────────────────────────────────

void VideoWidget::clearChainTextures(std::vector<GLuint> &texList) {
    for (GLuint t : texList)
        if (t) glDeleteTextures(1, &t);
    texList.clear();
}

void VideoWidget::primeChainSources(std::vector<NodeChainSource> &chain,
                                     std::vector<GLuint> &texList) {
    texList.resize(chain.size(), 0);
    for (size_t i = 0; i < chain.size(); ++i) {
        auto *src = chain[i].source.get();
        if (!src) continue;
        if (!src->isReady()) src->nextFrame(); // prime first frame if possible
        if (src->isReady())
            setupTextureGL(texList[i], src->frameSize());
        if (src->isReady())
            uploadSourceFrameGL(texList[i], src);
    }
}

void VideoWidget::setNodeChainA(std::vector<NodeChainSource> chain) {
    makeCurrent();
    clearChainTextures(m_chainTexA);
    m_chainA = std::move(chain);
    primeChainSources(m_chainA, m_chainTexA);
    doneCurrent();
    update();
}

void VideoWidget::setNodeChainB(std::vector<NodeChainSource> chain) {
    makeCurrent();
    clearChainTextures(m_chainTexB);
    m_chainB = std::move(chain);
    primeChainSources(m_chainB, m_chainTexB);
    doneCurrent();
    update();
}

void VideoWidget::drawChainSources(std::vector<NodeChainSource> &chain,
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
        auto *src = chain[i].source.get();
        if (!src || !src->isReady()) continue;
        GLuint tex = src->glTexture() ? src->glTexture() : texList[i];
        if (!tex) continue;
        const float cx = chain[i].cropX, cy = chain[i].cropY;
        const float cw = chain[i].cropW, ch = chain[i].cropH;
        const QRectF placement(canvasBounds.left() + chain[i].baseX * canvasBounds.width(),
                               canvasBounds.top()  + chain[i].baseY * canvasBounds.height(),
                               chain[i].baseW * canvasBounds.width(),
                               chain[i].baseH * canvasBounds.height());
        glColor4f(1.f, 1.f, 1.f, alpha);
        renderTexture(tex, cx, cy, cw, ch,
                      (float)placement.x(), (float)placement.y(), (float)placement.width(), (float)placement.height());
    }
}

void VideoWidget::advanceChainSources(std::vector<NodeChainSource> &chain,
                                       std::vector<GLuint> &texList, bool &anyDecoded) {
    for (size_t i = 0; i < chain.size() && i < texList.size(); ++i) {
        auto *src = chain[i].source.get();
        if (!src) continue;
        // Live/async sources that aren't yet ready: try to prime them
        if (!src->isReady()) {
            src->nextFrame();
            if (src->isReady()) {
                makeCurrent();
                setupTextureGL(texList[i], src->frameSize());
                uploadSourceFrameGL(texList[i], src);
                doneCurrent();
                anyDecoded = true;
            }
            continue;
        }
        if (!chain[i].playing) continue;
        if (src->nextFrame()) {
            makeCurrent();
            uploadSourceFrameGL(texList[i], src);
            doneCurrent();
            anyDecoded = true;
        }
    }
}

// ── Query ─────────────────────────────────────────────────────────────────────

double VideoWidget::getCurrentTimeA() const {
    return m_sourceA ? m_sourceA->currentTime() : 0.0;
}
double VideoWidget::getDurationA() const {
    return m_sourceA ? m_sourceA->duration() : 0.0;
}
double VideoWidget::getCurrentTimeB() const {
    return m_sourceB ? m_sourceB->currentTime() : 0.0;
}
double VideoWidget::getDurationB() const {
    return m_sourceB ? m_sourceB->duration() : 0.0;
}

void VideoWidget::setCrossfade(float mixB) {
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
    update();
}

void VideoWidget::setPanicOverlay(PanicOverlay overlay) {
    if (m_panicOverlay == overlay) return;
    m_panicOverlay = overlay;
    update();
}

void VideoWidget::setOutputFrozen(bool frozen) {
    if (m_outputFrozen == frozen) return;
    m_outputFrozen = frozen;
    update();
}

QImage VideoWidget::getFrameA() const {
    if (!m_sourceA || !m_sourceA->isReady()) return {};
    QSize sz = m_sourceA->frameSize();
    return QImage(m_sourceA->frameData(), sz.width(), sz.height(),
                  sz.width() * 3, QImage::Format_RGB888).copy();
}

QImage VideoWidget::getFrameB() const {
    if (!m_sourceB || !m_sourceB->isReady()) return {};
    QSize sz = m_sourceB->frameSize();
    return QImage(m_sourceB->frameData(), sz.width(), sz.height(),
                  sz.width() * 3, QImage::Format_RGB888).copy();
}

// ── GL helpers ────────────────────────────────────────────────────────────────

void VideoWidget::setupTextureGL(GLuint &tex, QSize sz, bool alpha) {
    if (tex) { glDeleteTextures(1, &tex); tex = 0; }
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
}

void VideoWidget::uploadSourceFrameGL(GLuint &tex, MediaSource *source) {
    if (!source || !source->isReady()) return;
    // Source already has its frame in a shared GL texture — nothing to upload.
    if (source->glTexture()) return;
    QSize sz = source->frameSize();
    if (sz.isEmpty()) return;

    const bool alpha = source->hasAlpha();
    const GLenum fmt = alpha ? GL_RGBA : GL_RGB;

    // Check whether the existing texture matches the incoming frame size.
    // This handles async sources (Camera, Screen) whose first frame size
    // may differ from the (0,0) placeholder used at construction time.
    if (tex) {
        GLint texW = 0, texH = 0;
        glBindTexture(GL_TEXTURE_2D, tex);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &texW);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texH);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (texW != sz.width() || texH != sz.height())
            setupTextureGL(tex, sz, alpha);
    } else {
        setupTextureGL(tex, sz, alpha);
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sz.width(), sz.height(),
                    fmt, GL_UNSIGNED_BYTE, source->frameData());
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ── Frame loop ────────────────────────────────────────────────────────────────

// Advances a source by one frame, respecting trim and repeat.
// Returns true if a new frame was decoded and should be uploaded.
bool VideoWidget::advanceSource(MediaSource *source, bool &playing, bool repeat,
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

void VideoWidget::updateFrame() {
    if (m_outputFrozen) return;

    const bool hasChainA = !m_chainA.empty();
    const bool hasChainB = !m_chainB.empty();
    if (!m_playingA && !m_playingB && !m_playingOverlay && !hasChainA && !hasChainB) return;

    bool decodedA = false, decodedB = false, decodedOverlay = false;

    if (m_playingA)
        decodedA = advanceSource(m_sourceA.get(), m_playingA,
                                 m_repeatA, m_trimStartA, m_trimEndA);
    if (m_playingB)
        decodedB = advanceSource(m_sourceB.get(), m_playingB,
                                 m_repeatB, m_trimStartB, m_trimEndB);
    if (m_playingOverlay && m_htmlOverlay)
        decodedOverlay = advanceSource(m_htmlOverlay.get(), m_playingOverlay,
                                       false, 0.0, -1.0);

    bool chainADecoded = false, chainBDecoded = false;
    advanceChainSources(m_chainA, m_chainTexA, chainADecoded);
    advanceChainSources(m_chainB, m_chainTexB, chainBDecoded);

    if (decodedA || decodedB || decodedOverlay || chainADecoded || chainBDecoded) {
        makeCurrent();
        if (decodedA)       uploadSourceFrameGL(m_textureA,       m_sourceA.get());
        if (decodedB)       uploadSourceFrameGL(m_textureB,       m_sourceB.get());
        if (decodedOverlay) uploadSourceFrameGL(m_textureOverlay, m_htmlOverlay.get());
        doneCurrent();
        update();
    }

    // For live sources that are not yet ready (e.g. camera still starting),
    // keep triggering repaints so the first frame appears as soon as it arrives.
    if ((m_playingA       && m_sourceA    && !m_sourceA->isReady())    ||
        (m_playingB       && m_sourceB    && !m_sourceB->isReady())    ||
        (m_playingOverlay && m_htmlOverlay && !m_htmlOverlay->isReady())) {
        update();
    }
}

// ── Drag & drop ───────────────────────────────────────────────────────────────

void VideoWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void VideoWidget::dropEvent(QDropEvent *event) {
    const QMimeData *md = event->mimeData();
    if (md->hasUrls()) {
        loadVideoA(md->urls().first().toLocalFile());
        event->acceptProposedAction();
    }
}
