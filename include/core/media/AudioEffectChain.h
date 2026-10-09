#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include "core/audio/AudioRack.h"
#include "ui/nodes/AudioEffects.h"

#include <memory>
#include <vector>

/// Real-time PCM processor: 44.1 kHz stereo float in/out, one pedalboard graph per Audio FX node.
class AudioEffectChain {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels = 2;
    static constexpr int kFrameSamples = 1024;

    AudioEffectChain();
    ~AudioEffectChain();

    void setEffects(const QVector<AudioEffectRef> &effects);
    const QVector<AudioEffectRef> &effects() const { return m_effects; }

    /// Clear delay/reverb tails and modulator phases (call on seek).
    void reset();

    /// Processes one PCM chunk; output is always the same size as the input.
    bool process(const QByteArray &in, QByteArray &out);

    bool hasFilters() const { return !m_stages.empty(); }

private:
    struct Stage;

    QVector<AudioEffectRef> m_effects;
    std::vector<std::unique_ptr<Stage>> m_stages;
    std::vector<float> m_left = std::vector<float>(kFrameSamples);
    std::vector<float> m_right = std::vector<float>(kFrameSamples);
};
