#pragma once

#include "core/sources/MediaSource.h"
#include <QObject>
#include <QImage>
#include <QString>

/// Thin MediaSource view onto a WebRtcManager session (decoded phone camera frames).
class WebRtcSource : public QObject, public MediaSource {
    Q_OBJECT

public:
    explicit WebRtcSource(const QString &sessionToken);
    ~WebRtcSource() override;

    static bool isAvailable();

    void setName(const QString &name) { m_name = name; }

    Type    type()        const override { return Type::WebRtc; }
    bool    isReady()     const override { return !m_frame.isNull(); }
    QSize   frameSize()   const override { return m_frame.size(); }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_frame.constBits());
    }
    bool    nextFrame()         override;
    QString displayName() const override { return m_name; }

private:
    QString  m_token;
    QImage   m_frame;
    uint64_t m_lastSeq = 0;
    QString  m_name;
};
