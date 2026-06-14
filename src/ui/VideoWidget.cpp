#include "VideoWidget.h"
#include <QTimer>
#include <QDebug>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>

VideoWidget::VideoWidget(QWidget *parent)
    : QOpenGLWidget(parent), player(std::make_unique<VideoPlayer>()) {
    setAcceptDrops(true);
    setStyleSheet("background-color: #000;");

    frameTimer = new QTimer(this);
    connect(frameTimer, &QTimer::timeout, this, &VideoWidget::updateFrame);
}

VideoWidget::~VideoWidget() {
    if (frameTimer) {
        frameTimer->stop();
    }
    if (texture) {
        glDeleteTextures(1, &texture);
    }
}

void VideoWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    qDebug() << "OpenGL initialized";
}

void VideoWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void VideoWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!texture || !player->isOpen()) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, texture);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(0, 0);
    glTexCoord2f(1, 0);
    glVertex2f(width(), 0);
    glTexCoord2f(1, 1);
    glVertex2f(width(), height());
    glTexCoord2f(0, 1);
    glVertex2f(0, height());
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoWidget::loadVideo(const QString &filePath) {
    frameTimer->stop();
    playing = false;

    if (player->open(filePath)) {
        setupTexture();
        update();
        qDebug() << "Video loaded:" << filePath;
    } else {
        qWarning() << "Failed to load video:" << filePath;
    }
}

void VideoWidget::play() {
    if (player->isOpen()) {
        playing = true;
        frameTimer->start(33); // ~30 FPS
    }
}

void VideoWidget::pause() {
    playing = false;
    frameTimer->stop();
}

void VideoWidget::stop() {
    pause();
    player->close();
    texture = 0;
}

void VideoWidget::seek(double seconds) {
    if (player->isOpen()) {
        player->seek(seconds);
        updateTexture();
        update();
    }
}

double VideoWidget::getCurrentTime() const {
    return player->isOpen() ? player->getCurrentTime() : 0.0;
}

double VideoWidget::getDuration() const {
    return player->isOpen() ? player->getDuration() : 0.0;
}

void VideoWidget::setupTexture() {
    if (texture) {
        glDeleteTextures(1, &texture);
    }

    makeCurrent();
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    QSize size = player->getFrameSize();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size.width(), size.height(), 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoWidget::updateTexture() {
    if (!player->isOpen() || !texture) {
        return;
    }

    if (player->decodeFrame()) {
        makeCurrent();
        const uint8_t *frameData = player->getFrameData();
        QSize size = player->getFrameSize();

        glBindTexture(GL_TEXTURE_2D, texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width(), size.height(), GL_RGB, GL_UNSIGNED_BYTE, frameData);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void VideoWidget::updateFrame() {
    if (playing && player->isOpen()) {
        updateTexture();
        update();
    }
}

void VideoWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void VideoWidget::dropEvent(QDropEvent *event) {
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        QUrl url = mimeData->urls().first();
        QString filePath = url.toLocalFile();
        loadVideo(filePath);
        event->acceptProposedAction();
    }
}
