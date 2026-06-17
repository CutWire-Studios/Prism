#pragma once

#include "ui/ProgramOutputSink.h"
#include <QStringList>
#include <QVector>
#include <memory>

/// Exposes the program mix as a V4L2 virtual webcam (v4l2loopback on Linux).
class VirtualCameraProgramSink : public ProgramOutputSink {
public:
    VirtualCameraProgramSink();
    ~VirtualCameraProgramSink() override;

    QString name() const override;
    bool    isAvailable() const override;
    bool    isActive() const override;

    bool start(const QString &streamName = {}) override;
    void stop() override;
    void submitFrame(const QImage &frame) override;

    QString devicePath() const { return m_devicePath; }
    void    setDevicePath(const QString &path);

    /// Paths of v4l2loopback devices currently present on the system.
    static QStringList availableLoopbackDevices();

    /// Saved setting, else first loopback device, else /dev/video42.
    static QString defaultDevicePath();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool    m_active = false;
    QString m_devicePath;
    QVector<uint8_t> m_yuyvBuffer;
};
