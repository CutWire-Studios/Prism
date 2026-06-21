#include "core/sources/WindowCaptureSource.h"
#include <QWindowCapture>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QCapturableWindow>
#include <QDebug>

WindowCaptureSource::WindowCaptureSource() = default;

WindowCaptureSource::~WindowCaptureSource() {
    stop();
}

QList<QCapturableWindow> WindowCaptureSource::capturableWindows() {
    return QWindowCapture::capturableWindows();
}

bool WindowCaptureSource::start(const QCapturableWindow &window) {
    stop();

    m_name    = window.description().isEmpty() ? "(unknown window)" : window.description();
    m_capture = new QWindowCapture();
    m_session = new QMediaCaptureSession();
    m_sink    = new QVideoSink();

    m_capture->setWindow(window);
    m_session->setWindowCapture(m_capture);
    m_session->setVideoSink(m_sink);

    connect(m_sink, &QVideoSink::videoFrameChanged,
            this,   &WindowCaptureSource::onVideoFrameChanged,
            Qt::QueuedConnection);

    m_capture->start();
    qDebug() << "WindowCaptureSource: started" << m_name;
    return true;
}

void WindowCaptureSource::stop() {
    if (m_capture) m_capture->stop();
    if (m_session) {
        m_session->setWindowCapture(nullptr);
        m_session->setVideoSink(nullptr);
    }
    delete m_sink;    m_sink    = nullptr;
    delete m_session; m_session = nullptr;
    delete m_capture; m_capture = nullptr;
    m_frame = {};
    m_dirty = false;
}

void WindowCaptureSource::onVideoFrameChanged(const QVideoFrame &frame) {
    if (!frame.isValid()) return;
    QImage img = frame.toImage().convertToFormat(QImage::Format_RGB888);
    if (!img.isNull()) {
        m_frame = std::move(img);
        m_dirty = true;
    }
}

bool WindowCaptureSource::nextFrame() {
    if (!m_dirty) return false;
    m_dirty = false;
    return true;
}
