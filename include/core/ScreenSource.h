#pragma once

#include "core/MediaSource.h"
#include <QObject>
#include <QImage>
#include <QtCore/qglobal.h>

class QScreen;
class QMediaCaptureSession;
class QVideoSink;
class QVideoFrame;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
class QScreenCapture;
#endif

// MediaSource that captures a display/screen via Qt Multimedia.
//
// Requires Qt 6.5+ (QScreenCapture was introduced in 6.5).
// On older Qt the source compiles cleanly but start() always returns false.
//
// Usage:
//   auto scr = std::make_unique<ScreenSource>();
//   scr->start();                 // primary screen
//   scr->start(someQScreen);      // specific screen
//   videoWidget->setSourceB(std::move(scr));
//   videoWidget->playB();

class ScreenSource : public QObject, public MediaSource {
    Q_OBJECT

public:
    ScreenSource();
    ~ScreenSource() override;

    // Start capturing. Pass nullptr to use the primary screen.
    bool start(QScreen *screen = nullptr);
    void stop();

    bool isCapturing() const;

    Type    type()        const override { return Type::Screen; }
    bool    isReady()     const override { return !m_frame.isNull(); }
    QSize   frameSize()   const override { return m_frame.size(); }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_frame.constBits());
    }
    bool    nextFrame()         override;
    QString displayName() const override { return m_name; }

private slots:
    void onVideoFrameChanged(const QVideoFrame &frame);

private:
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QScreenCapture       *m_capture = nullptr;
    QMediaCaptureSession *m_session = nullptr;
    QVideoSink           *m_sink    = nullptr;
#endif
    QImage  m_frame;   // Format_RGB888, updated on main thread
    bool    m_dirty  = false;
    QString m_name;
};
