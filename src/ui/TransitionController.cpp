#include "ui/TransitionController.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSlider>
#include <QVariantAnimation>
#include <QEasingCurve>

TransitionController::TransitionController(VideoWidget    *videoWidget,
                                           QComboBox      *transitionCombo,
                                           QDoubleSpinBox *durationSpin,
                                           QPushButton    *autoBtn,
                                           QPushButton    *cutBtn,
                                           QSlider        *crossfaderSlider,
                                           QObject        *parent)
    : QObject(parent)
    , m_videoWidget(videoWidget)
    , m_transitionCombo(transitionCombo)
    , m_durationSpin(durationSpin)
    , m_autoBtn(autoBtn)
    , m_cutBtn(cutBtn)
    , m_crossfaderSlider(crossfaderSlider)
{
}

void TransitionController::setupConnections() {
    connect(m_transitionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TransitionController::onTransitionModeChanged);
    connect(m_autoBtn, &QPushButton::clicked, this, &TransitionController::onAutoTransitionClicked);
    connect(m_cutBtn,  &QPushButton::clicked, this, &TransitionController::onCutTransitionClicked);

    // Stop any running animation when the user grabs the fader manually.
    connect(m_crossfaderSlider, &QSlider::sliderPressed, this, [this]() {
        if (m_animation) {
            m_animation->stop();
            m_animation->deleteLater();
            m_animation = nullptr;
        }
    });
}

int TransitionController::currentModeIndex() const {
    return m_transitionCombo ? m_transitionCombo->currentIndex() : 0;
}

double TransitionController::currentDurationSecs() const {
    return m_durationSpin ? m_durationSpin->value() : 1.0;
}

void TransitionController::onTransitionModeChanged(int index) {
    using TM = VideoWidget::TransitionMode;
    static const TM modes[] = {
        TM::Crossfade, TM::Cut,
        TM::WipeLeft, TM::WipeRight, TM::WipeUp, TM::WipeDown,
        TM::SlideLeft, TM::SlideRight, TM::SlideUp, TM::SlideDown,
        TM::DipToBlack, TM::DipToWhite,
        TM::Additive, TM::CrossZoom, TM::SplitDoor, TM::SplitDoorVert,
        TM::VortexSpin, TM::SplitQuadrants, TM::Gallery3D,
        TM::Cube3D, TM::Flip3D
    };
    if (index >= 0 && index < 21)
        m_videoWidget->setTransitionMode(modes[index]);
}

void TransitionController::onAutoTransitionClicked() {
    if (m_animation) {
        m_animation->stop();
        delete m_animation;
        m_animation = nullptr;
    }

    int currentVal = m_crossfaderSlider->value();
    int targetVal  = (currentVal <= 50) ? 100 : 0;
    if (currentVal == targetVal) return;

    int durationMs = static_cast<int>(currentDurationSecs() * 1000.0);

    m_animation = new QVariantAnimation(this);
    m_animation->setDuration(durationMs);
    m_animation->setStartValue(currentVal);
    m_animation->setEndValue(targetVal);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);

    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_crossfaderSlider->setValue(value.toInt());
    });
    connect(m_animation, &QVariantAnimation::finished, this, [this]() {
        if (m_animation) { m_animation->deleteLater(); m_animation = nullptr; }
    });

    m_animation->start();
}

void TransitionController::onCutTransitionClicked() {
    if (m_animation) {
        m_animation->stop();
        delete m_animation;
        m_animation = nullptr;
    }

    int currentVal = m_crossfaderSlider->value();
    int targetVal  = (currentVal <= 50) ? 100 : 0;
    m_crossfaderSlider->setValue(targetVal);
}

void TransitionController::setTransitionModeIndex(int index) {
    if (m_transitionCombo && index >= 0 && index < m_transitionCombo->count()) {
        m_transitionCombo->setCurrentIndex(index);
    }
}

void TransitionController::setTransitionDuration(double secs) {
    if (m_durationSpin) {
        m_durationSpin->setValue(secs);
    }
}

QStringList TransitionController::transitionModeNames() const {
    QStringList names;
    if (m_transitionCombo) {
        for (int i = 0; i < m_transitionCombo->count(); ++i) {
            names.append(m_transitionCombo->itemText(i));
        }
    }
    return names;
}
