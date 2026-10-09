#include "core/media/AudioEffectChain.h"

#include "core/audio/dsp/GraphProcessor.h"

#include <QDebug>

#include <algorithm>

using prism::audiofx::GraphProcessor;
using prism::audiofx::LiveValue;

struct AudioEffectChain::Stage {
    int nodeId = -1;
    QString signature;
    std::unique_ptr<GraphProcessor> processor;
    QVector<LiveValue> lastValues;
};

AudioEffectChain::AudioEffectChain() = default;

AudioEffectChain::~AudioEffectChain() = default;

namespace {

void pushValue(GraphProcessor &gp, const prism::audiofx::AudioRack &rack, const LiveValue &v) {
    using Kind = LiveValue::Kind;
    const std::string node = v.node.toStdString();
    switch (v.kind) {
    case Kind::Knob:
        gp.setKnob(gp.nodeIndex(node), v.index, v.value);
        break;
    case Kind::Bypass:
        gp.setBypass(gp.nodeIndex(node), v.value >= 0.5f);
        break;
    case Kind::RouteDepth: {
        if (v.mod < 0 || v.mod >= rack.modulators.size()) break;
        gp.setRouteDepth(gp.nodeIndex(node), v.index, gp.modulatorIndex(rack.modulators[v.mod].id.toStdString()), v.value);
        break;
    }
    case Kind::LaneGain:
        gp.setLaneGain(gp.nodeIndex(node), v.index, v.value);
        break;
    case Kind::ModKnob:
        gp.setModulatorKnob(gp.modulatorIndex(node), v.index, v.value);
        break;
    case Kind::Step:
        gp.setModulatorStep(gp.modulatorIndex(node), v.index, v.value);
        break;
    }
}

bool sameSlot(const LiveValue &a, const LiveValue &b) {
    return a.kind == b.kind && a.node == b.node && a.index == b.index && a.mod == b.mod;
}

} // namespace

void AudioEffectChain::setEffects(const QVector<AudioEffectRef> &effects) {
    m_effects = effects;

    std::vector<std::pair<int, QString>> wanted;
    for (const AudioEffectRef &ref : effects) {
        if (!ref.rack.empty())
            wanted.emplace_back(ref.nodeId, prism::audiofx::shapeSignature(ref.rack));
    }

    bool same = wanted.size() == m_stages.size();
    for (size_t i = 0; same && i < wanted.size(); ++i)
        same = m_stages[i]->nodeId == wanted[i].first && m_stages[i]->signature == wanted[i].second;

    if (!same) {
        std::vector<std::unique_ptr<Stage>> stages;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = kSampleRate;
        spec.maximumBlockSize = kFrameSamples;
        spec.numChannels = kChannels;

        for (const AudioEffectRef &ref : effects) {
            if (ref.rack.empty())
                continue;
            QString error;
            auto graph = prism::audiofx::buildGraph(ref.rack, &error);
            if (!graph) {
                qWarning() << "AudioEffectChain: unable to build rack:" << error;
                continue;
            }
            auto stage = std::make_unique<Stage>();
            stage->nodeId = ref.nodeId;
            stage->signature = prism::audiofx::shapeSignature(ref.rack);
            stage->processor = std::make_unique<GraphProcessor>(std::move(graph));
            stage->processor->prepare(spec);
            stage->processor->reset();
            stage->lastValues = prism::audiofx::liveValues(ref.rack);
            stages.push_back(std::move(stage));
        }
        m_stages = std::move(stages);
        return;
    }

    size_t s = 0;
    for (const AudioEffectRef &ref : effects) {
        if (ref.rack.empty())
            continue;
        Stage &stage = *m_stages[s++];
        const QVector<LiveValue> values = prism::audiofx::liveValues(ref.rack);
        for (int i = 0; i < values.size(); ++i) {
            const bool known = i < stage.lastValues.size() && sameSlot(values[i], stage.lastValues[i]);
            if (known && values[i].value == stage.lastValues[i].value)
                continue;
            pushValue(*stage.processor, ref.rack, values[i]);
        }
        stage.lastValues = values;
    }
}

void AudioEffectChain::reset() {
    for (auto &stage : m_stages)
        stage->processor->reset();
}

bool AudioEffectChain::process(const QByteArray &in, QByteArray &out) {
    out.clear();
    if (in.isEmpty())
        return false;

    const int frameBytes = kChannels * static_cast<int>(sizeof(float));
    if (in.size() % frameBytes != 0)
        return false;

    out = in;
    if (m_stages.empty())
        return true;

    const int frames = in.size() / frameBytes;
    const float *src = reinterpret_cast<const float *>(in.constData());
    float *dst = reinterpret_cast<float *>(out.data());
    float *left = m_left.data();
    float *right = m_right.data();
    float *channels[kChannels] = {left, right};

    for (int offset = 0; offset < frames; offset += kFrameSamples) {
        const int count = std::min(kFrameSamples, frames - offset);
        for (int i = 0; i < count; ++i) {
            left[i] = src[(offset + i) * 2];
            right[i] = src[(offset + i) * 2 + 1];
        }

        juce::dsp::AudioBlock<float> block(channels, kChannels, 0, static_cast<size_t>(count));
        for (auto &stage : m_stages)
            stage->processor->process(block);

        for (int i = 0; i < count; ++i) {
            dst[(offset + i) * 2] = left[i];
            dst[(offset + i) * 2 + 1] = right[i];
        }
    }
    return true;
}
