#include "ui/editors/style/StyleControls.h"

#include "ui/common/Theme.h"

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <cmath>

namespace style {

namespace {
constexpr int kSliderSteps = 1000;
}

SliderSpinRow::SliderSpinRow(const QString &label, double min, double max, double step, int decimals,
                             const QString &suffix, QWidget *parent)
    : QWidget(parent)
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_spin(new QDoubleSpinBox(this))
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto *name = new QLabel(label, this);
    name->setProperty("role", "secondary");
    name->setMinimumWidth(78);

    m_slider->setRange(0, kSliderSteps);
    m_spin->setRange(min, max);
    m_spin->setSingleStep(step);
    m_spin->setDecimals(decimals);
    m_spin->setSuffix(suffix.isEmpty() ? QString() : QLatin1Char(' ') + suffix);
    m_spin->setKeyboardTracking(false);
    m_spin->setMinimumWidth(78);

    lay->addWidget(name);
    lay->addWidget(m_slider, 1);
    lay->addWidget(m_spin);

    connect(m_slider, &QSlider::valueChanged, this, [this](int pos) {
        const double lo = m_spin->minimum();
        const double hi = m_spin->maximum();
        const QSignalBlocker block(m_spin);
        m_spin->setValue(lo + (hi - lo) * pos / kSliderSteps);
        emit valueChanged(m_spin->value());
    });
    connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        syncSlider();
        emit valueChanged(v);
    });
}

double SliderSpinRow::value() const
{
    return m_spin->value();
}

void SliderSpinRow::setValue(double v)
{
    const QSignalBlocker a(m_spin);
    m_spin->setValue(v);
    syncSlider();
}

void SliderSpinRow::setRange(double min, double max)
{
    const QSignalBlocker a(m_spin);
    m_spin->setRange(min, max);
    syncSlider();
}

void SliderSpinRow::syncSlider()
{
    const double lo = m_spin->minimum();
    const double hi = m_spin->maximum();
    const QSignalBlocker block(m_slider);
    m_slider->setValue(hi > lo ? int(std::lround((m_spin->value() - lo) / (hi - lo) * kSliderSteps)) : 0);
}

ColorButton::ColorButton(QWidget *parent)
    : QPushButton(parent)
{
    setMinimumWidth(84);
    setCursor(Qt::PointingHandCursor);
    connect(this, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, this, tr("Pick Color"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid() || c == m_color)
            return;
        setColor(c);
        emit colorChanged(c);
    });
    setColor(m_color);
}

void ColorButton::setColor(const QColor &c)
{
    m_color = c;
    setStyleSheet(QStringLiteral("background-color:%1; color:%2; border:1px solid %3; border-radius:3px; padding:2px 8px;")
                      .arg(c.name(QColor::HexArgb),
                           c.lightness() > 128 ? QStringLiteral("#111") : QStringLiteral("#eee"),
                           Theme::instance().tokens().textSecondary.name(QColor::HexArgb)));
    setText(c.name(c.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb).toUpper());
}

QWidget *labeledRow(const QString &label, QWidget *field, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto *name = new QLabel(label, row);
    name->setProperty("role", "secondary");
    name->setMinimumWidth(78);
    lay->addWidget(name);
    lay->addWidget(field, 1);
    return row;
}

} // namespace style
