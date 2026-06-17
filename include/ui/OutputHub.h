#pragma once

#include <QObject>
#include <QList>
#include <QPointer>
#include <memory>
#include "ui/NdiProgramSink.h"
#include "ui/VirtualCameraProgramSink.h"
#include "ui/ProgramRecorder.h"

class VideoWidget;
class MirrorOutputWindow;

/// Routes the program compositor feed to mirror windows and external sinks (NDI, …).
class OutputHub : public QObject {
    Q_OBJECT

public:
    explicit OutputHub(QObject *parent = nullptr);

    void setProgramSource(VideoWidget *source);
    VideoWidget *programSource() const { return m_source; }

    /// Opens a mirror window showing the current program mix.
    MirrorOutputWindow *addMirrorOutput(const QString &title = {});

    const QList<QPointer<MirrorOutputWindow>> &mirrorOutputs() const { return m_mirrors; }

    // ── NDI program output ────────────────────────────────────────────────────
    bool ndiAvailable() const;
    bool ndiOutputEnabled() const { return m_ndiEnabled; }
    QString ndiStreamName() const;

    /// Start or stop NDI program output. Returns false if NDI is unavailable or start failed.
    bool setNdiOutputEnabled(bool enabled, const QString &streamName = {});

    // ── Virtual camera program output ─────────────────────────────────────────
    bool virtualCameraAvailable() const;
    bool virtualCameraEnabled() const { return m_virtualCameraEnabled; }
    QString virtualCameraDevicePath() const;

    /// Start or stop virtual camera output. Returns false if unavailable or start failed.
    bool setVirtualCameraEnabled(bool enabled, const QString &devicePath = {});

    // ── Program recording ─────────────────────────────────────────────────────
    bool isProgramRecording() const;
    QString recordingOutputPath() const;
    QString recordingMarkersPath() const;

    bool setProgramRecordingEnabled(bool enabled, const QString &outputPath = {});
    void addRecordingMarker(const QString &label);

signals:
    void ndiOutputEnabledChanged(bool enabled);
    void virtualCameraEnabledChanged(bool enabled);
    void programRecordingChanged(bool recording);

private slots:
    void onProgramFrameReady();
    void onMirrorDestroyed(QObject *obj);

private:
    void syncFrameConsumers();
    int  activeFrameConsumerCount() const;
    void placeOnSecondaryScreen(QWidget *window);

    VideoWidget *m_source = nullptr;
    QList<QPointer<MirrorOutputWindow>> m_mirrors;

    std::unique_ptr<NdiProgramSink>            m_ndiSink;
    std::unique_ptr<VirtualCameraProgramSink>  m_virtualCameraSink;
    std::unique_ptr<ProgramRecorder>           m_recorder;
    bool m_ndiEnabled = false;
    bool m_virtualCameraEnabled = false;
};
