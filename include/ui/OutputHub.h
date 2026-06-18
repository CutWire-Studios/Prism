#pragma once

#include <QObject>
#include <QList>
#include <QPointer>
#include <QTimer>
#include <unordered_map>
#include <memory>
#include "ui/NdiProgramSink.h"
#include "ui/VirtualCameraProgramSink.h"
#include "ui/ProgramRecorder.h"
#include "ui/RecordingOptions.h"
#include "ui/ClipNodeModel.h"

class VideoWidget;
class MirrorOutputWindow;

/// Routes the program compositor feed to mirror windows and external sinks (NDI, …).
class OutputHub : public QObject {
    Q_OBJECT

public:
    explicit OutputHub(QObject *parent = nullptr);
    ~OutputHub() override;

    void setProgramSource(VideoWidget *source);
    VideoWidget *programSource() const { return m_source; }

    void setActiveDeckNodes(NodeId deckA, NodeId deckB);

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

    // ── Multi-track recording ─────────────────────────────────────────────────
    bool isRecording() const;
    bool isProgramRecording() const;
    RecordingOptions recordingOptions() const { return m_recordingOptions; }
    QString recordingOutputStem() const { return m_recordingStem; }
    QStringList activeRecordingTrackLabels() const;
    QStringList recordingOutputPaths() const;
    QString recordingMarkersPath() const;
    qint64 recordingDurationMs() const;

    bool startRecording(const RecordingOptions &opts);
    void stopRecording();
    bool setProgramRecordingEnabled(bool enabled, const QString &outputPath = {});
    void addRecordingMarker(const QString &label);

signals:
    void ndiOutputEnabledChanged(bool enabled);
    void virtualCameraEnabledChanged(bool enabled);
    void recordingChanged(bool recording);
    void recordingProgress(qint64 elapsedMs);

private slots:
    void onProgramFrameReady();
    void onMirrorDestroyed(QObject *obj);
    void onRecordingProgressTick();

private:
    void writeCombinedMarkersFile() const;
    static QString sanitizeFileStem(const QString &name);
    void syncFrameConsumers();
    int  activeFrameConsumerCount() const;
    bool needsDeckFrameReadback() const;
    void placeOnSecondaryScreen(QWidget *window);

    VideoWidget *m_source = nullptr;
    QList<QPointer<MirrorOutputWindow>> m_mirrors;

    std::unique_ptr<NdiProgramSink>            m_ndiSink;
    std::unique_ptr<VirtualCameraProgramSink>  m_virtualCameraSink;
    std::unique_ptr<ProgramRecorder>           m_programRecorder;
    std::unique_ptr<ProgramRecorder>           m_deckARecorder;
    std::unique_ptr<ProgramRecorder>           m_deckBRecorder;
    std::unordered_map<NodeId, std::unique_ptr<ProgramRecorder>> m_sourceRecorders;

    RecordingOptions m_recordingOptions;
    QString          m_recordingStem;
    QString          m_combinedMarkersPath;
    QStringList      m_lastRecordingPaths;
    NodeId           m_activeDeckA = 0;
    NodeId           m_activeDeckB = 0;
    QTimer          *m_progressTimer = nullptr;

    bool m_ndiEnabled = false;
    bool m_virtualCameraEnabled = false;
};
