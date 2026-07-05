#include "ui/output/ProgramMirrorWidget.h"
#include "ui/common/GlWidgetSurface.h"

ProgramMirrorWidget::ProgramMirrorWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setStyleSheet("background-color: #000;");
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

ProgramMirrorWidget::~ProgramMirrorWidget() {
    makeCurrent();
    if (m_displayTex) glDeleteTextures(1, &m_displayTex);
    doneCurrent();
}

void ProgramMirrorWidget::setFrame(const QImage &frame) {
    if (frame.isNull()) return;
    m_frame = frame;
    update();
}

void ProgramMirrorWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glEnable(GL_TEXTURE_2D);
}

void ProgramMirrorWidget::resizeGL(int w, int h) {
    m_glWidth  = w;
    m_glHeight = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void ProgramMirrorWidget::paintGL() {
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);

    const GLuint targetFbo = defaultFramebufferObject();
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);

    int w = viewport[2];
    int h = viewport[3];
    if (w <= 0 || h <= 0) {
        const QSize surface = GlWidgetSurface::drawSize(this, m_glWidth, m_glHeight);
        w = surface.width();
        h = surface.height();
    }
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    if (m_frame.isNull()) return;

    uploadFrameGL();

    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glBindTexture(GL_TEXTURE_2D, m_displayTex);
    glBegin(GL_QUADS);
    glTexCoord2f(0.f, 0.f); glVertex2f(0.f,     0.f);
    glTexCoord2f(1.f, 0.f); glVertex2f((float)w, 0.f);
    glTexCoord2f(1.f, 1.f); glVertex2f((float)w, (float)h);
    glTexCoord2f(0.f, 1.f); glVertex2f(0.f,     (float)h);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ProgramMirrorWidget::uploadFrameGL() {
    const int w = m_frame.width();
    const int h = m_frame.height();
    if (!m_displayTex || w != m_texW || h != m_texH) {
        if (m_displayTex) glDeleteTextures(1, &m_displayTex);
        glGenTextures(1, &m_displayTex);
        m_texW = w;
        m_texH = h;
        glBindTexture(GL_TEXTURE_2D, m_displayTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_displayTex);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                    GL_RGBA, GL_UNSIGNED_BYTE, m_frame.constBits());
    glBindTexture(GL_TEXTURE_2D, 0);
}
