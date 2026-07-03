#pragma once

#include <QObject>
#include "ui/canvas/VideoWidget.h"

class QComboBox;
class QPushButton;
class QSlider;
class QVariantAnimation;

/// Manages the crossfader transition mode, AUTO/CUT buttons, and animated
/// fader sweep.  Extracted from MainWindow to keep transition logic cohesive.
class TransitionController : public QObject {
    Q_OBJECT
public:
    /// durationSlider holds hundredths of a second (0–100 = 0–1 s).
    explicit TransitionController(VideoWidget *videoWidget,
                                  QComboBox   *transitionCombo,
                                  QSlider     *durationSlider,
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

    /// Stop any running fader animation and drop UI pointers before they are destroyed.
    void shutdown();

    ~TransitionController() override;

public slots:
    void onTransitionModeChanged(int index);
    void onAutoTransitionClicked();
    void onCutTransitionClicked();

private:
    VideoWidget    *m_videoWidget;
    QComboBox      *m_transitionCombo;
    QSlider        *m_durationSlider;
    QPushButton    *m_autoBtn;
    QPushButton    *m_cutBtn;
    QSlider        *m_crossfaderSlider;
    QVariantAnimation *m_animation = nullptr;
};
