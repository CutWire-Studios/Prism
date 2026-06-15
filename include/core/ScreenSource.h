#pragma once

#include "core/MediaSource.h"
#include <QObject>
#include <QImage>
#include <QString>
#include <QVariantMap>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

class QScreen;

// MediaSource that captures a display or window via the XDG Desktop Portal
// ScreenCast D-Bus API (org.freedesktop.portal.ScreenCast), reading frames
// through a PipeWire stream via GStreamer's pipewiresrc element.
//
// The portal shows its own picker UI — the user selects the capture source
// interactively. Pass CaptureType to control which sources are offered.
//
// Usage:
//   auto scr = std::make_unique<ScreenSource>();
//   scr->start(ScreenSource::CaptureType::Monitor);   // screen picker
//   scr->start(ScreenSource::CaptureType::Window);    // window/tab picker
//   videoWidget->setSourceB(std::move(scr));
//   videoWidget->playB();

class ScreenSource : public QObject, public MediaSource {
    Q_OBJECT

public:
    enum class CaptureType {
        Monitor = 1,  // physical displays only
        Window  = 2,  // application windows / browser tabs
        Any     = 3,  // let the user choose either
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

    QImage  m_frame;
    bool    m_dirty = false;
};
