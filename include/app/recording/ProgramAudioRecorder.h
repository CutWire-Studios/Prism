#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QVector>
#include <QElapsedTimer>
#include <QJsonDocument>

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwrContext;
struct AVFrame;
struct AVPacket;
struct AVAudioFifo;

/// Encodes the program audio mix to disk as FLAC (44.1 kHz stereo).
/// Intended to pair with a separate video recording file for sync in post.
class ProgramAudioRecorder : public QObject {
    Q_OBJECT

public:
    struct Marker {
        qint64  timeMs = 0;
        QString label;
    };

    explicit ProgramAudioRecorder(QObject *parent = nullptr);
    ~ProgramAudioRecorder() override;

    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels   = 2;

    static QString makeOutputPath(const QString &dir, const QString &stem, const QString &suffix);

    static QJsonDocument buildMarkersJson(const QString &audioPath, const QVector<Marker> &markers,
                                          const QString &trackLabel, qint64 durationMs);

    bool isRecording() const { return m_recording; }
    QString outputPath() const { return m_outputPath; }
    QString markersPath() const { return m_markersPath; }
    QString trackLabel() const { return m_trackLabel; }
    qint64 recordingDurationMs() const;
    const QVector<Marker> &markers() const { return m_markers; }
    int capturedSampleCount() const { return static_cast<int>(m_sampleIndex); }

    bool startRecording(const QString &outputPath, const QString &trackLabel = {},
                        bool writeMarkersOnStop = true);
    void stopRecording();

    /// Mixes deck A/B PCM (interleaved float32 stereo @ 44.1 kHz).
    void submitDeckChunk(int deckIndex, const QByteArray &pcm);

    /// Writes a single PCM stream (deck/clip iso recording).
    void submitPcm(const QByteArray &pcm);

    /// Adds auxiliary live input (microphone) to the program mix recording.
    void submitMicChunk(const QByteArray &pcm);

    void addMarker(const QString &label);

signals:
    void recordingChanged(bool recording);
    void errorOccurred(const QString &message);

private:
    void cleanup();
    void writeMarkersFile() const;
    bool flushEncoder();
    void drainMixQueues();
    void drainPcmQueue(bool flushPartial);
    void encodeMixedBlock(const float *samples, int sampleCount);
    void drainFifo(bool flushPartial);
    void sendFrame(int sampleCount);

    bool     m_recording = false;
    bool     m_writeMarkersOnStop = true;
    QString  m_trackLabel;
    QString  m_outputPath;
    QString  m_markersPath;
    QVector<Marker> m_markers;
    QElapsedTimer   m_timer;
    qint64          m_lastDurationMs = 0;

    QByteArray m_deckQueueA;
    QByteArray m_deckQueueB;
    QByteArray m_micQueue;
    QByteArray m_pcmQueue;

    AVFormatContext *m_fmtCtx   = nullptr;
    AVCodecContext  *m_codecCtx = nullptr;
    AVStream        *m_stream   = nullptr;
    SwrContext      *m_swrCtx   = nullptr;
    AVFrame         *m_pcmFrame = nullptr;
    AVPacket        *m_packet   = nullptr;
    AVAudioFifo     *m_fifo     = nullptr;
    int              m_frameSize = 0;
    int64_t          m_sampleIndex = 0;

    static constexpr int kMixBlockSamples = 1024;
};
