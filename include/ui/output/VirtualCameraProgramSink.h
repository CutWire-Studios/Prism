#pragma once

#include "ui/output/ProgramOutputSink.h"
#include <QStringList>
#include <QVector>
#include <memory>

/// Exposes the program mix as a virtual webcam (v4l2loopback on Linux,
/// DirectShow Softcam on Windows).
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

    /// Linux: v4l2loopback device paths. Windows: empty (single Softcam device).
    static QStringList availableLoopbackDevices();

    /// Saved setting, else platform default device name/path.
    static QString defaultDevicePath();

private:
    void stopInternal();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool    m_active = false;
    QString m_devicePath;
};
