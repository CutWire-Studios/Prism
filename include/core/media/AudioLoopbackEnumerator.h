#pragma once

#include <QAudioDevice>
#include <QList>
#include <QString>

struct AudioOutputDeviceInfo {
    QString id;
    QString label;
    bool    isDefault = false;
};

namespace AudioLoopbackEnumerator {

/// Lists playback devices the user can choose to capture system audio from.
QList<AudioOutputDeviceInfo> listPlaybackDevices();

/// Resolves the loopback/monitor input device for a chosen playback device.
QAudioDevice monitorForPlaybackDevice(const QString &outputDeviceId);

/// PulseAudio/PipeWire monitor source id derived from a playback device id.
QString monitorSourceIdForPlayback(const QString &outputDeviceId);

/// Raw output device id for a chosen playback device, with no ".monitor"
/// suffix — used as the PipeWire target-object on Linux, since PipeWire (as
/// opposed to PulseAudio) has no separate monitor-source object: you target
/// the sink node itself and request its monitor via a stream property.
QString sinkNodeIdForPlayback(const QString &outputDeviceId);

/// Human-readable monitor source label for UI hints (empty if unresolved).
QString monitorLabelForPlaybackDevice(const QString &outputDeviceId);

/// RMS level of float32 interleaved PCM (0 = silence).
float pcmRms(const QByteArray &pcm);

/// True when PCM chunk is above the noise-gate threshold.
bool pcmHasSignal(const QByteArray &pcm, float threshold = 0.01f);

/// Hysteresis noise gate for live loopback (avoids monitor hiss keeping shaders hot).
class LiveAudioGate {
public:
    bool accept(float rms, qint64 nowMs);

    bool isOpen() const { return m_open; }

private:
    bool   m_open = false;
    qint64 m_belowSinceMs = 0;

    static constexpr float kOpenThreshold  = 0.012f;
    static constexpr float kCloseThreshold = 0.006f;
    static constexpr qint64 kCloseHoldMs   = 120;
};

} // namespace AudioLoopbackEnumerator
