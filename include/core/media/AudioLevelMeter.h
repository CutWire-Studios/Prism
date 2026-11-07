#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QVariantMap>
#include <atomic>
#include <memory>

class ClipGraph;

/// Peak meter for one audio-carrying node. The audio side only does a lock-free max into an
/// atomic; the UI thread reads and decays it.
class AudioLevelMeter {
public:
    static constexpr float kDecayPerSecond = 1.5f;

    /// Interleaved float PCM scaled by @p gain.
    void feed(const QByteArray &pcm, float gain = 1.0f);
    void feed(const float *samples, int count, float gain = 1.0f);

    /// New peak since the last call, falling by kDecayPerSecond from the previous reading.
    float poll(float dtSeconds);

private:
    std::atomic<float> m_peak{0.0f};
    float m_shown = 0.0f;
};

/// Every node's meter. Source nodes (clips, mics, captures) own a tap; effects, mixers and
/// outputs read the loudest signal feeding them.
class AudioMeterBank {
public:
    std::shared_ptr<AudioLevelMeter> meter(quint64 nodeId);

    /// Polls the taps, derives the downstream nodes of @p graphs and returns the display levels
    /// (0..1 on a -60..0 dB scale) keyed by node id, for non-zero nodes only.
    QHash<quint64, float> poll(float dtSeconds, const QList<const ClipGraph *> &graphs);

    static float toDisplay(float linearPeak);

private:
    QHash<quint64, std::shared_ptr<AudioLevelMeter>> m_meters;
};
