#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <QVector>
#include "core/media/AudioDecoder.h"
#include "core/media/AudioEffectChain.h"
#include "core/media/PcmResampler.h"
#include "ui/nodes/AudioEffects.h"

class QAudioSource;
class QIODevice;

/// Captures system/loopback audio from a selected playback device.
class AudioLoopbackCapture : public QObject {
public:
    using PcmTapFn = std::function<void(const QByteArray &)>;

    explicit AudioLoopbackCapture(QObject *parent = nullptr);
    ~AudioLoopbackCapture() override;

    bool start();
    void stop();
    bool isRunning() const;

    void setPlaybackDeviceId(const QString &deviceId) { m_playbackDeviceId = deviceId; }
    QString playbackDeviceId() const { return m_playbackDeviceId; }

    void setTargetOutputDeviceId(const QString &deviceId);
    QString targetOutputDeviceId() const { return m_targetOutputDeviceId; }

    void setVolumePercent(int volumePercent);
    void setMuted(bool muted) { m_muted = muted; }

    void setProgramRecordingTap(PcmTapFn tap) { m_programRecordingTap = std::move(tap); }

    void setAnalysisTap(PcmTapFn tap) { m_analysisTap = std::move(tap); }

    void setEffectChain(const QVector<AudioEffectRef> &effects);

private:
    void pullInput();
    void applyGain(QByteArray &pcmChunk) const;

    std::unique_ptr<QAudioSource> m_source;
    QIODevice *m_inputIODevice = nullptr;
    PcmResampler m_inputResampler;
    bool m_needsInputResample = false;
    QTimer m_pullTimer;
    QString m_playbackDeviceId;
    QString m_targetOutputDeviceId;
    int m_volumePercent = 100;
    bool m_muted = false;
    PcmTapFn m_programRecordingTap;
    PcmTapFn m_analysisTap;
    AudioEffectChain m_effectChain;
#if defined(Q_OS_LINUX)
    bool m_useGStreamer = false;
#elif defined(Q_OS_WIN)
    bool m_useWasapi = false;
#endif
};
