#pragma once

#include <QColor>
#include <QPushButton>
#include <QString>
#include <QWidget>

class QDoubleSpinBox;
class QSlider;

namespace style {

/// Label + slider + spin box on one row. valueChanged fires on user edits only; setValue is silent.
class SliderSpinRow : public QWidget {
    Q_OBJECT
public:
    SliderSpinRow(const QString &label, double min, double max, double step, int decimals = 2,
                  const QString &suffix = QString(), QWidget *parent = nullptr);

    double value() const;
    void setValue(double v);
    void setRange(double min, double max);

signals:
    void valueChanged(double v);

private:
    void syncSlider();

    QSlider *m_slider;
    QDoubleSpinBox *m_spin;
};

/// Swatch button that opens a colour dialog (with alpha). colorChanged fires on user edits only.
class ColorButton : public QPushButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget *parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor &c);

signals:
    void colorChanged(const QColor &c);

private:
    QColor m_color = Qt::white;
};

/// A label on the left, @p field filling the rest.
QWidget *labeledRow(const QString &label, QWidget *field, QWidget *parent = nullptr);

} // namespace style
