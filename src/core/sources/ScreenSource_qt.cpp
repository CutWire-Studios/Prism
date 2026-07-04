#include "core/sources/ScreenSource.h"
#include <QScreenCapture>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

ScreenSource::ScreenSource() = default;

ScreenSource::~ScreenSource() {
    stop();
}

bool ScreenSource::start(CaptureType type) {
    if (type != CaptureType::Monitor) {
        qWarning() << "ScreenSource: window capture uses WindowCaptureSource on this platform";
        return false;
    }
    return start(0);
}

bool ScreenSource::start(int screenIndex) {
    stop();

    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty()) {
        qWarning() << "ScreenSource: no screens available";
        return false;
    }

    const int idx = qBound(0, screenIndex, screens.size() - 1);
    QScreen *screen = screens.at(idx);

    m_capture = new QScreenCapture();
    m_session = new QMediaCaptureSession();
    m_sink    = new QVideoSink();

    m_capture->setScreen(screen);
    m_session->setScreenCapture(m_capture);
    m_session->setVideoSink(m_sink);

    connect(m_sink, &QVideoSink::videoFrameChanged,
            this,   &ScreenSource::onVideoFrameChanged,
            Qt::QueuedConnection);

    m_capture->start();
    m_name       = QStringLiteral("Screen %1").arg(idx + 1);
    m_capturing  = true;
    qDebug() << "ScreenSource: started" << m_name << screen->name();
    return true;
}

void ScreenSource::stop() {
    if (m_capture)
        m_capture->stop();
    if (m_session) {
        m_session->setScreenCapture(nullptr);
        m_session->setVideoSink(nullptr);
    }
    delete m_sink;
    delete m_session;
    delete m_capture;
    m_sink    = nullptr;
    m_session = nullptr;
    m_capture = nullptr;

    m_frame      = {};
    m_dirty      = false;
    m_capturing  = false;
    m_name.clear();
}

bool ScreenSource::isCapturing() const {
    return m_capturing;
}

bool ScreenSource::nextFrame() {
    if (!m_dirty) return false;
    m_dirty = false;
    return true;
}

void ScreenSource::onVideoFrameChanged(const QVideoFrame &frame) {
    if (!frame.isValid()) return;
    QImage img = frame.toImage().convertToFormat(QImage::Format_RGB888);
    if (!img.isNull()) {
        m_frame = std::move(img);
        m_dirty = true;
    }
}
