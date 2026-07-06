#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <QVector>
#include "core/media/AudioDecoder.h"
#include "core/media/AudioEffectChain.h"
#include "ui/nodes/AudioEffects.h"

class QAudioSource;
class QIODevice;

/// Captures live microphone PCM and mixes it into the matching master output device.
class AudioInputCapture : public QObject {
public:
    using PcmTapFn = std::function<void(const QByteArray &)>;

    explicit AudioInputCapture(QObject *parent = nullptr);
    ~AudioInputCapture() override;

    bool start();
    void stop();
    bool isRunning() const;

    void setInputDeviceId(const QString &deviceId) { m_inputDeviceId = deviceId; }
    QString inputDeviceId() const { return m_inputDeviceId; }

    void setTargetOutputDeviceId(const QString &deviceId);
    QString targetOutputDeviceId() const { return m_targetOutputDeviceId; }

    void setVolumePercent(int volumePercent);
    void setMuted(bool muted) { m_muted = muted; }

    void setProgramRecordingTap(PcmTapFn tap) { m_programRecordingTap = std::move(tap); }

    void setEffectChain(const QVector<AudioEffectRef> &effects);

private:
    void pullInput();
    void applyGain(QByteArray &pcmChunk) const;

    std::unique_ptr<QAudioSource> m_source;
    QIODevice *m_inputIODevice = nullptr;
    QTimer m_pullTimer;
    QString m_inputDeviceId;
    QString m_targetOutputDeviceId;
    int m_volumePercent = 100;
    bool m_muted = false;
    PcmTapFn m_programRecordingTap;
    AudioEffectChain m_effectChain;
};
