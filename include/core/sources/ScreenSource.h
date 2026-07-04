#pragma once

#include "core/sources/MediaSource.h"
#include <QObject>
#include <QImage>
#include <QString>
#include <QVariantMap>

#ifdef Q_OS_LINUX
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#endif

class QScreenCapture;
class QMediaCaptureSession;
class QVideoSink;
class QVideoFrame;

/// Captures a monitor for deck playback.
/// Linux: PipeWire/xdg-desktop-portal screencast + GStreamer appsink.
/// Windows/macOS: Qt QScreenCapture + QVideoSink.
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
    /// Index into QGuiApplication::screens() (Windows/macOS).
    bool start(int screenIndex);

    void stop();

    bool isCapturing() const;

    /// Stable id used to remember the OS capture selection across recreations.
    /// On Linux this keys the xdg-desktop-portal restore token so the source
    /// restores its previous choice instead of re-prompting.
    void setCaptureId(const QString &id) { m_captureId = id; }
    QString captureId() const { return m_captureId; }

    /// When true (Linux only), the source runs the portal picker to obtain a
    /// fresh selection/restore token and then stops without building a capture
    /// pipeline. Used by the clip node "Edit" button to change the choice.
    void setPickOnly(bool pickOnly) { m_pickOnly = pickOnly; }

    Type    type()        const override { return Type::Screen; }
    bool    isReady()     const override { return !m_frame.isNull(); }
    QSize   frameSize()   const override { return m_frame.size(); }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_frame.constBits());
    }
    bool    nextFrame()         override;
    QString displayName() const override { return m_name; }

signals:
    /// Emitted after the OS picker completes. On success `ok` is true and
    /// `restoreToken` holds the (possibly empty) token the portal issued for
    /// restoring this exact selection later. Emitted with ok=false if the user
    /// cancelled or the handshake failed.
    void captureConfigured(bool ok, const QString &restoreToken);

#ifdef Q_OS_LINUX
private slots:
    void onCreateSessionResponse(uint response, QVariantMap results);
    void onSelectSourcesResponse(uint response, QVariantMap results);
    void onStartResponse(uint response, QVariantMap results);
#endif

#ifndef Q_OS_LINUX
private slots:
    void onVideoFrameChanged(const QVideoFrame &frame);
#endif

private:
#ifdef Q_OS_LINUX
    void buildGstPipeline(int fd, uint32_t nodeId);
    void handlePipelineError(const QString &detail);
    static GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData);
    static gboolean onBusMessage(GstBus *bus, GstMessage *msg, gpointer userData);

    enum class State { Idle, CreatingSession, SelectingSources, Starting, Capturing };
    State       m_state       = State::Idle;
    CaptureType m_captureType = CaptureType::Monitor;

    QString m_sessionHandle;

    GstElement *m_pipeline = nullptr;
    GstElement *m_appsink  = nullptr;
#else
    bool m_capturing = false;

    QScreenCapture       *m_capture = nullptr;
    QMediaCaptureSession *m_session = nullptr;
    QVideoSink           *m_sink    = nullptr;
#endif

    QImage  m_frame;
    bool    m_dirty = false;
    QString m_name;
    QString m_captureId;
    bool    m_pickOnly = false;
    // image-orientation tag reported by pipewiresrc (e.g. "flip-rotate-180" for
    // a vertical flip). Applied to each frame so KWin's flipped screencast shows
    // upright. Empty / "rotate-0" means no correction.
    QString m_orientation;
};
