#include "ui/VideoWidget.h"
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
    if (m_textureA) glDeleteTextures(1, &m_textureA);
    if (m_textureB) glDeleteTextures(1, &m_textureB);
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
        return {1.f - t, t};
    case TransitionMode::Cut:
        return t < 0.5f ? std::make_pair(1.f, 0.f) : std::make_pair(0.f, 1.f);
    case TransitionMode::WipeLeft:
    case TransitionMode::SlideLeft:
        // Both decks render at full opacity; masking is done geometrically.
        return {1.f, t > 0.f ? 1.f : 0.f};
    case TransitionMode::DipToBlack:
        return {std::max(0.f, 1.f - 2.f * t), std::max(0.f, 2.f * t - 1.f)};
    }
    return {1.f - t, t};
}

void VideoWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_videoRectA = QRectF();
    m_videoRectB = QRectF();

    const float t = std::clamp(m_crossfadeB, 0.f, 1.f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draws one deck at the given alpha. Sets outRect to the computed video rect
    // so overlay painting in paintEvent() can position text/image overlays correctly.
    auto drawDeck = [&](GLuint tex, MediaSource *src,
                        float cx, float cy, float cw, float ch,
                        float baseX, float baseY, float baseW, float baseH,
                        float alpha, QRectF &outRect) {
        if (!tex || !src || !src->isReady() || alpha <= 0.f) return;
        const QRectF bounds(baseX * width(), baseY * height(),
                            baseW * width(), baseH * height());
        outRect = computeContainedRect(src->frameSize(), cw, ch, bounds);
        glColor4f(1.f, 1.f, 1.f, alpha);
        renderTexture(tex, cx, cy, cw, ch,
                      (float)outRect.x(),     (float)outRect.y(),
                      (float)outRect.width(), (float)outRect.height());
    };

    switch (m_transitionMode) {

    case TransitionMode::Crossfade: {
        const float alphaA = 1.f - t, alphaB = t;
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, alphaA, m_videoRectA);
        drawChainSources(m_chainA, m_chainTexA, alphaA, m_videoRectA);
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, alphaB, m_videoRectB);
        drawChainSources(m_chainB, m_chainTexB, alphaB, m_videoRectB);
        break;
    }

    case TransitionMode::Cut: {
        // Hard switch: deck A shown until the crossfader passes centre, then deck B.
        if (t < 0.5f) {
            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA);
            drawChainSources(m_chainA, m_chainTexA, 1.f, m_videoRectA);
        } else {
            drawDeck(m_textureB, m_sourceB.get(),
                     m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                     m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB);
            drawChainSources(m_chainB, m_chainTexB, 1.f, m_videoRectB);
        }
        break;
    }

    case TransitionMode::WipeLeft: {
        // A fills the whole frame. B is revealed from the left as t increases,
        // using a scissor rectangle [0, t*W] × [0, H].
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA);
        drawChainSources(m_chainA, m_chainTexA, 1.f, m_videoRectA);

        if (t > 0.f) {
            glEnable(GL_SCISSOR_TEST);
            // glScissor origin is bottom-left in framebuffer coordinates.
            glScissor(0, 0, static_cast<GLint>(t * width()), height());
            drawDeck(m_textureB, m_sourceB.get(),
                     m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                     m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB);
            drawChainSources(m_chainB, m_chainTexB, 1.f, m_videoRectB);
            glDisable(GL_SCISSOR_TEST);
        }
        break;
    }

    case TransitionMode::SlideLeft: {
        // A slides out to the left; B pushes in from the right edge.
        // glTranslatef shifts all vertex positions for the wrapped draw calls.
        const float offA = -(t * width());
        const float offB = (1.f - t) * width();

        glPushMatrix();
        glTranslatef(offA, 0.f, 0.f);
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA);
        drawChainSources(m_chainA, m_chainTexA, 1.f, m_videoRectA);
        glPopMatrix();

        glPushMatrix();
        glTranslatef(offB, 0.f, 0.f);
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB);
        drawChainSources(m_chainB, m_chainTexB, 1.f, m_videoRectB);
        glPopMatrix();
        break;
    }

    case TransitionMode::DipToBlack: {
        // A fades to black as t reaches 0.5; B fades up from black as t passes 0.5.
        const float alphaA = std::max(0.f, 1.f - 2.f * t);
        const float alphaB = std::max(0.f, 2.f * t - 1.f);
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, alphaA, m_videoRectA);
        drawChainSources(m_chainA, m_chainTexA, alphaA, m_videoRectA);
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, alphaB, m_videoRectB);
        drawChainSources(m_chainB, m_chainTexB, alphaB, m_videoRectB);
        break;
    }

    } // switch

    glDisable(GL_BLEND);
    glColor4f(1.f, 1.f, 1.f, 1.f);
}

