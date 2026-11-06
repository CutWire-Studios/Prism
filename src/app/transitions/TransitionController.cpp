#include "app/transitions/TransitionController.h"
#include <QEasingCurve>
#include <QTimer>
#include <algorithm>

TransitionController::TransitionController(Compositor *compositor, QObject *parent)
    : QObject(parent)
    , m_compositor(compositor)
{
}

TransitionController::~TransitionController() {
    shutdown();
}

void TransitionController::shutdown() {
    m_waitingForOut = false;
    stopAnimation();
    emitRunningIfChanged();
}

void TransitionController::stopAnimation() {
    m_animating = false;
    QObject::disconnect(m_tickConnection);
}

float TransitionController::easedProgress(double elapsedMs, double durationMs) {
    if (durationMs <= 0.0 || elapsedMs >= durationMs)
        return 1.f;
    if (elapsedMs <= 0.0)
        return 0.f;
    return float(QEasingCurve(QEasingCurve::InOutQuad).valueForProgress(elapsedMs / durationMs));
}

void TransitionController::emitRunningIfChanged() {
    const bool running = isRunning();
    if (running == m_lastRunning)
        return;
    m_lastRunning = running;
    emit runningChanged(running);
}

bool TransitionController::stagingEmpty() const {
    return !m_compositor->sourceStaging();
}

void TransitionController::applyProgress(float value) {
    value = std::clamp(value, 0.f, 1.f);
    if (value == m_progressF)
        return;
    if (m_progressF <= 0.f && !m_incomingStarted) {
        m_incomingStarted = true;
        emit takeStarted();
    }
    m_progressF = value;
    const int previous = m_progress;
    m_progress = value >= 1.f ? 100 : std::min(99, qRound(value * 100.f));
    const bool cancelled = value <= 0.f && m_incomingStarted;
    if (value <= 0.f)
        m_incomingStarted = false;
    m_compositor->setCrossfade(value);
    emit crossfadeChanged(value);
    if (m_progress != previous)
        emit faderValueChanged(m_progress);
    if (value >= 1.f)
        commitTake();
    else if (cancelled)
        emit takeCancelled();
}

void TransitionController::onFrameTick() {
    if (!m_animating)
        return;
    const double durationMs = m_durationSecs * 1000.0;
    const double elapsed = double(m_sweepClock.nsecsElapsed()) / 1e6;
    if (elapsed >= durationMs) {
        stopAnimation();
        applyProgress(1.f);
        emitRunningIfChanged();
        return;
    }
    applyProgress(m_sweepStart + (1.f - m_sweepStart) * easedProgress(elapsed, durationMs));
}

void TransitionController::commitTake() {
    m_compositor->swapDeckContents();
    m_compositor->setCrossfade(0.f);
    m_progress = 0;
    m_progressF = 0.f;
    m_incomingStarted = false;
    emit takeCommitted();
    emit crossfadeChanged(0.f);
    emit faderValueChanged(0);
}

void TransitionController::setFaderValue(int value) {
    stopAnimation();
    applyProgress(std::clamp(value, 0, 100) / 100.f);
    emitRunningIfChanged();
}

void TransitionController::setTransitionModeIndex(int index) {
    static const Compositor::TransitionMode modes[] = {
        Compositor::TransitionMode::Crossfade, Compositor::TransitionMode::Cut,
        Compositor::TransitionMode::WipeLeft, Compositor::TransitionMode::WipeRight,
        Compositor::TransitionMode::WipeUp, Compositor::TransitionMode::WipeDown,
        Compositor::TransitionMode::SlideLeft, Compositor::TransitionMode::SlideRight,
        Compositor::TransitionMode::SlideUp, Compositor::TransitionMode::SlideDown,
        Compositor::TransitionMode::DipToBlack, Compositor::TransitionMode::DipToWhite,
        Compositor::TransitionMode::Additive, Compositor::TransitionMode::CrossZoom,
        Compositor::TransitionMode::SplitDoor, Compositor::TransitionMode::SplitDoorVert,
        Compositor::TransitionMode::VortexSpin, Compositor::TransitionMode::SplitQuadrants,
        Compositor::TransitionMode::Gallery3D, Compositor::TransitionMode::Cube3D,
        Compositor::TransitionMode::Flip3D
    };
    if (index < 0 || index >= 21 || index == m_modeIndex)
        return;
    m_modeIndex = index;
    m_compositor->setTransitionMode(modes[index]);
    emit modeChanged(index);
}

void TransitionController::setTransitionDuration(double secs) {
    secs = std::max(0.0, secs);
    if (qFuzzyCompare(secs + 1.0, m_durationSecs + 1.0))
        return;
    m_durationSecs = secs;
    emit durationChanged(secs);
}

QStringList TransitionController::transitionModeNames() const {
    return {
        tr("Crossfade"), tr("Cut"),
        tr("Wipe ←"), tr("Wipe →"), tr("Wipe ↑"), tr("Wipe ↓"),
        tr("Slide ←"), tr("Slide →"), tr("Slide ↑"), tr("Slide ↓"),
        tr("Dip to Black"), tr("Dip to White"),
        tr("Additive Glow"), tr("Cross Zoom"),
        tr("Split Door (H)"), tr("Split Door (V)"),
        tr("Vortex Spin"), tr("Split 4-Corner"),
        tr("3D Gallery Fly"), tr("3D Cube"), tr("3D Flip")
    };
}

void TransitionController::startAfterOut(std::function<void()> transition) {
    const double outSecs = m_progress == 0 ? m_compositor->requestDeckOut(Deck::Live) : 0.0;
    if (outSecs <= 0.0) {
        transition();
        return;
    }
    m_waitingForOut = true;
    emitRunningIfChanged();
    QTimer::singleShot(qRound(outSecs * 1000.0), this, [this, transition = std::move(transition)]() {
        if (!m_waitingForOut)
            return;
        m_waitingForOut = false;
        transition();
        emitRunningIfChanged();
    });
}

void TransitionController::autoTransition() {
    if (m_waitingForOut || m_animating || stagingEmpty())
        return;
    if (m_modeIndex == 1) {
        cutTransition();
        return;
    }
    startAfterOut([this]() { runAutoTransition(); });
}

void TransitionController::runAutoTransition() {
    m_sweepStart = m_progressF;
    m_sweepClock.start();
    m_animating = true;
    m_tickConnection = connect(m_compositor, &Compositor::frameTick, this, &TransitionController::onFrameTick,
                               Qt::DirectConnection);
    emitRunningIfChanged();
}

void TransitionController::cutTransition() {
    if (m_waitingForOut || stagingEmpty())
        return;
    startAfterOut([this]() {
        stopAnimation();
        applyProgress(1.f);
        emitRunningIfChanged();
    });
}
