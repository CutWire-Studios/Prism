#pragma once

#include "core/sources/MediaSource.h"
#include <QImage>
#include <QCameraDevice>
#include <memory>

class CameraBackend;

// Camera capture via Qt Multimedia (FFmpeg or GStreamer backend depending on Qt build).
// Works with standard UVC cameras (USB) and Intel IPU6 / MIPI cameras
// that are not accessible via raw V4L2 paths.
//
// The physical device is owned by a shared, reference-counted CameraBackend so
// the same camera can feed several decks/layers at once (and survive rewiring)
// without ever being opened twice — V4L2 devices are exclusive, so a second open
// of a live device fails. Each CameraSource is a lightweight consumer that pulls
// frames from its backend.
class CameraSource : public MediaSource {
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

    bool isCapturing() const { return m_backend != nullptr; }

    Type    type()        const override { return Type::Camera; }
    bool    isReady()     const override;
    bool    hasFailed()   const;
    QString lastError()   const;
    QSize   frameSize()   const override { return m_frame.size(); }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_frame.constBits());
    }
    bool    nextFrame()         override;
    QString displayName() const override { return m_name; }

private:
    std::shared_ptr<CameraBackend> m_backend;
    quint64 m_lastGen = 0;              // last frame generation pulled from backend
    QImage  m_frame;                    // Format_RGB888, shared copy of backend's latest frame
    QString m_name;
};
