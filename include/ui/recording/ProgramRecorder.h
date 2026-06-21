#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwsContext;
struct AVFrame;
struct AVPacket;

/// Encodes the program compositor feed to disk via FFmpeg (H.264/MKV).
class ProgramRecorder : public QObject {
    Q_OBJECT

public:
    struct Marker {
        qint64  timeMs = 0;
        QString label;
    };

    explicit ProgramRecorder(QObject *parent = nullptr);
    ~ProgramRecorder() override;

    static QString defaultOutputDir();
    static QString defaultOutputPath();
    static QString makeOutputPath(const QString &dir, const QString &stem, const QString &suffix);

    static QJsonDocument buildMarkersJson(const QString &videoPath, const QVector<Marker> &markers,
                                          const QString &trackLabel, qint64 durationMs, int frameRate);

    bool isRecording() const { return m_recording; }
    QString outputPath() const { return m_outputPath; }
    QString markersPath() const { return m_markersPath; }
    QString trackLabel() const { return m_trackLabel; }
    qint64 recordingDurationMs() const;
    const QVector<Marker> &markers() const { return m_markers; }
    int capturedFrameCount() const { return static_cast<int>(m_frameIndex); }
    bool startRecording(const QString &outputPath, const QString &trackLabel = {},
                        bool writeMarkersOnStop = true);
    void stopRecording();

    void submitFrame(const QImage &frame);
    void addMarker(const QString &label);

signals:
    void recordingChanged(bool recording);
    void errorOccurred(const QString &message);

private:
    void cleanup();
    void writeMarkersFile() const;
    bool flushEncoder();

    bool     m_recording = false;
    bool     m_writeMarkersOnStop = true;
    QString  m_trackLabel;
    QString  m_outputPath;
    QString  m_markersPath;
    QVector<Marker> m_markers;
    QElapsedTimer   m_timer;
    qint64          m_lastDurationMs = 0;

    AVFormatContext *m_fmtCtx    = nullptr;
    AVCodecContext  *m_codecCtx  = nullptr;
    AVStream        *m_stream     = nullptr;
    SwsContext      *m_swsCtx     = nullptr;
    AVFrame         *m_yuvFrame   = nullptr;
    AVPacket        *m_packet     = nullptr;
    int64_t          m_frameIndex = 0;

    static constexpr int kWidth  = 1280;
    static constexpr int kHeight = 720;
    static constexpr int kFps    = 30;
};
