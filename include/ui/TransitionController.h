#pragma once

#include <QObject>
#include "ui/VideoWidget.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;
class QVariantAnimation;

/// Manages the crossfader transition mode, AUTO/CUT buttons, and animated
/// fader sweep.  Extracted from MainWindow to keep transition logic cohesive.
class TransitionController : public QObject {
    Q_OBJECT
public:
    explicit TransitionController(VideoWidget *videoWidget,
                                  QComboBox   *transitionCombo,
                                  QDoubleSpinBox *durationSpin,
                                  QPushButton *autoBtn,
                                  QPushButton *cutBtn,
                                  QSlider     *crossfaderSlider,
                                  QObject     *parent = nullptr);

    /// Call once after construction to wire all internal signals.
    void setupConnections();

    int    currentModeIndex()    const;
    double currentDurationSecs() const;

    void setTransitionModeIndex(int index);
    void setTransitionDuration(double secs);
    QStringList transitionModeNames() const;

public slots:
    void onTransitionModeChanged(int index);
    void onAutoTransitionClicked();
    void onCutTransitionClicked();

private:
    VideoWidget    *m_videoWidget;
    QComboBox      *m_transitionCombo;
    QDoubleSpinBox *m_durationSpin;
    QPushButton    *m_autoBtn;
    QPushButton    *m_cutBtn;
    QSlider        *m_crossfaderSlider;
    QVariantAnimation *m_animation = nullptr;
};
