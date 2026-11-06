#include "ui/canvas/VideoWidget.h"
#include "ui/common/GlWidgetSurface.h"
#include "ui/common/Theme.h"
#include <QCoreApplication>
#include <QEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QPainter>
#include <QFont>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QWindow>
#include <algorithm>

VideoWidget::VideoWidget(QWidget *parent)
    : QOpenGLWidget(parent) {
    setAcceptDrops(true);
    setStyleSheet("background-color: #000;");
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    m_comp = new Compositor(this);
    connect(m_comp, &Compositor::frameReady, this, qOverload<>(&QWidget::update));
    connect(m_comp, &Compositor::programFrameReady, this, &VideoWidget::programFrameReady);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

VideoWidget::~VideoWidget() {
    blockSignals(true);
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
    m_comp->shutdownPlayback();
    // The compositor's textures outlive this widget's context, so nothing to release here.
    delete m_comp;
    m_comp = nullptr;
}

void VideoWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
}

void VideoWidget::resizeGL(int w, int h) {
    // QOpenGLWidget passes device-pixel dimensions here; on Windows HiDPI they
    // exceed width()/height(). All default-FBO drawing must use these sizes.
    m_glWidth  = w;
    m_glHeight = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void VideoWidget::paintGL() {
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    blitProgramToScreen(viewport[2], viewport[3]);
}

void VideoWidget::blitProgramToScreen(int surfaceW, int surfaceH) {
    const GLuint targetFbo = defaultFramebufferObject();
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);

    if (surfaceW <= 0 || surfaceH <= 0) {
        const QSize surface = GlWidgetSurface::drawSize(this, m_glWidth, m_glHeight);
        surfaceW = surface.width();
        surfaceH = surface.height();
    }
    const int w = surfaceW;
    const int h = surfaceH;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glBindTexture(GL_TEXTURE_2D, m_comp->texturesShareable() ? m_comp->programTexture() : 0);
    const QRectF r = programRectOnSurface(QSizeF(w, h), QSizeF(programWidth(), programHeight()),
                                          outputScaleMode());
    const float x0 = (float)r.left(),  y0 = (float)r.top();
    const float x1 = (float)r.right(), y1 = (float)r.bottom();
    glBegin(GL_QUADS);
    glTexCoord2f(0.f, 0.f); glVertex2f(x0, y0);
    glTexCoord2f(1.f, 0.f); glVertex2f(x1, y0);
    glTexCoord2f(1.f, 1.f); glVertex2f(x1, y1);
    glTexCoord2f(0.f, 1.f); glVertex2f(x0, y1);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
}

QRectF VideoWidget::scaleRectToWidget(const QRectF &programRect) const {
    if (programRect.isEmpty() || width() <= 0 || height() <= 0)
        return programRect;
    const QRectF dst = programRectOnSurface(QSizeF(width(), height()),
                                            QSizeF(programWidth(), programHeight()),
                                            outputScaleMode());
    const double sx = dst.width()  / programWidth();
    const double sy = dst.height() / programHeight();
    return QRectF(dst.x() + programRect.x() * sx, dst.y() + programRect.y() * sy,
                  programRect.width() * sx, programRect.height() * sy);
}

void VideoWidget::paintEvent(QPaintEvent *e) {
    QOpenGLWidget::paintEvent(e);
    // Without a usable GL context (no driver, or a headless platform) the widget never gets its
    // FBO, and a QPainter on it dereferences that null FBO.
    if (!isValid() || defaultFramebufferObject() == 0)
        return;

    if (m_comp->panicOverlay() == PanicOverlay::StayTuned) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);

        const QRect r = rect();
        QColor frameColor = Theme::instance().tokens().accent;
        frameColor.setAlpha(120);
        p.setPen(QPen(frameColor, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(24, 24, -24, -24), 12, 12);

        p.setPen(QColor(242, 241, 246));
        QFont titleFont(QStringLiteral("Inter"), 42, QFont::Bold);
        titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        p.setFont(titleFont);
        p.drawText(r.adjusted(0, -20, 0, 0), Qt::AlignCenter, QStringLiteral("Stay Tuned"));
        return;
    }

    if (m_comp->panicOverlay() != PanicOverlay::None)
        return;

    const auto [alphaA, alphaB] = m_comp->computeDeckAlphas();
    if (alphaA <= 0.f && m_comp->overlays(true).isEmpty()) {
        if (alphaB <= 0.f && m_comp->overlays(false).isEmpty()) return;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (!m_comp->overlays(true).isEmpty() && alphaA > 0.f) {
        const QRectF rectA = scaleRectToWidget(m_comp->videoRectProgram(true));
        const QRectF vr = rectA.isEmpty() ? QRectF(0, 0, width(), height()) : rectA;
        m_comp->renderOverlays(p, m_comp->overlays(true), vr, alphaA);
    }
    if (!m_comp->overlays(false).isEmpty() && alphaB > 0.f) {
        const QRectF rectB = m_comp->singleStreamMode()
                           ? QRectF() : scaleRectToWidget(m_comp->videoRectProgram(false));
        const QRectF vr = rectB.isEmpty() ? QRectF(0, 0, width(), height()) : rectB;
        m_comp->renderOverlays(p, m_comp->overlays(false), vr, alphaB);
    }
}

