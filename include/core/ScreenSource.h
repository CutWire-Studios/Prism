#pragma once

#include "core/MediaSource.h"
#include <QObject>
#include <QImage>
#include <QString>
#include <QVariantMap>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

class QScreen;

class ScreenSource : public QObject, public MediaSource {
    Q_OBJECT

public:
    enum class CaptureType {
        Monitor = 1,
        Window  = 2,
        Any     = 3,
    };

    ScreenSource();
    ~ScreenSource() override;

    bool start(CaptureType type = CaptureType::Monitor);
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
    void onCreateSessionResponse(uint response, QVariantMap results);
    void onSelectSourcesResponse(uint response, QVariantMap results);
    void onStartResponse(uint response, QVariantMap results);

private:
    void buildGstPipeline(int fd, uint32_t nodeId);
    static GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData);

    enum class State { Idle, CreatingSession, SelectingSources, Starting, Capturing };
    State       m_state       = State::Idle;
    CaptureType m_captureType = CaptureType::Monitor;

    QString m_sessionHandle;
    QString m_name;

    GstElement *m_pipeline = nullptr;
    GstElement *m_appsink  = nullptr;

    QImage m_frame;
    bool   m_dirty = false;
};
