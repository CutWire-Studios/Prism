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
    if (m_transitionMode == TransitionMode::DipToWhite) {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
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
                        float alpha, QRectF &outRect, int canvasW, int canvasH) {
        if (!tex || !src || !src->isReady() || alpha <= 0.f) return;

        QRectF canvasBounds(0, 0, width(), height());
        if (canvasW > 0 && canvasH > 0) {
            float canvasAR = (float)canvasW / canvasH;
            float windowAR = height() > 0.f ? (float)width() / height() : canvasAR;

            float canvasX, canvasY, canvasRW, canvasRH;
            if (canvasAR > windowAR) {
                canvasRW = width();
                canvasRH = canvasRW / canvasAR;
            } else {
                canvasRH = height();
                canvasRW = canvasRH * canvasAR;
            }
            canvasBounds = QRectF((width() - canvasRW) / 2, (height() - canvasRH) / 2, canvasRW, canvasRH);
        }

        const QRectF bounds(canvasBounds.left() + baseX * canvasBounds.width(),
                            canvasBounds.top()  + baseY * canvasBounds.height(),
                            baseW * canvasBounds.width(),
                            baseH * canvasBounds.height());
        outRect = bounds;

        // Draw a neat outer frame border around the video, but only if the video is not filling the full screen
        // (to prevent drawing borders outside the screen area for normal full-screen play)
        bool isFullScreen = std::abs(bounds.x()) < 1.0f && std::abs(bounds.y()) < 1.0f &&
                            std::abs(bounds.width() - width()) < 2.0f && std::abs(bounds.height() - height()) < 2.0f;
        if (!isFullScreen) {
            glDisable(GL_TEXTURE_2D);
            // Outer white frame border
            glColor4f(0.9f, 0.9f, 0.9f, alpha);
            float bx = std::max(4.f, (float)bounds.width() * 0.015f);
            float by = std::max(4.f, (float)bounds.height() * 0.015f);
            glBegin(GL_QUADS);
            glVertex2f(bounds.x() - bx, bounds.y() - by);
            glVertex2f(bounds.x() + bounds.width() + bx, bounds.y() - by);
            glVertex2f(bounds.x() + bounds.width() + bx, bounds.y() + bounds.height() + by);
            glVertex2f(bounds.x() - bx, bounds.y() + bounds.height() + by);
            glEnd();

            // Inner dark accent border
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
    };

    auto draw3DDeck = [&](GLuint tex, MediaSource *src, float alpha, float aspect) {
        if (!tex || !src || !src->isReady() || alpha <= 0.f) return;

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        glColor4f(1.f, 1.f, 1.f, alpha);
        glBegin(GL_QUADS);
        glTexCoord2f(0.f, 1.f); glVertex3f(-aspect, -1.f, 0.f);
        glTexCoord2f(1.f, 1.f); glVertex3f( aspect, -1.f, 0.f);
        glTexCoord2f(1.f, 0.f); glVertex3f( aspect,  1.f, 0.f);
        glTexCoord2f(0.f, 0.f); glVertex3f(-aspect,  1.f, 0.f);
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
    };

    switch (m_transitionMode) {

    case TransitionMode::Crossfade: {
        const float alphaA = 1.f - t, alphaB = t;
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, alphaA, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, alphaA, m_canvasWidthA, m_canvasHeightA);
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, alphaB, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
        drawChainSources(m_chainB, m_chainTexB, alphaB, m_canvasWidthB, m_canvasHeightB);
        break;
    }

    case TransitionMode::Cut: {
        // Hard switch: deck A shown until the crossfader passes centre, then deck B.
        if (t < 0.5f) {
            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
        } else {
            drawDeck(m_textureB, m_sourceB.get(),
                     m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                     m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
            drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);
        }
        break;
    }

    case TransitionMode::WipeLeft: {
        // A fills the whole frame. B is revealed from the left as t increases,
        // using a scissor rectangle [0, t*W] × [0, H].
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);

        if (t > 0.f) {
            glEnable(GL_SCISSOR_TEST);
            // glScissor origin is bottom-left in framebuffer coordinates.
            glScissor(0, 0, static_cast<GLint>(t * width()), height());
            drawDeck(m_textureB, m_sourceB.get(),
                     m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                     m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
            drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);
            glDisable(GL_SCISSOR_TEST);
        }
        break;
    }

    case TransitionMode::WipeRight: {
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);

        if (t > 0.f) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(static_cast<GLint>((1.f - t) * width()), 0,
                      static_cast<GLint>(t * width()), height());
            drawDeck(m_textureB, m_sourceB.get(),
                     m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                     m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
            drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);
            glDisable(GL_SCISSOR_TEST);
        }
        break;
    }

    case TransitionMode::WipeUp: {
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);

        if (t > 0.f) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, width(), static_cast<GLint>(t * height()));
            drawDeck(m_textureB, m_sourceB.get(),
                     m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                     m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
            drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);
            glDisable(GL_SCISSOR_TEST);
        }
        break;
    }

    case TransitionMode::WipeDown: {
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);

        if (t > 0.f) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, static_cast<GLint>((1.f - t) * height()),
                      width(), static_cast<GLint>(t * height()));
            drawDeck(m_textureB, m_sourceB.get(),
                     m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                     m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
            drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);
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
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
        glPopMatrix();

        glPushMatrix();
        glTranslatef(offB, 0.f, 0.f);
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
        drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);
        glPopMatrix();
        break;
    }

    case TransitionMode::SlideRight: {
        const float offA = t * width();
        const float offB = -(1.f - t) * width();

        glPushMatrix();
        glTranslatef(offA, 0.f, 0.f);
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
        glPopMatrix();

        glPushMatrix();
        glTranslatef(offB, 0.f, 0.f);
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
        drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);
        glPopMatrix();
        break;
    }

    case TransitionMode::SlideUp: {
        const float offA = -(t * height());
        const float offB = (1.f - t) * height();

        glPushMatrix();
        glTranslatef(0.f, offA, 0.f);
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
        glPopMatrix();

        glPushMatrix();
        glTranslatef(0.f, offB, 0.f);
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
        drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);
        glPopMatrix();
        break;
    }

    case TransitionMode::SlideDown: {
        const float offA = t * height();
        const float offB = -(1.f - t) * height();

        glPushMatrix();
        glTranslatef(0.f, offA, 0.f);
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
        glPopMatrix();

        glPushMatrix();
        glTranslatef(0.f, offB, 0.f);
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
        drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);
        glPopMatrix();
        break;
    }

    case TransitionMode::DipToBlack:
    case TransitionMode::DipToWhite: {
        const float alphaA = std::max(0.f, 1.f - 2.f * t);
        const float alphaB = std::max(0.f, 2.f * t - 1.f);
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, alphaA, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, alphaA, m_canvasWidthA, m_canvasHeightA);
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, alphaB, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
        drawChainSources(m_chainB, m_chainTexB, alphaB, m_canvasWidthB, m_canvasHeightB);
        break;
    }

    case TransitionMode::Additive: {
        const float alphaA = 1.f - t;
        const float alphaB = t;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Set blend func to additive
        drawDeck(m_textureA, m_sourceA.get(),
                 m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                 m_baseXA, m_baseYA, m_baseWA, m_baseHA, alphaA, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
        drawChainSources(m_chainA, m_chainTexA, alphaA, m_canvasWidthA, m_canvasHeightA);
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, alphaB, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
        drawChainSources(m_chainB, m_chainTexB, alphaB, m_canvasWidthB, m_canvasHeightB);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Restore standard
        break;
    }

    case TransitionMode::CrossZoom: {
        const float alphaA = 1.f - t;
        const float alphaB = t;

        // Draw Deck A zoomed in
        if (alphaA > 0.f) {
            glPushMatrix();
            glTranslatef(width() / 2.f, height() / 2.f, 0.f);
            float scaleA = 1.f + t * 1.5f; // Zoom in
            glScalef(scaleA, scaleA, 1.f);
            glTranslatef(-width() / 2.f, -height() / 2.f, 0.f);

            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, alphaA, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, alphaA, m_canvasWidthA, m_canvasHeightA);
            glPopMatrix();
        }

        // Draw Deck B zooming in
        if (alphaB > 0.f) {
            glPushMatrix();
            glTranslatef(width() / 2.f, height() / 2.f, 0.f);
            float scaleB = 0.5f + t * 0.5f; // Zoom in from 0.5 to 1.0
            glScalef(scaleB, scaleB, 1.f);
            glTranslatef(-width() / 2.f, -height() / 2.f, 0.f);

            drawDeck(m_textureB, m_sourceB.get(),
                     m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                     m_baseXB, m_baseYB, m_baseWB, m_baseHB, alphaB, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
            drawChainSources(m_chainB, m_chainTexB, alphaB, m_canvasWidthB, m_canvasHeightB);
            glPopMatrix();
        }
        break;
    }

    case TransitionMode::SplitDoor: {
        // Draw Deck B in background
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
        drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);

        // Draw Deck A split doors sliding apart
        if (t < 1.f) {
            glEnable(GL_SCISSOR_TEST);

            // Left half door of A
            glScissor(0, 0, width() / 2, height());
            glPushMatrix();
            glTranslatef(-t * (width() / 2.f), 0.f, 0.f);
            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
            glPopMatrix();

            // Right half door of A
            glScissor(width() / 2, 0, width() - (width() / 2), height());
            glPushMatrix();
            glTranslatef(t * (width() / 2.f), 0.f, 0.f);
            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
            glPopMatrix();

            glDisable(GL_SCISSOR_TEST);
        }
        break;
    }

    case TransitionMode::SplitDoorVert: {
        // Draw Deck B in background
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
        drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);

        // Draw Deck A split doors sliding vertically apart
        if (t < 1.f) {
            glEnable(GL_SCISSOR_TEST);

            // Bottom half door of A
            glScissor(0, 0, width(), height() / 2);
            glPushMatrix();
            glTranslatef(0.f, t * (height() / 2.f), 0.f);
            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
            glPopMatrix();

            // Top half door of A
            glScissor(0, height() / 2, width(), height() - (height() / 2));
            glPushMatrix();
            glTranslatef(0.f, -t * (height() / 2.f), 0.f);
            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
            glPopMatrix();

            glDisable(GL_SCISSOR_TEST);
        }
        break;
    }

    case TransitionMode::VortexSpin: {
        const float alphaA = 1.f - t;
        const float alphaB = t;

        // Draw Deck A spinning and shrinking
        if (alphaA > 0.f) {
            glPushMatrix();
            glTranslatef(width() / 2.f, height() / 2.f, 0.f);
            glScalef(alphaA, alphaA, 1.f);
            glRotatef(t * 180.f, 0.f, 0.f, 1.f);
            glTranslatef(-width() / 2.f, -height() / 2.f, 0.f);

            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, alphaA, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, alphaA, m_canvasWidthA, m_canvasHeightA);
            glPopMatrix();
        }

        // Draw Deck B spinning and growing
        if (alphaB > 0.f) {
            glPushMatrix();
            glTranslatef(width() / 2.f, height() / 2.f, 0.f);
            glScalef(alphaB, alphaB, 1.f);
            glRotatef((t - 1.f) * 180.f, 0.f, 0.f, 1.f);
            glTranslatef(-width() / 2.f, -height() / 2.f, 0.f);

            drawDeck(m_textureB, m_sourceB.get(),
                     m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                     m_baseXB, m_baseYB, m_baseWB, m_baseHB, alphaB, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
            drawChainSources(m_chainB, m_chainTexB, alphaB, m_canvasWidthB, m_canvasHeightB);
            glPopMatrix();
        }
        break;
    }

    case TransitionMode::SplitQuadrants: {
        // Draw Deck B in background
        drawDeck(m_textureB, m_sourceB.get(),
                 m_cropXB, m_cropYB, m_cropWB, m_cropHB,
                 m_baseXB, m_baseYB, m_baseWB, m_baseHB, 1.f, m_videoRectB, m_canvasWidthB, m_canvasHeightB);
        drawChainSources(m_chainB, m_chainTexB, 1.f, m_canvasWidthB, m_canvasHeightB);

        // Draw Deck A split into 4 quadrants sliding towards corners
        if (t < 1.f) {
            glEnable(GL_SCISSOR_TEST);
            const int w2 = width() / 2;
            const int h2 = height() / 2;

            // Top-Left quadrant
            glScissor(0, h2, w2, height() - h2);
            glPushMatrix();
            glTranslatef(-t * w2, -t * h2, 0.f);
            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
            glPopMatrix();

            // Top-Right quadrant
            glScissor(w2, h2, width() - w2, height() - h2);
            glPushMatrix();
            glTranslatef(t * w2, -t * h2, 0.f);
            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
            glPopMatrix();

            // Bottom-Left quadrant
            glScissor(0, 0, w2, h2);
            glPushMatrix();
            glTranslatef(-t * w2, t * h2, 0.f);
            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
            glPopMatrix();

            // Bottom-Right quadrant
            glScissor(w2, 0, width() - w2, h2);
            glPushMatrix();
            glTranslatef(t * w2, t * h2, 0.f);
            drawDeck(m_textureA, m_sourceA.get(),
                     m_cropXA, m_cropYA, m_cropWA, m_cropHA,
                     m_baseXA, m_baseYA, m_baseWA, m_baseHA, 1.f, m_videoRectA, m_canvasWidthA, m_canvasHeightA);
            drawChainSources(m_chainA, m_chainTexA, 1.f, m_canvasWidthA, m_canvasHeightA);
            glPopMatrix();

            glDisable(GL_SCISSOR_TEST);
        }
        break;
    }

    case TransitionMode::Gallery3D: {
        const float alphaA = 1.f - t;
        const float alphaB = t;

        // Switch projection matrix to perspective
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        
        float aspect = (float)width() / height();
        // Setup perspective frustum: FOV = 45 degrees
        float fH = std::tan(22.5f / 180.f * 3.14159265f) * 1.f;
        float fW = fH * aspect;
        glFrustum(-fW, fW, -fH, fH, 1.f, 100.f);
        
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        
        // Eased fader value for camera position
        float ease = t * t * (3.f - 2.f * t);

        // 3D Layout coordinates for slides
        float XB = 2.4f * aspect; // Deck B is positioned to the right
        float YB = -0.3f;          // Deck B is slightly lower
        float ZB = -1.8f;          // Deck B is further back
        float rotB = -15.f;        // Deck B is angled at -15 degrees

        // Camera path animations:
        float d = 2.41421356f;
        float rotB_rad = rotB * 3.14159265f / 180.f;

        // Slide B's target camera position (offset by distance d along Deck B's rotated normal)
        float X1 = XB + d * std::sin(rotB_rad);
        float Y1 = YB;
        float Z1 = ZB + d * std::cos(rotB_rad);

        // Interpolate camera position between Slide A's (0, 0, d) and Slide B's (X1, Y1, Z1)
        float Cx = ease * X1;
        float Cy = ease * Y1;
        float Cz = (1.f - ease) * d + ease * Z1 + std::sin(t * 3.14159265f) * 1.0f; // zoom out in the middle!

        float CrotY = ease * rotB; // pan from 0 to rotB
        float CrotX = std::sin(t * 3.14159265f) * 4.f; // subtle camera pitch tilt for handheld look

        // Apply camera view transformation (inverse of camera model matrix: R_x * R_y * T)
        glRotatef(-CrotX, 1.f, 0.f, 0.f);
        glRotatef(-CrotY, 0.f, 1.f, 0.f);
        glTranslatef(-Cx, -Cy, -Cz);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        // 1. Draw a beautiful, styled studio gallery wall background (Z = -20)
        // This includes a top-left spotlight gradient that matches our frame lighting,
        // overlayed with a large, low-opacity ambient video texture glow.
        glPushMatrix();
        glTranslatef(XB * 0.5f, 0.f, -20.f);
        glScalef(15.f * aspect, 15.f, 1.f);
        glDisable(GL_TEXTURE_2D);
        
        // Studio spotlight gradient (Top-Left is warm golden-white, Bottom-Right is dark charcoal)
        glBegin(GL_QUADS);
        glColor4f(0.18f, 0.14f, 0.08f, 1.0f); // Warm top-left light
        glVertex3f(-1.f,  1.f, 0.f); // top-left
        
        glColor4f(0.04f, 0.03f, 0.02f, 1.0f); // Dark top-right shadow
        glVertex3f( 1.f,  1.f, 0.f); // top-right
        
        glColor4f(0.01f, 0.01f, 0.01f, 1.0f); // Pure black bottom-right
        glVertex3f( 1.f, -1.f, 0.f); // bottom-right
        
        glColor4f(0.04f, 0.03f, 0.02f, 1.0f); // Dark bottom-left shadow
        glVertex3f(-1.f, -1.f, 0.f); // bottom-left
        glEnd();
        
        glEnable(GL_TEXTURE_2D);
        glPopMatrix();

        // 2. Draw ambient video glow textures in the background (Z = -18)
        // Blends from Deck A's colors to Deck B's colors as t changes
        if (alphaA > 0.f && m_textureA && m_sourceA && m_sourceA->isReady()) {
            glPushMatrix();
            glTranslatef(0.f, 0.f, -18.f);
            glScalef(12.f * aspect, 12.f, 1.f);
            
            glBindTexture(GL_TEXTURE_2D, m_textureA);
            glColor4f(0.35f, 0.35f, 0.35f, alphaA * 0.18f);
            glBegin(GL_QUADS);
            glTexCoord2f(0.f, 1.f); glVertex3f(-1.f, -1.f, 0.f);
            glTexCoord2f(1.f, 1.f); glVertex3f( 1.f, -1.f, 0.f);
            glTexCoord2f(1.f, 0.f); glVertex3f( 1.f,  1.f, 0.f);
            glTexCoord2f(0.f, 0.f); glVertex3f(-1.f,  1.f, 0.f);
            glEnd();
            glPopMatrix();
        }

        if (alphaB > 0.f && m_textureB && m_sourceB && m_sourceB->isReady()) {
            glPushMatrix();
            glTranslatef(XB, YB, -18.f);
            glScalef(12.f * aspect, 12.f, 1.f);
            
            glBindTexture(GL_TEXTURE_2D, m_textureB);
            glColor4f(0.35f, 0.35f, 0.35f, alphaB * 0.18f);
            glBegin(GL_QUADS);
            glTexCoord2f(0.f, 1.f); glVertex3f(-1.f, -1.f, 0.f);
            glTexCoord2f(1.f, 1.f); glVertex3f( 1.f, -1.f, 0.f);
            glTexCoord2f(1.f, 0.f); glVertex3f( 1.f,  1.f, 0.f);
            glTexCoord2f(0.f, 0.f); glVertex3f(-1.f,  1.f, 0.f);
            glEnd();
            glPopMatrix();
        }

        // Helper lambda for drawing 3D decks with the golden frame
        auto drawGallery3DDeck = [&](GLuint tex, MediaSource *src, float alpha) {
            if (!tex || !src || !src->isReady() || alpha <= 0.f) return;

            glDisable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            float d0 = 0.006f;
            float d1 = 0.016f;
            float d2 = 0.040f;
            float d3 = 0.052f;
            float d4 = 0.068f;

            // Drop Shadow
            for (int i = 4; i > 0; --i) {
                float shadowOffset = i * 0.012f;
                float shadowAlpha = 0.22f / i * alpha;
                glColor4f(0.0f, 0.0f, 0.0f, shadowAlpha);
                
                float sx_min = -aspect - d4 - shadowOffset + 0.01f;
                float sx_max =  aspect + d4 + shadowOffset + 0.01f;
                float sy_min = -1.f - d4 - shadowOffset - 0.01f;
                float sy_max =  1.f + d4 + shadowOffset - 0.01f;
                
                glBegin(GL_QUADS);
                glVertex3f(sx_min, sy_min, -0.025f);
                glVertex3f(sx_max, sy_min, -0.025f);
                glVertex3f(sx_max, sy_max, -0.025f);
                glVertex3f(sx_min, sy_max, -0.025f);
                glEnd();
            }

            auto draw3DFrameTier = [&](float a, float b, 
                                       float r_lt, float g_lt, float b_lt,
                                       float r_rb, float g_rb, float b_rb,
                                       float z_offset) {
                glBegin(GL_QUADS);
                glColor4f(r_lt, g_lt, b_lt, alpha);
                glVertex3f(-aspect - b, -1.f - b, z_offset);
                glVertex3f(-aspect - a, -1.f - a, z_offset);
                glVertex3f(-aspect - a,  1.f + a, z_offset);
                glVertex3f(-aspect - b,  1.f + b, z_offset);
                glEnd();

                glBegin(GL_QUADS);
                glColor4f(r_rb, g_rb, b_rb, alpha);
                glVertex3f(aspect + a, -1.f - a, z_offset);
                glVertex3f(aspect + b, -1.f - b, z_offset);
                glVertex3f(aspect + b,  1.f + b, z_offset);
                glVertex3f(aspect + a,  1.f + a, z_offset);
                glEnd();

                glBegin(GL_QUADS);
                glColor4f(r_lt, g_lt, b_lt, alpha);
                glVertex3f(-aspect - b, -1.f - b, z_offset);
                glVertex3f( aspect + b, -1.f - b, z_offset);
                glVertex3f( aspect + a, -1.f - a, z_offset);
                glVertex3f(-aspect - a, -1.f - a, z_offset);
                glEnd();

                glBegin(GL_QUADS);
                glColor4f(r_rb, g_rb, b_rb, alpha);
                glVertex3f(-aspect - a,  1.f + a, z_offset);
                glVertex3f( aspect + a,  1.f + a, z_offset);
                glVertex3f( aspect + b,  1.f + b, z_offset);
                glVertex3f(-aspect - b,  1.f + b, z_offset);
                glEnd();
            };

            // Render Concentric Moldings
            draw3DFrameTier(0.f, d0, 0.08f, 0.06f, 0.04f, 0.04f, 0.03f, 0.02f, -0.002f);
            draw3DFrameTier(d0, d1, 0.95f, 0.82f, 0.35f, 0.55f, 0.40f, 0.12f, -0.005f);
            draw3DFrameTier(d1, d2, 0.45f, 0.32f, 0.15f, 0.28f, 0.18f, 0.08f, -0.010f);
            draw3DFrameTier(d2, d3, 1.00f, 0.92f, 0.50f, 0.65f, 0.48f, 0.08f, -0.015f);
            draw3DFrameTier(d3, d4, 0.90f, 0.75f, 0.25f, 0.50f, 0.35f, 0.05f, -0.020f);

            // Specular shiny highlight sweep
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            float shinePos = -aspect * 1.5f + t * (aspect * 3.0f);
            float sw = aspect * 0.25f;
            glColor4f(1.0f, 0.97f, 0.85f, 0.55f * alpha);
            glBegin(GL_QUADS);
            glVertex3f(shinePos - sw, -1.f - d4, -0.018f);
            glVertex3f(shinePos + sw, -1.f - d4, -0.018f);
            glVertex3f(shinePos + sw + 0.4f, 1.f + d4, -0.018f);
            glVertex3f(shinePos - sw + 0.4f, 1.f + d4, -0.018f);
            glEnd();

            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // Video Quad
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex);
            glColor4f(1.f, 1.f, 1.f, alpha);
            glBegin(GL_QUADS);
            glTexCoord2f(0.f, 1.f); glVertex3f(-aspect, -1.f, 0.f);
            glTexCoord2f(1.f, 1.f); glVertex3f( aspect, -1.f, 0.f);
            glTexCoord2f(1.f, 0.f); glVertex3f( aspect,  1.f, 0.f);
            glTexCoord2f(0.f, 0.f); glVertex3f(-aspect,  1.f, 0.f);
            glEnd();
            glBindTexture(GL_TEXTURE_2D, 0);
        };

        // 2. Draw Deck A at (0, 0, 0)
        if (alphaA > 0.f) {
            glPushMatrix();
            drawGallery3DDeck(m_textureA, m_sourceA.get(), alphaA);
            glPopMatrix();
        }

        // 3. Draw Deck B at (XB, YB, ZB) with Y-rotation rotB
        if (alphaB > 0.f) {
            glPushMatrix();
            glTranslatef(XB, YB, ZB);
            glRotatef(rotB, 0.f, 1.f, 0.f);
            drawGallery3DDeck(m_textureB, m_sourceB.get(), alphaB);
            glPopMatrix();
        }

        glDisable(GL_DEPTH_TEST);

        // Pop matrices
        glPopMatrix(); // modelview
        glMatrixMode(GL_PROJECTION);
        glPopMatrix(); // projection
        glMatrixMode(GL_MODELVIEW);

        // 4. Draw sweeping golden light leak screen-space overlay
        float shineAlpha = 0.6f * std::sin(t * 3.14159265f);
        if (shineAlpha > 0.f) {
            float w = width();
            float h = height();
            float tilt = 120.f;
            float bandWidth = 280.f;
            float center = -bandWidth + t * (w + bandWidth * 2.f);

            glDisable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);

            glBegin(GL_QUADS);
            // Left Half
            glColor4f(1.f, 0.92f, 0.82f, 0.f);
            glVertex2f(center - bandWidth / 2.f, 0.f);
            glColor4f(1.f, 0.92f, 0.82f, shineAlpha);
            glVertex2f(center, 0.f);
            glColor4f(1.f, 0.92f, 0.82f, shineAlpha);
            glVertex2f(center + tilt, h);
            glColor4f(1.f, 0.92f, 0.82f, 0.f);
            glVertex2f(center + tilt - bandWidth / 2.f, h);

            // Right Half
            glColor4f(1.f, 0.92f, 0.82f, shineAlpha);
            glVertex2f(center, 0.f);
            glColor4f(1.f, 0.92f, 0.82f, 0.f);
            glVertex2f(center + bandWidth / 2.f, 0.f);
            glColor4f(1.f, 0.92f, 0.82f, 0.f);
            glVertex2f(center + tilt + bandWidth / 2.f, h);
            glColor4f(1.f, 0.92f, 0.82f, shineAlpha);
            glVertex2f(center + tilt, h);
            glEnd();

            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_TEXTURE_2D);
        }
        break;
    }

    case TransitionMode::Cube3D: {
        const float alphaA = 1.f - t;
        const float alphaB = t;

        // Switch projection matrix to perspective
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        
        float aspect = (float)width() / height();
        // Setup perspective frustum: FOV = 45 degrees
        float fH = std::tan(22.5f / 180.f * 3.14159265f) * 1.f;
        float fW = fH * aspect;
        glFrustum(-fW, fW, -fH, fH, 1.f, 100.f);
        
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        
        // Z translation matches frustum/FOV so that scale=1 fits the viewport exactly.
        // Face A is at Z = aspect locally, so camera moves back by 2.41421356f + aspect.
        float cameraDist = 2.41421356f + aspect;
        glTranslatef(0.f, 0.f, -cameraDist);
        
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        // Face A rotates out to the left (-Y rotation)
        if (alphaA > 0.f) {
            glPushMatrix();
            glRotatef(-t * 90.f, 0.f, 1.f, 0.f);
            glTranslatef(0.f, 0.f, aspect);
            draw3DDeck(m_textureA, m_sourceA.get(), alphaA, aspect);
            glPopMatrix();
        }

        // Face B rotates in from the right (+Y rotation)
        if (alphaB > 0.f) {
            glPushMatrix();
            glRotatef(90.f - t * 90.f, 0.f, 1.f, 0.f);
            glTranslatef(0.f, 0.f, aspect);
            draw3DDeck(m_textureB, m_sourceB.get(), alphaB, aspect);
            glPopMatrix();
        }

        glDisable(GL_DEPTH_TEST);

        glPopMatrix(); // pop modelview
        glMatrixMode(GL_PROJECTION);
        glPopMatrix(); // pop projection
        glMatrixMode(GL_MODELVIEW);
        break;
    }

    case TransitionMode::Flip3D: {
        const float alphaA = 1.f - t;
        const float alphaB = t;

        // Switch projection matrix to perspective
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        
        float aspect = (float)width() / height();
        // Setup perspective frustum: FOV = 45 degrees
        float fH = std::tan(22.5f / 180.f * 3.14159265f) * 1.f;
        float fW = fH * aspect;
        glFrustum(-fW, fW, -fH, fH, 1.f, 100.f);
        
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        
        // Card is at Z = 0 locally, so camera moves back by 2.41421356f.
        glTranslatef(0.f, 0.f, -2.41421356f);
        
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        // Rotate the entire scene by -t * 180 degrees
        glPushMatrix();
        glRotatef(-t * 180.f, 0.f, 1.f, 0.f);
        
        if (t < 0.5f) {
            draw3DDeck(m_textureA, m_sourceA.get(), alphaA, aspect);
        } else {
            // Flip the back side card by 180 degrees so the video texture is facing the camera
            glRotatef(180.f, 0.f, 1.f, 0.f);
            draw3DDeck(m_textureB, m_sourceB.get(), alphaB, aspect);
        }
        
        glPopMatrix();

        glDisable(GL_DEPTH_TEST);

        glPopMatrix(); // pop modelview
        glMatrixMode(GL_PROJECTION);
        glPopMatrix(); // pop projection
        glMatrixMode(GL_MODELVIEW);
        break;
    }

    } // switch

    // HTML overlay composited on top (RGBA, transparent parts show the A/B video)
    if (m_textureOverlay && m_htmlOverlay && m_htmlOverlay->isReady()) {
        const QRectF screenBounds(0, 0, width(), height());
        QRectF ovlRect = computeContainedRect(m_htmlOverlay->frameSize(), 1, 1, screenBounds);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        renderTexture(m_textureOverlay, 0, 0, 1, 1,
                      (float)ovlRect.x(),     (float)ovlRect.y(),
                      (float)ovlRect.width(), (float)ovlRect.height());
    }

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
                  t == MediaSource::Type::Html);
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
                  t == MediaSource::Type::Html);
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

    // Compute canvas bounds relative to the window — same logic as drawDeck — so
    // chain sources are positioned in the same coordinate space as the deck clip.
    QRectF canvasBounds(0, 0, width(), height());
    if (canvasW > 0 && canvasH > 0) {
        float canvasAR = (float)canvasW / canvasH;
        float windowAR = height() > 0.f ? (float)width() / height() : canvasAR;
        float cW, cH;
        if (canvasAR > windowAR) {
            cW = width();
            cH = cW / canvasAR;
        } else {
            cH = height();
            cW = cH * canvasAR;
        }
        canvasBounds = QRectF((width() - cW) / 2.f, (height() - cH) / 2.f, cW, cH);
    }

    for (size_t i = 0; i < chain.size() && i < texList.size(); ++i) {
        auto *src = chain[i].source.get();
        GLuint tex = texList[i];
        if (!tex || !src || !src->isReady()) continue;
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
