#pragma once

#include "core/audio/AudioRack.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>

class QDial;
class QHBoxLayout;
class QLCDNumber;
class QVBoxLayout;
class QWidget;

/// One Audio FX node in the resolved stream chain (upstream → downstream).
struct AudioEffectRef {
    int nodeId = -1;
    prism::audiofx::AudioRack rack;
};

/// Called while an effect edit dialog is open so audio can track control changes.
using AudioEffectLiveUpdate = std::function<void(const QJsonObject &)>;

struct AudioEffectDescriptor {
    int     id = -1;
    QString name;
    QString menuLabel;
    bool    available = true;
    QJsonObject defaultParams;

    QString editLabel;
    std::function<bool(QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &onLiveChange)> editDialog;
    std::function<QString(const QJsonObject &params)> dynamicLabel;
};

namespace AudioEffects {

constexpr int kAudioFxEffectId = 100;

const QVector<AudioEffectDescriptor> &all();
const AudioEffectDescriptor *byId(int id);

bool runAudioEffectDialog(QWidget *parent, const QString &title,
                          const std::function<void(QVBoxLayout *)> &build);
bool bypassed(const QJsonObject &p);
double snapStep(double value, double step);
int linearToDial(double value, double minV, double maxV);
double dialToLinear(int dial, double minV, double maxV, double step = 0.0);
void updateLcd(QLCDNumber *lcd, double value, int precision);
void wireDial(QDial *dial, QLCDNumber *lcd, double &value, double minV, double maxV, double step,
              int precision, const std::function<void()> &notify);
void addDialColumn(QHBoxLayout *row, const QString &title, double &value, double minV, double maxV,
                   double step, const QString &unit, int dialSize, int precision,
                   const std::function<void()> &notify);
void addPrimaryDial(QVBoxLayout *layout, double &value, double minV, double maxV, double step,
                    const QString &unit, int precision, const std::function<void()> &notify);
void addBypassCheckbox(QVBoxLayout *layout, bool &bypass, const std::function<void()> &notify);
int freqToDial(double freq);
double dialToFreq(int dial);
void addFreqDial(QVBoxLayout *layout, double &freq, const std::function<void()> &notify);

} // namespace AudioEffects
