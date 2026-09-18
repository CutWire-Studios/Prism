#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>

struct AVBufferRef;
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwsContext;
struct AVFrame;
struct AVPacket;

/// Encodes the program compositor feed to disk via FFmpeg (H.264/MKV), on a hardware encoder
/// where one is available and chosen, falling back to x264.
class ProgramRecorder : public QObject {
    Q_OBJECT

public:
    struct Marker {
        qint64  timeMs = 0;
        QString label;
    };

    struct EncoderOption {
        QString id;     // settings value: "x264", "nvenc", "qsv", "amf", "vaapi"
        QString label;
    };

    /// "auto" first, then the H.264 encoders that open on this machine.
    static QList<EncoderOption> availableEncoders();
    /// QSettings "recording/videoEncoder"; "auto" when unset.
    static QString encoderSetting();
    static void setEncoderSetting(const QString &id);

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
    /// The encoder the current (or last) recording ended up on.
    QString encoderLabel() const { return m_encoderLabel; }
    bool startRecording(const QString &outputPath, const QString &trackLabel = {},
                        bool writeMarkersOnStop = true,
                        int width = 1280, int height = 720);
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
    bool openPipeline();
    bool openEncoder(int encoderIndex);
    bool sendFrame(AVFrame *frame);

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
    AVBufferRef     *m_hwDevice   = nullptr;
    AVFrame         *m_hwFrame    = nullptr;   // upload target when the encoder takes GPU frames
    QList<int>       m_candidates;             // indexes into the encoder table, in try order
    int              m_candidate  = -1;
    QString          m_encoderLabel;
    int64_t          m_frameIndex = 0;
    int64_t          m_lastPts    = -1;   // wall-clock PTS guard (monotonic)

    int m_width  = 1280;
    int m_height = 720;
    static constexpr int kFps = 30;
};
