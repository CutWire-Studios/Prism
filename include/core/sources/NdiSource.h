#pragma once

#include "core/sources/MediaSource.h"
#include <QImage>
#include <QString>
#include <QStringList>

/// Receives a network NDI source (another CutWire Prism instance, OBS, phone app, …).
class NdiSource : public MediaSource {
public:
    NdiSource();
    ~NdiSource() override;

    static bool        isAvailable();
    static QStringList discoverSources(int waitMs = 2000);

    bool connectTo(const QString &ndiName);
    void disconnect();
    void setName(const QString &name) { m_name = name; }

    Type    type()        const override { return Type::Ndi; }
    bool    isReady()     const override { return !m_frame.isNull(); }
    QSize   frameSize()   const override { return m_frame.size(); }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_frame.constBits());
    }
    bool    nextFrame()         override;
    QString displayName() const override { return m_name; }

private:
    void storeVideoFrame(const QImage &img);

#ifdef PRISM_HAVE_NDI
    struct NDIlib_recv_instance_type *m_recv = nullptr;
#endif
    QImage  m_frame;
    bool    m_dirty     = false;
    bool    m_connected = false;
    QString m_name;
    QString m_ndiName;
};
