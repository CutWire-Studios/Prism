#include "core/media/AudioLevelMeter.h"

#include "core/graph/ClipGraph.h"

#include <algorithm>
#include <cmath>

void AudioLevelMeter::feed(const QByteArray &pcm, float gain)
{
    feed(reinterpret_cast<const float *>(pcm.constData()), int(pcm.size() / sizeof(float)), gain);
}

void AudioLevelMeter::feed(const float *samples, int count, float gain)
{
    float peak = 0.0f;
    for (int i = 0; i < count; ++i)
        peak = std::max(peak, std::fabs(samples[i]));
    peak *= gain;
    float current = m_peak.load(std::memory_order_relaxed);
    while (peak > current && !m_peak.compare_exchange_weak(current, peak, std::memory_order_relaxed)) {
    }
}

float AudioLevelMeter::poll(float dtSeconds)
{
    const float fresh = m_peak.exchange(0.0f, std::memory_order_relaxed);
    m_shown = std::max(fresh, m_shown - kDecayPerSecond * dtSeconds);
    return m_shown;
}

std::shared_ptr<AudioLevelMeter> AudioMeterBank::meter(quint64 nodeId)
{
    auto &m = m_meters[nodeId];
    if (!m)
        m = std::make_shared<AudioLevelMeter>();
    return m;
}

float AudioMeterBank::toDisplay(float linearPeak)
{
    if (linearPeak <= 0.001f)
        return 0.0f;
    const float db = 20.0f * std::log10(linearPeak);
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

QHash<quint64, float> AudioMeterBank::poll(float dtSeconds, const QList<const ClipGraph *> &graphs)
{
    using NT = ClipGraph::NodeType;
    QHash<quint64, float> level;
    for (auto it = m_meters.begin(); it != m_meters.end();) {
        const float v = it.value()->poll(dtSeconds);
        if (v > 0.0f)
            level.insert(it.key(), v);
        if (v <= 0.0f && it.value().use_count() == 1)
            it = m_meters.erase(it);
        else
            ++it;
    }

    for (const ClipGraph *g : graphs) {
        QList<quint64> derived;
        for (quint64 id : g->nodeIds()) {
            const ClipGraph::Node *n = g->node(id);
            if (n->type == NT::AudioEffect || n->type == NT::AudioMixer || n->type == NT::MasterAudioOutput)
                derived.append(id);
        }
        for (int pass = 0; pass < derived.size(); ++pass) {
            bool changed = false;
            for (const ClipGraph::Edge &e : g->edges()) {
                if (!derived.contains(e.to))
                    continue;
                const float in = level.value(e.from, 0.0f);
                if (in > level.value(e.to, 0.0f)) {
                    level[e.to] = in;
                    changed = true;
                }
            }
            if (!changed)
                break;
        }
    }
    return level;
}