// ── Frameless window chrome ───────────────────────────────────────────────────

namespace {
// The output window can be "fullscreen" either via real showFullScreen() or, on
// macOS, by snapping to the screen geometry (isFullScreen() stays false then —
// see OutputWindow::enterFullscreen). Treat both as fullscreen so window-drag
// stays disabled during a live show.
bool windowIsFullscreen(const QWidget *w) {
    return w && (w->isFullScreen() || w->property("prismManualFullscreen").toBool());
}
} // namespace

Qt::Edges VideoWidget::resizeEdgesAt(const QPointF &pos) const {
    constexpr double kGrip = 8.0;
    Qt::Edges edges;
    if (pos.x() < kGrip) edges |= Qt::LeftEdge;
    else if (pos.x() >= width() - kGrip) edges |= Qt::RightEdge;
    if (pos.y() < kGrip) edges |= Qt::TopEdge;
    else if (pos.y() >= height() - kGrip) edges |= Qt::BottomEdge;
    return edges;
}

void VideoWidget::mousePressEvent(QMouseEvent *event) {
    if (m_framelessWindowChrome && window() && !windowIsFullscreen(window())
        && event->button() == Qt::LeftButton) {
        if (const Qt::Edges edges = resizeEdgesAt(event->position()); edges) {
            if (auto *wh = window()->windowHandle(); wh && wh->startSystemResize(edges)) {
                event->accept();
                return;
            }
        }
        if (auto *wh = window()->windowHandle(); wh && wh->startSystemMove()) {
            event->accept();
            return;
        }
        m_windowDragActive = true;
        m_windowDragOffset = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_framelessWindowChrome && !m_windowDragActive) {
        const Qt::Edges edges = (window() && !windowIsFullscreen(window()))
            ? resizeEdgesAt(event->position()) : Qt::Edges();
        if (edges == Qt::Edges(Qt::LeftEdge | Qt::TopEdge) || edges == Qt::Edges(Qt::RightEdge | Qt::BottomEdge))
            setCursor(Qt::SizeFDiagCursor);
        else if (edges == Qt::Edges(Qt::RightEdge | Qt::TopEdge) || edges == Qt::Edges(Qt::LeftEdge | Qt::BottomEdge))
            setCursor(Qt::SizeBDiagCursor);
        else if (edges & (Qt::LeftEdge | Qt::RightEdge))
            setCursor(Qt::SizeHorCursor);
        else if (edges & (Qt::TopEdge | Qt::BottomEdge))
            setCursor(Qt::SizeVerCursor);
        else
            unsetCursor();
    }
    if (m_windowDragActive && window() && !windowIsFullscreen(window())
        && (event->buttons() & Qt::LeftButton)) {
        window()->move(event->globalPosition().toPoint() - m_windowDragOffset);
        event->accept();
        return;
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (m_framelessWindowChrome && event->button() == Qt::LeftButton)
        m_windowDragActive = false;
    QOpenGLWidget::mouseReleaseEvent(event);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (m_framelessWindowChrome && event->button() == Qt::LeftButton) {
        emit framelessToggleFullscreenRequested();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void VideoWidget::contextMenuEvent(QContextMenuEvent *event) {
    if (m_framelessWindowChrome) {
        emit framelessContextMenuRequested(event->globalPos());
        event->accept();
        return;
    }
    QOpenGLWidget::contextMenuEvent(event);
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