void VideoWidget::paintEvent(QPaintEvent *e) {
    QOpenGLWidget::paintEvent(e);

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
    if (m_sourceA->nextFrame())
        uploadSourceFrameGL(m_textureA, m_sourceA.get());
    doneCurrent();
    // Live sources (Camera, Screen, Window, Slideshow, Color) deliver frames
    // asynchronously and are never "ready" at construction time.
    const auto t = m_sourceA->type();
    m_playingA = (t == MediaSource::Type::Camera   ||
                  t == MediaSource::Type::Screen    ||
                  t == MediaSource::Type::Window    ||
                  t == MediaSource::Type::Slideshow);
    update();
}

void VideoWidget::setSourceB(std::unique_ptr<MediaSource> source) {
    m_playingB = false;
    m_sourceB  = std::move(source);
    if (!m_sourceB) return;
    makeCurrent();
    setupTextureGL(m_textureB, m_sourceB->frameSize());
    if (m_sourceB->nextFrame())
        uploadSourceFrameGL(m_textureB, m_sourceB.get());
    doneCurrent();
    const auto t = m_sourceB->type();
    m_playingB = (t == MediaSource::Type::Camera   ||
                  t == MediaSource::Type::Screen    ||
                  t == MediaSource::Type::Window    ||
                  t == MediaSource::Type::Slideshow);
    update();
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
                                    const QRectF &bounds) {
    if (alpha <= 0.f) return;
    for (size_t i = 0; i < chain.size() && i < texList.size(); ++i) {
        auto *src = chain[i].source.get();
        GLuint tex = texList[i];
        if (!tex || !src || !src->isReady()) continue;
        const float cx = chain[i].cropX, cy = chain[i].cropY;
        const float cw = chain[i].cropW, ch = chain[i].cropH;
        const QRectF placement(bounds.left() + chain[i].baseX * bounds.width(),
                               bounds.top()  + chain[i].baseY * bounds.height(),
                               chain[i].baseW * bounds.width(),
                               chain[i].baseH * bounds.height());
        const QRectF r = computeContainedRect(src->frameSize(), cw, ch, placement);
        glColor4f(1.f, 1.f, 1.f, alpha);
        renderTexture(tex, cx, cy, cw, ch,
                      (float)r.x(), (float)r.y(), (float)r.width(), (float)r.height());
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
    m_crossfadeB = std::clamp(mixB, 0.f, 1.f);
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

void VideoWidget::setupTextureGL(GLuint &tex, QSize sz) {
    if (tex) { glDeleteTextures(1, &tex); tex = 0; }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, sz.width(), sz.height(),
                 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoWidget::uploadSourceFrameGL(GLuint &tex, MediaSource *source) {
    if (!source || !source->isReady()) return;
    QSize sz = source->frameSize();
    if (sz.isEmpty()) return;

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
            setupTextureGL(tex, sz); // recreate at correct size
    } else {
        setupTextureGL(tex, sz);
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sz.width(), sz.height(),
                    GL_RGB, GL_UNSIGNED_BYTE, source->frameData());
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ── Frame loop ────────────────────────────────────────────────────────────────

// Advances a source by one frame, respecting trim and repeat.
// Returns true if a new frame was decoded and should be uploaded.
bool VideoWidget::advanceSource(MediaSource *source, bool &playing, bool repeat,
                                double trimStart, double trimEnd) {
    if (!source || !source->isReady()) return false;

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
    const bool hasChainA = !m_chainA.empty();
    const bool hasChainB = !m_chainB.empty();
    if (!m_playingA && !m_playingB && !hasChainA && !hasChainB) return;

    bool decodedA = false, decodedB = false;

    if (m_playingA)
        decodedA = advanceSource(m_sourceA.get(), m_playingA,
                                 m_repeatA, m_trimStartA, m_trimEndA);
    if (m_playingB)
        decodedB = advanceSource(m_sourceB.get(), m_playingB,
                                 m_repeatB, m_trimStartB, m_trimEndB);

    bool chainADecoded = false, chainBDecoded = false;
    advanceChainSources(m_chainA, m_chainTexA, chainADecoded);
    advanceChainSources(m_chainB, m_chainTexB, chainBDecoded);

    if (decodedA || decodedB || chainADecoded || chainBDecoded) {
        makeCurrent();
        if (decodedA) uploadSourceFrameGL(m_textureA, m_sourceA.get());
        if (decodedB) uploadSourceFrameGL(m_textureB, m_sourceB.get());
        doneCurrent();
        update();
    }

    // For live sources that are not yet ready (e.g. camera still starting),
    // keep triggering repaints so the first frame appears as soon as it arrives.
    if ((m_playingA && m_sourceA && !m_sourceA->isReady()) ||
        (m_playingB && m_sourceB && !m_sourceB->isReady())) {
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
