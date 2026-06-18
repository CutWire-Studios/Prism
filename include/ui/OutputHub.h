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
    enum class TrackKind {
        Program,
        DeckA,
        DeckB,
        Source
    };

    explicit OutputHub(QObject *parent = nullptr);
    ~OutputHub() override;

    void setProgramSource(VideoWidget *source);
    VideoWidget *programSource() const { return m_source; }

    void setActiveDeckNodes(NodeId deckA, NodeId deckB);

    QString outputDir() const { return m_outputDir; }
    void setOutputDir(const QString &dir);

    /// Opens a mirror window showing the current program mix.
    MirrorOutputWindow *addMirrorOutput(const QString &title = {});

    const QList<QPointer<MirrorOutputWindow>> &mirrorOutputs() const { return m_mirrors; }

    // ── NDI program output ────────────────────────────────────────────────────
    bool ndiAvailable() const;
    bool ndiOutputEnabled() const { return m_ndiEnabled; }
    QString ndiStreamName() const;

    bool setNdiOutputEnabled(bool enabled, const QString &streamName = {});

    // ── Virtual camera program output ─────────────────────────────────────────
    bool virtualCameraAvailable() const;
    bool virtualCameraEnabled() const { return m_virtualCameraEnabled; }
    QString virtualCameraDevicePath() const;

    bool setVirtualCameraEnabled(bool enabled, const QString &devicePath = {});

    // ── Independent per-stream recording ──────────────────────────────────────
    bool isRecording() const;
    bool isProgramRecording() const;
    bool isTrackRecording(TrackKind kind, NodeId sourceNodeId = 0) const;

    bool startProgramRecording();
    void stopProgramRecording();
    bool startDeckARecording();
    void stopDeckARecording();
    bool startDeckBRecording();
    void stopDeckBRecording();
    bool startSourceRecording(NodeId nodeId, const QString &label);
    void stopSourceRecording(NodeId nodeId);

    void stopAllRecording();
    void addRecordingMarker(const QString &label);

    QStringList activeRecordingTrackLabels() const;
    qint64 longestActiveRecordingMs() const;
    qint64 trackRecordingDurationMs(TrackKind kind, NodeId sourceNodeId = 0) const;

signals:
    void ndiOutputEnabledChanged(bool enabled);
    void virtualCameraEnabledChanged(bool enabled);
    void recordingStateChanged();
    void recordingProgress(qint64 elapsedMs);
    void recordingError(const QString &message);

private slots:
    void onProgramFrameReady();
    void onMirrorDestroyed(QObject *obj);
    void onRecordingProgressTick();

private:
    static QString sanitizeFileStem(const QString &name);
    QString makeTrackOutputPath(const QString &suffix) const;
    void syncFrameConsumers();
    int  activeFrameConsumerCount() const;
    bool needsDeckFrameReadback() const;
    void ensureProgressTimer();
    void maybeStopProgressTimer();
    void placeOnSecondaryScreen(QWidget *window);
    ProgramRecorder *recorderFor(TrackKind kind, NodeId sourceNodeId = 0) const;

    VideoWidget *m_source = nullptr;
    QList<QPointer<MirrorOutputWindow>> m_mirrors;

    std::unique_ptr<NdiProgramSink>            m_ndiSink;
    std::unique_ptr<VirtualCameraProgramSink>  m_virtualCameraSink;
    std::unique_ptr<ProgramRecorder>           m_programRecorder;
    std::unique_ptr<ProgramRecorder>           m_deckARecorder;
    std::unique_ptr<ProgramRecorder>           m_deckBRecorder;
    std::unordered_map<NodeId, std::unique_ptr<ProgramRecorder>> m_sourceRecorders;

    QString  m_outputDir;
    NodeId   m_activeDeckA = 0;
    NodeId   m_activeDeckB = 0;
    QTimer  *m_progressTimer = nullptr;

    bool m_ndiEnabled = false;
    bool m_virtualCameraEnabled = false;
};
