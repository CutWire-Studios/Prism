#pragma once

#include "core/audio/AudioRack.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Model behind AudioEffectEditor: wraps an AudioRack and publishes it as plain maps. Knob and
// slider edits go through the set* calls (no rebuild, `edited` only); structural edits emit
// `boardChanged`. While bound to a node every edit is applied to the graph live, and commit()
// or cancel() settle it.
class PedalboardModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// {items: [...], modulators: [...], sources: [{id, label}]}
    Q_PROPERTY(QVariantMap board READ board NOTIFY boardChanged)
    /// Palette rows {group, label, kind, arg}; kind is pedal | parallel | bands | mod.
    Q_PROPERTY(QVariantList palette READ palette CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString targetSplit READ targetSplit NOTIFY targetChanged)
    Q_PROPERTY(int targetLane READ targetLane NOTIFY targetChanged)

public:
    explicit PedalboardModel(QObject *parent = nullptr);

    QVariantMap board() const { return m_board; }
    QVariantList palette() const { return m_palette; }
    QString status() const { return m_status; }
    QString targetSplit() const { return m_selSplit; }
    int targetLane() const { return m_selLane; }

    /// Loads the node's effect params and starts applying edits to it live.
    Q_INVOKABLE void bind(quint64 nodeId);
    /// Keeps the edits as one undo step.
    Q_INVOKABLE void commit();
    /// Puts the node back to the params it had at bind().
    Q_INVOKABLE void cancel();

    Q_INVOKABLE void setTarget(const QString &split, int lane);
    /// kind/arg from a palette row; irPath is only read for the convolution pedal.
    Q_INVOKABLE bool addFromPalette(const QString &kind, const QString &arg, const QString &irPath = {});
    Q_INVOKABLE bool addLane(const QString &splitId);
    Q_INVOKABLE bool removeLane(const QString &splitId, int lane);
    Q_INVOKABLE bool removeItem(const QString &id);
    Q_INVOKABLE bool moveItem(const QString &id, const QString &split, int lane, int index);
    Q_INVOKABLE bool removeModulator(const QString &id);
    Q_INVOKABLE bool addRoute(const QString &from, const QString &pedal, const QString &knob);
    Q_INVOKABLE bool removeRoute(const QString &routeId);

    Q_INVOKABLE void setPedalKnob(const QString &id, const QString &knob, double value);
    Q_INVOKABLE void setPedalBypass(const QString &id, bool bypass);
    Q_INVOKABLE void setPedalIr(const QString &id, const QString &path);
    Q_INVOKABLE void setSplitBlend(const QString &id, double value);
    Q_INVOKABLE void setSplitCrossfade(const QString &id, bool on);
    Q_INVOKABLE void setCrossover(const QString &id, int index, double hz);
    Q_INVOKABLE void setLaneGain(const QString &id, int lane, double gain);
    Q_INVOKABLE void setModKnob(const QString &id, const QString &knob, double value);
    Q_INVOKABLE void setStep(const QString &id, int step, double value);
    Q_INVOKABLE void setModSource(const QString &id, const QString &source);
    Q_INVOKABLE void setRouteDepth(const QString &routeId, double depth);

signals:
    void boardChanged();
    void statusChanged();
    void targetChanged();
    void edited();

private:
    void structural();
    bool applyOp(bool ok);
    void setStatus(const QString &text);
    void rebuild();
    void applyLive();
    prism::audiofx::RackSlot targetSlot() const;

    prism::audiofx::AudioRack m_rack;
    QVariantMap m_board;
    QVariantList m_palette;
    QString m_status;
    QString m_selSplit;
    int m_selLane = 0;
    quint64 m_node = 0;
    QVariantMap m_original;
};
