#pragma once

#include "core/text/TextShading.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;

namespace style {

class ColorButton;
class SliderSpinRow;
class GradientStopBar;

/// Editor for a prism::TextGradient: draggable stop bar, per-stop colour, type, angle, offset,
/// speed, scale, centre, repeat, OKLab and (text only) the box the gradient maps onto.
class GradientEditor : public QWidget {
    Q_OBJECT
public:
    explicit GradientEditor(QWidget *parent = nullptr);

    const prism::TextGradient &gradient() const { return m_gradient; }
    /// Silent: does not emit changed().
    void setGradient(const prism::TextGradient &gradient);
    /// The "Map to" (TextGradientSpace) field; shapes have no accent scopes, so it is hidden by default.
    void setSpaceVisible(bool visible);

signals:
    void changed();

private:
    void load();
    void edit();
    void refreshStopRow();

    prism::TextGradient m_gradient;
    int m_selected = 0;
    bool m_loading = false;

    GradientStopBar *m_bar;
    ColorButton *m_stopColor;
    QDoubleSpinBox *m_stopPos;
    QPushButton *m_removeStop;
    QComboBox *m_kind;
    SliderSpinRow *m_angle, *m_offset, *m_speed, *m_scale, *m_centerX, *m_centerY;
    QCheckBox *m_repeat, *m_oklab;
    QComboBox *m_space;
    QWidget *m_spaceRow;
};

} // namespace style
