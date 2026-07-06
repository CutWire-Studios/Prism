#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>
#include <functional>
#include <QVector>
#include "core/media/AudioDecoder.h"
#include "core/media/AudioEffectChain.h"
#include "ui/nodes/AudioEffects.h"

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
    void setCrossfadeFactor(float factor) { m_crossfadeFactor = factor; }
    void setMuted(bool muted);
    /// Playback rate (time-stretched, pitch preserved). 1.0 = normal.
    void setSpeed(double speed) { m_decoder.setPlaybackSpeed(speed); }

    bool isPlaying() const;
    QString currentFilePath() const { return m_currentFilePath; }

    void setDelayMs(int delayMs) { m_delayMs = delayMs; }
    int delayMs() const { return m_delayMs; }

    /// Select the output device by QAudioDevice::id() (empty = system default).
    /// Takes effect on the next start(); restart playback to switch live.
    void setOutputDeviceId(const QString &deviceId) { m_deviceId = deviceId; }
    QString outputDeviceId() const { return m_deviceId; }

    void setEffectChain(const QVector<AudioEffectRef> &effects);

    using PcmTapFn = std::function<void(const QByteArray &)>;
    /// Post-crossfade PCM (for program audio mix recording).
    void setPcmTap(PcmTapFn tap) { m_pcmTap = std::move(tap); }
    /// Pre-crossfade PCM at clip volume (for deck/clip iso recording).
    void setIsoPcmTap(PcmTapFn tap) { m_isoPcmTap = std::move(tap); }

private slots:
    void pushAudio();

private:
    void applyGain(QByteArray &pcmChunk, float crossfadeFactor) const;

    AudioDecoder m_decoder;
    AudioEffectChain m_effectChain;
    std::unique_ptr<QAudioSink> m_sink;
    QIODevice *m_outputDevice = nullptr;
    QTimer m_pushTimer;
    QString m_currentFilePath;
    QByteArray m_residualBuffer;
    int m_volumePercent = 100;
    float m_crossfadeFactor = 1.0f;
    bool m_muted = false;
    int m_delayMs = 0;
    QString m_deviceId;   // empty = system default output
    qint64 m_silenceBytesPending = 0;
    PcmTapFn m_pcmTap;
    PcmTapFn m_isoPcmTap;
};
