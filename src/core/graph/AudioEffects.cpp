#include "core/graph/AudioEffects.h"

#include "core/audio/AudioRack.h"

#include <cmath>
#include <algorithm>

namespace AudioEffects {

bool bypassed(const QJsonObject &p) {
    return p.value(QStringLiteral("bypass")).toBool(false);
}

constexpr int kLinearDialSteps = 1000;

double snapStep(double value, double step) {
    if (step <= 0.0)
        return value;
    return std::round(value / step) * step;
}

int linearToDial(double value, double minV, double maxV) {
    value = std::clamp(value, minV, maxV);
    const double t = (maxV > minV) ? (value - minV) / (maxV - minV) : 0.0;
    return static_cast<int>(std::lround(t * kLinearDialSteps));
}

double dialToLinear(int dial, double minV, double maxV, double step) {
    const double t = static_cast<double>(dial) / kLinearDialSteps;
    return snapStep(minV + t * (maxV - minV), step);
}

constexpr double kFreqDialMinHz = 20.0;
constexpr double kFreqDialMaxHz = 20000.0;
constexpr int kFreqDialSteps = 1000;

int freqToDial(double freq) {
    freq = std::clamp(freq, kFreqDialMinHz, kFreqDialMaxHz);
    const double t = std::log(freq / kFreqDialMinHz) / std::log(kFreqDialMaxHz / kFreqDialMinHz);
    return static_cast<int>(std::lround(t * kFreqDialSteps));
}

double dialToFreq(int dial) {
    const double t = static_cast<double>(dial) / kFreqDialSteps;
    return kFreqDialMinHz * std::pow(kFreqDialMaxHz / kFreqDialMinHz, t);
}

const QVector<AudioEffectDescriptor> &all() {
    static const QVector<AudioEffectDescriptor> registry = [] {
        AudioEffectDescriptor d;
        d.id = kAudioFxEffectId;
        d.name = QStringLiteral("Audio FX");
        d.menuLabel = QStringLiteral("Audio FX");
        d.defaultParams = prism::audiofx::toJson({});
        d.editLabel = QStringLiteral("Edit");
        d.dynamicLabel = [](const QJsonObject &p) {
            const size_t n = prism::audiofx::fromJson(p).items.size();
            return n == 0 ? QStringLiteral("Empty") : QStringLiteral("%1 pedals").arg(n);
        };
        return QVector<AudioEffectDescriptor>{d};
    }();
    return registry;
}

const AudioEffectDescriptor *byId(int id) {
    for (const AudioEffectDescriptor &d : all())
        if (d.id == id) return &d;
    return nullptr;
}

} // namespace AudioEffects
