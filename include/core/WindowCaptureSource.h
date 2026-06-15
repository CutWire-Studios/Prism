#pragma once

#include "core/MediaSource.h"
#include <QObject>
#include <QImage>
#include <QCapturableWindow>

class QWindowCapture;
class QMediaCaptureSession;
class QVideoSink;
class QVideoFrame;

// Captures a single application window (or browser tab) via Qt's
// QWindowCapture + QCapturableWindow.  On Wayland with PipeWire this uses
// the xdg-desktop-portal ScreenCast interface so the user is prompted to
// choose exactly which window (or screen) to share.
class WindowCaptureSource : public QObject, public MediaSource {
    Q_OBJECT

public:
    WindowCaptureSource();
    ~WindowCaptureSource() override;

    bool start(const QCapturableWindow &window);
    void stop();

    bool isCapturing() const { return m_capture != nullptr; }

    // Returns all windows currently capturable on this system.
    static QList<QCapturableWindow> capturableWindows();

    Type    type()        const override { return Type::Window; }
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
    QWindowCapture       *m_capture = nullptr;
    QMediaCaptureSession *m_session = nullptr;
    QVideoSink           *m_sink    = nullptr;

    QImage  m_frame;
    bool    m_dirty = false;
    QString m_name;
};
