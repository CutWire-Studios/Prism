#include "app/transitions/TransitionController.h"
#include <QVariantAnimation>
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
    if (m_animation) {
        m_animation->stop();
        delete m_animation;
        m_animation = nullptr;
    }
}

void TransitionController::emitRunningIfChanged() {
    const bool running = isRunning();
    if (running == m_lastRunning)
        return;
    m_lastRunning = running;
    emit runningChanged(running);
}

void TransitionController::applyFader(int value) {
    value = std::clamp(value, 0, 100);
    if (value == m_faderValue)
        return;
    m_faderValue = value;
    m_compositor->setCrossfade(value / 100.0f);
    emit faderValueChanged(value);
}

void TransitionController::setFaderValue(int value) {
    stopAnimation();
    applyFader(value);
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

void TransitionController::startAfterOut(int currentVal, std::function<void()> transition) {
    const bool atEndStop = currentVal == 0 || currentVal == 100;
    const double outSecs = atEndStop ? m_compositor->requestDeckOut(currentVal == 0) : 0.0;
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
    if (m_waitingForOut)
        return;
    startAfterOut(m_faderValue, [this]() { runAutoTransition(); });
}

void TransitionController::runAutoTransition() {
    stopAnimation();

    const int currentVal = m_faderValue;
    const int targetVal  = (currentVal <= 50) ? 100 : 0;
    if (currentVal == targetVal) {
        emitRunningIfChanged();
        return;
    }

    m_animation = new QVariantAnimation(this);
    m_animation->setDuration(static_cast<int>(m_durationSecs * 1000.0));
    m_animation->setStartValue(currentVal);
    m_animation->setEndValue(targetVal);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);

    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        applyFader(value.toInt());
    });
    connect(m_animation, &QVariantAnimation::finished, this, [this]() {
        if (m_animation) { m_animation->deleteLater(); m_animation = nullptr; }
        emitRunningIfChanged();
    });

    m_animation->start();
    emitRunningIfChanged();
}

void TransitionController::cutTransition() {
    if (m_waitingForOut)
        return;
    startAfterOut(m_faderValue, [this]() { runCutTransition(); });
}

void TransitionController::runCutTransition() {
    stopAnimation();
    applyFader((m_faderValue <= 50) ? 100 : 0);
    emitRunningIfChanged();
}
