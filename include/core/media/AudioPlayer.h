#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>
#include "core/media/AudioDecoder.h"

class QAudioSink;
class QIODevice;

/// Plays a media file's audio track through a QAudioSink, with volume, mute and
/// a playback delay (for A/V sync). Pulls PCM from an owned AudioDecoder.
class AudioPlayer : public QObject {
    Q_OBJECT

public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    bool start(const QString &filePath, double startTimeSeconds = 0.0);
    void stop();
    void pause();
    void resume();
    bool seek(double seconds);

    void setVolumePercent(int volumePercent);
    void setMuted(bool muted);

    bool isPlaying() const;
    QString currentFilePath() const { return m_currentFilePath; }

    void setDelayMs(int delayMs) { m_delayMs = delayMs; }
    int delayMs() const { return m_delayMs; }

private slots:
    void pushAudio();

private:
    void applyGain(QByteArray &pcmChunk) const;

    AudioDecoder m_decoder;
    std::unique_ptr<QAudioSink> m_sink;
    QIODevice *m_outputDevice = nullptr;
    QTimer m_pushTimer;
    QString m_currentFilePath;
    QByteArray m_residualBuffer;
    int m_volumePercent = 100;
    bool m_muted = false;
    int m_delayMs = 0;
    qint64 m_silenceBytesPending = 0;
};
