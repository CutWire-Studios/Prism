#pragma once

#include <QObject>
#include <QStringList>
#include <functional>
#include "core/render/Compositor.h"

class QVariantAnimation;

/// Owns the crossfader value, transition mode/duration, and the animated AUTO/CUT sweep.
class TransitionController : public QObject {
    Q_OBJECT
public:
    explicit TransitionController(Compositor *compositor, QObject *parent = nullptr);
    ~TransitionController() override;

    int    faderValue()          const { return m_faderValue; }
    int    currentModeIndex()    const { return m_modeIndex; }
    double currentDurationSecs() const { return m_durationSecs; }
    bool   isRunning()           const { return m_animation || m_waitingForOut; }
    QStringList transitionModeNames() const;

    /// Manual fader move (clamped to 0..100); cancels any running sweep.
    void setFaderValue(int value);
    void setTransitionModeIndex(int index);
    void setTransitionDuration(double secs);

    /// Stop any running fader animation.
    void shutdown();

public slots:
    void autoTransition();
    void cutTransition();

signals:
    void faderValueChanged(int value);
    void modeChanged(int index);
    void durationChanged(double secs);
    void runningChanged(bool running);

private:
    /// Runs @p transition now, or once the outgoing deck's Out animations have played when the
    /// fader rests on an end stop. A second request during that wait is ignored.
    void startAfterOut(int currentVal, std::function<void()> transition);

    void runAutoTransition();
    void runCutTransition();
    void applyFader(int value);
    void stopAnimation();
    void emitRunningIfChanged();

    Compositor *m_compositor;
    int    m_faderValue   = 0;
    int    m_modeIndex    = 0;
    double m_durationSecs = 1.0;
    bool   m_waitingForOut = false;
    bool   m_lastRunning   = false;
    QVariantAnimation *m_animation = nullptr;
};
