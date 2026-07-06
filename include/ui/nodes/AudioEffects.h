#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>

class QWidget;

/// One audio effect in the resolved stream chain (upstream → downstream).
struct AudioEffectRef {
    int         effectId = -1;
    QJsonObject params;

    bool operator==(const AudioEffectRef &o) const {
        return effectId == o.effectId && params == o.params;
    }
};

/// Called while an effect edit dialog is open so audio can track control changes.
using AudioEffectLiveUpdate = std::function<void(const QJsonObject &)>;

struct AudioEffectDescriptor {
    int     id = -1;
    QString name;
    QString menuLabel;
    bool    available = true;
    QJsonObject defaultParams;

    /// FFmpeg libavfilter fragment for this effect (empty = bypass at build time).
    std::function<QString(const QJsonObject &params)> filterSpec;

    QString editLabel;
    std::function<bool(QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &onLiveChange)> editDialog;
    std::function<QString(const QJsonObject &params)> dynamicLabel;
};

namespace AudioEffects {

const QVector<AudioEffectDescriptor> &all();
const AudioEffectDescriptor *byId(int id);

/// Ordered FFmpeg filter chain for the given effect refs (skips unknown / bypassed).
QString buildFilterChain(const QVector<AudioEffectRef> &effects);

/// Identity string for comparing effect chains.
QString effectsKey(const QVector<AudioEffectRef> &effects);

} // namespace AudioEffects
