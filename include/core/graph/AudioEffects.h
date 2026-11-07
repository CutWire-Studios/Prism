#pragma once

#include "core/audio/AudioRack.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>

/// One Audio FX node in the resolved stream chain (upstream → downstream).
struct AudioEffectRef {
    int nodeId = -1;
    prism::audiofx::AudioRack rack;
    float wet = 1.0f;   // 0 = dry signal only, 1 = fully processed
};

struct AudioEffectDescriptor {
    int     id = -1;
    QString name;
    QString menuLabel;
    bool    available = true;
    QJsonObject defaultParams;

    QString editLabel;
    std::function<QString(const QJsonObject &params)> dynamicLabel;
};

namespace AudioEffects {

constexpr int kAudioFxEffectId = 100;

const QVector<AudioEffectDescriptor> &all();
const AudioEffectDescriptor *byId(int id);

bool bypassed(const QJsonObject &p);
double snapStep(double value, double step);
int linearToDial(double value, double minV, double maxV);
double dialToLinear(int dial, double minV, double maxV, double step = 0.0);
int freqToDial(double freq);
double dialToFreq(int dial);

} // namespace AudioEffects
