#include "core/ScreenSource.h"
#include <QVideoFrame>
#include <QDebug>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#  include <QScreenCapture>
#  include <QMediaCaptureSession>
#  include <QVideoSink>
#  include <QGuiApplication>
#  include <QScreen>
#endif

ScreenSource::ScreenSource() = default;

ScreenSource::~ScreenSource() {
    stop();
}

bool ScreenSource::start(QScreen *screen) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    stop();

    QScreen *scr = screen ? screen : QGuiApplication::primaryScreen();
    if (!scr) {
        qWarning() << "ScreenSource: no screen available";
        return false;
    }

    m_name    = scr->name();
    m_capture = new QScreenCapture();
    m_session = new QMediaCaptureSession();
    m_sink    = new QVideoSink();

    m_capture->setScreen(scr);
    m_session->setScreenCapture(m_capture);
    m_session->setVideoSink(m_sink);

    connect(m_sink, &QVideoSink::videoFrameChanged,
            this,   &ScreenSource::onVideoFrameChanged,
            Qt::QueuedConnection);

    m_capture->start();
    qDebug() << "ScreenSource: started" << m_name;
    return true;
#else
    Q_UNUSED(screen);
    qWarning() << "ScreenSource: requires Qt 6.5+, this build has Qt" << QT_VERSION_STR;
    return false;
#endif
}

void ScreenSource::stop() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (m_capture) m_capture->stop();
    if (m_session) {
        m_session->setScreenCapture(nullptr);
        m_session->setVideoSink(nullptr);
    }
    delete m_sink;    m_sink    = nullptr;
    delete m_session; m_session = nullptr;
    delete m_capture; m_capture = nullptr;
#endif
    m_frame = {};
    m_dirty = false;
}

bool ScreenSource::isCapturing() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return m_capture != nullptr;
#else
    return false;
#endif
}

// ── Frame delivery ────────────────────────────────────────────────────────────

// Runs on the main thread (Qt::QueuedConnection).
void ScreenSource::onVideoFrameChanged(const QVideoFrame &frame) {
    if (!frame.isValid()) return;
    QImage img = frame.toImage().convertToFormat(QImage::Format_RGB888);
    if (!img.isNull()) {
        m_frame = std::move(img);
        m_dirty = true;
    }
}

bool ScreenSource::nextFrame() {
    if (!m_dirty) return false;
    m_dirty = false;
    return true;
}
