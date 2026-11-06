#pragma once

#include <QObject>
#include <QStringList>
#include <QElapsedTimer>
#include <QMetaObject>
#include <functional>
#include "core/render/Compositor.h"

/// Owns the transition progress (0 = Live fully on air, 100 = Staging fully on air), transition
/// mode/duration, and the animated take / cut. Reaching 100 swaps the Live and Staging slot contents
/// and resets the progress to 0 in the same frame.
class TransitionController : public QObject {
    Q_OBJECT
public:
    explicit TransitionController(Compositor *compositor, QObject *parent = nullptr);
    ~TransitionController() override;

    /// Transition progress from Live toward Staging, 0..100.
    int    faderValue()          const { return m_progress; }
    /// Exact transition progress, 0..1; advanced every compositor frame during a take.
    float  progress()            const { return m_progressF; }
    /// InOutQuad-eased position of a take @p elapsedMs into a @p durationMs sweep, 0..1.
    static float easedProgress(double elapsedMs, double durationMs);
    int    currentModeIndex()    const { return m_modeIndex; }
    double currentDurationSecs() const { return m_durationSecs; }
    bool   isRunning()           const { return m_animating || m_waitingForOut; }
    QStringList transitionModeNames() const;

    /// Manual fader move (clamped to 0..100); cancels any running sweep. Reaching 100 commits a take.
    void setFaderValue(int value);
    void setTransitionModeIndex(int index);
    void setTransitionDuration(double secs);

    /// Stop any running fader animation.
    void shutdown();

public slots:
    /// Take: animates Live -> Staging with the configured mode and duration. No-op when Staging is empty.
    void autoTransition();
    /// Cut: switches to Staging immediately. No-op when Staging is empty.
    void cutTransition();

signals:
    void faderValueChanged(int value);
    /// Compositor crossfade (0 = Live, 1 = Staging) after every progress step, for audio gains.
    void crossfadeChanged(float mix);
    /// The Staging deck should start playing; progress just left 0.
    void takeStarted();
    /// The compositor slots were swapped and progress reset to 0; the outgoing content now sits in
    /// Staging. Listeners swap the remaining per-slot state and park Staging.
    void takeCommitted();
    // Progress returned to 0 without committing (e.g. slider released early).
    void takeCancelled();
    void modeChanged(int index);
    void durationChanged(double secs);
    void runningChanged(bool running);

private:
    /// Runs @p transition now, or once the outgoing deck's Out animations have played when the
    /// fader rests on an end stop. A second request during that wait is ignored.
    void startAfterOut(std::function<void()> transition);

    bool stagingEmpty() const;
    void runAutoTransition();
    void applyProgress(float value);
    void onFrameTick();
    void commitTake();
    void stopAnimation();
    void emitRunningIfChanged();

    Compositor *m_compositor;
    int    m_progress     = 0;
    float  m_progressF    = 0.f;
    bool   m_incomingStarted = false;
    int    m_modeIndex    = 0;
    double m_durationSecs = 1.0;
    bool   m_waitingForOut = false;
    bool   m_lastRunning   = false;
    bool   m_animating    = false;
    float  m_sweepStart   = 0.f;
    QElapsedTimer m_sweepClock;
    QMetaObject::Connection m_tickConnection;
};
