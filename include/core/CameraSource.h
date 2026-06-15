#pragma once

#include "core/MediaSource.h"
#include <QObject>
#include <QImage>
#include <QCameraDevice>

class QCamera;
class QMediaCaptureSession;
class QVideoSink;
class QVideoFrame;

// Camera capture via Qt Multimedia (GStreamer backend on Linux).
// Works with standard UVC cameras (USB) and Intel IPU6 / MIPI cameras
// that are not accessible via raw V4L2 paths.
class CameraSource : public QObject, public MediaSource {
    Q_OBJECT

public:
    CameraSource();
    ~CameraSource() override;

    // Start from a Qt-enumerated device (preferred).
    bool start(const QCameraDevice &device = {});

    // Fallback: start using a raw V4L2 path (e.g. "/dev/video2").
    // Only works when the camera IS a standard V4L2 device.
    bool startDevice(const QString &v4l2Path);

    void stop();
    void setName(const QString &name) { m_name = name; }

    bool isCapturing() const { return m_camera != nullptr; }

    Type    type()        const override { return Type::Camera; }
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
    QCamera              *m_camera  = nullptr;
    QMediaCaptureSession *m_session = nullptr;
    QVideoSink           *m_sink    = nullptr;

    QImage  m_frame;   // Format_RGB888, updated on main thread
    bool    m_dirty  = false;
    QString m_name;
};
