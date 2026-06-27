#pragma once

#include <QObject>
#include <QList>
#include <QPointer>
#include <QTimer>
#include <QImage>
#include <QMutex>
#include <QRecursiveMutex>
#include <QWaitCondition>
#include <unordered_map>
#include <atomic>
#include <memory>
#include "ui/output/NdiProgramSink.h"
#include "ui/output/VirtualCameraProgramSink.h"
#include "ui/output/ProgramFrameSource.h"
#include "ui/recording/ProgramRecorder.h"
#include "ui/recording/ProgramAudioRecorder.h"
#include "ui/recording/RecordingOptions.h"
#include "ui/nodes/ClipNodeModel.h"

class VideoWidget;
class MirrorOutputWindow;
class QThread;

/// Routes the program compositor feed to mirror windows and external sinks (NDI, …).
class OutputHub : public QObject {
    Q_OBJECT

public:
    enum class TrackKind {
        Program,
        DeckA,
        DeckB,
        Source,
        ProgramAudio,
        DeckAAudio,
        DeckBAudio,
        ClipAudio
    };

    explicit OutputHub(QObject *parent = nullptr);
    ~OutputHub() override;

    void setProgramSource(VideoWidget *source);
    void setProgramSourceForTest(ProgramFrameSource *source);
    VideoWidget *programSource() const;

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

    bool startProgramAudioRecording();
    void stopProgramAudioRecording();
    bool startDeckAAudioRecording();
    void stopDeckAAudioRecording();
    bool startDeckBAudioRecording();
    void stopDeckBAudioRecording();
    bool startClipAudioRecording(NodeId nodeId, const QString &label);
    void stopClipAudioRecording(NodeId nodeId);
    bool isProgramAudioRecording() const;
    void submitProgramAudioChunk(int deckIndex, const QByteArray &pcm);
    void submitDeckAudioChunk(int deckIndex, NodeId clipId, const QByteArray &pcm);
    void submitMicProgramAudioChunk(const QByteArray &pcm);

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
    QString makeAudioTrackOutputPath(const QString &suffix) const;
    void syncFrameConsumers();
    int  activeFrameConsumerCount() const;
    bool needsDeckFrameReadback() const;

    // Background frame dispatch: keeps encode/convert/blocking-IO off the GUI
    // thread. onProgramFrameReady() (GUI) does the GL readback and hands the
    // frames to m_dispatchThread, which fans them out to the heavy sinks.
    void dispatchLoop();
    void distributeFrames(const QImage &program, const QImage &deckA, const QImage &deckB);
    void ensureProgressTimer();
    void maybeStopProgressTimer();
    void placeOnSecondaryScreen(QWidget *window);
    ProgramRecorder *recorderFor(TrackKind kind, NodeId sourceNodeId = 0) const;
    ProgramAudioRecorder *audioRecorderFor(TrackKind kind, NodeId sourceNodeId = 0) const;

    ProgramFrameSource *m_frameSource = nullptr;
    QPointer<VideoWidget> m_videoWidget;
    QList<QPointer<MirrorOutputWindow>> m_mirrors;

    std::unique_ptr<NdiProgramSink>            m_ndiSink;
    std::unique_ptr<VirtualCameraProgramSink>  m_virtualCameraSink;
    std::unique_ptr<ProgramRecorder>           m_programRecorder;
    std::unique_ptr<ProgramAudioRecorder>      m_programAudioRecorder;
    std::unique_ptr<ProgramAudioRecorder>      m_deckAAudioRecorder;
    std::unique_ptr<ProgramAudioRecorder>      m_deckBAudioRecorder;
    std::unordered_map<NodeId, std::unique_ptr<ProgramAudioRecorder>> m_clipAudioRecorders;
    std::unique_ptr<ProgramRecorder>           m_deckARecorder;
    std::unique_ptr<ProgramRecorder>           m_deckBRecorder;
    std::unordered_map<NodeId, std::unique_ptr<ProgramRecorder>> m_sourceRecorders;

    QString  m_outputDir;
    NodeId   m_activeDeckA = 0;
    NodeId   m_activeDeckB = 0;
    QTimer  *m_progressTimer = nullptr;

    bool m_ndiEnabled = false;
    bool m_virtualCameraEnabled = false;

    // ── Background dispatch ───────────────────────────────────────────────────
    QThread        *m_dispatchThread = nullptr;
    QMutex          m_mailboxMutex;          // guards the pending-frame mailbox
    QWaitCondition  m_mailboxCv;
    QImage          m_pendingProgram, m_pendingDeckA, m_pendingDeckB;
    bool            m_frameDirty   = false;
    bool            m_dispatchStop = false;
    // Serialises all sink/recorder access between the dispatch thread and the
    // GUI thread (start/stop). Recursive so public helpers can be reused.
    QRecursiveMutex m_sinkMutex;
    // Lock-free fast path read by onProgramFrameReady() on the GUI thread.
    std::atomic<bool> m_needDeckReadback{false};
};
