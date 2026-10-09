#include "ui/editors/style/GradientEditor.h"

#include "ui/common/Theme.h"
#include "ui/editors/style/StyleControls.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace style {

using prism::TextGradient;
using prism::TextGradientStop;

class GradientStopBar : public QWidget {
    Q_OBJECT
public:
    explicit GradientStopBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(40);
        setMinimumWidth(160);
        setCursor(Qt::PointingHandCursor);
    }

    void setStops(const QList<TextGradientStop> *stops, int selected)
    {
        m_stops = stops;
        m_selected = selected;
        update();
    }

signals:
    void selectionChanged(int index);
    void stopMoved(int index, double pos);
    void stopAdded(double pos);
    void stopRemoved(int index);
    void colorRequested(int index);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF bar = barRect();
        p.setClipRect(bar);
        for (int y = 0; y < 22; y += 6)
            for (int x = 0; x < int(bar.width()) + 6; x += 6)
                p.fillRect(QRectF(bar.left() + x, bar.top() + y, 6, 6),
                           ((x / 6 + y / 6) % 2) ? QColor(0x55, 0x55, 0x55) : QColor(0x88, 0x88, 0x88));
        QLinearGradient g(bar.left(), 0, bar.right(), 0);
        for (const TextGradientStop &s : *m_stops)
            g.setColorAt(std::clamp(s.pos, 0.0, 1.0), s.color);
        p.fillRect(bar, g);
        p.setClipping(false);
        const auto &t = Theme::instance().tokens();
        p.setPen(t.stroke);
        p.drawRoundedRect(bar, 3, 3);

        for (int i = 0; i < m_stops->size(); ++i) {
            const double x = bar.left() + bar.width() * std::clamp((*m_stops)[i].pos, 0.0, 1.0);
            const QRectF handle(x - 6, bar.bottom() + 3, 12, 13);
            p.setPen(QPen(i == m_selected ? t.accent : t.textSecondary, i == m_selected ? 2 : 1));
            p.setBrush((*m_stops)[i].color);
            p.drawRoundedRect(handle, 3, 3);
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        const int hit = stopAt(e->position());
        if (e->button() == Qt::RightButton) {
            if (hit >= 0)
                emit stopRemoved(hit);
            return;
        }
        if (e->button() != Qt::LeftButton)
            return;
        if (hit >= 0) {
            m_drag = hit;
            if (hit != m_selected)
                emit selectionChanged(hit);
        } else if (barRect().adjusted(0, 0, 0, 20).contains(e->position())) {
            emit stopAdded(posAt(e->position().x()));
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_drag >= 0)
            emit stopMoved(m_drag, posAt(e->position().x()));
    }

    void mouseReleaseEvent(QMouseEvent *) override { m_drag = -1; }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        const int hit = stopAt(e->position());
        if (hit >= 0)
            emit colorRequested(hit);
    }

private:
    QRectF barRect() const { return QRectF(8, 2, width() - 16, 22); }

    double posAt(double x) const
    {
        const QRectF bar = barRect();
        return std::clamp((x - bar.left()) / bar.width(), 0.0, 1.0);
    }

    int stopAt(const QPointF &pt) const
    {
        const QRectF bar = barRect();
        int best = -1;
        double bestDist = 9.0;
        for (int i = 0; i < m_stops->size(); ++i) {
            const double x = bar.left() + bar.width() * std::clamp((*m_stops)[i].pos, 0.0, 1.0);
            const double d = std::abs(pt.x() - x);
            if (d <= bestDist && pt.y() >= bar.bottom() - 4) {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }

    const QList<TextGradientStop> *m_stops = nullptr;
    int m_selected = 0;
    int m_drag = -1;
};

GradientEditor::GradientEditor(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    m_bar = new GradientStopBar(this);
    lay->addWidget(m_bar);

    auto *stopRow = new QHBoxLayout;
    stopRow->setSpacing(6);
    m_stopColor = new ColorButton(this);
    m_stopPos = new QDoubleSpinBox(this);
    m_stopPos->setRange(0, 100);
    m_stopPos->setDecimals(1);
    m_stopPos->setSuffix(QStringLiteral(" %"));
    m_stopPos->setKeyboardTracking(false);
    m_removeStop = new QPushButton(tr("Remove"), this);
    stopRow->addWidget(new QLabel(tr("Stop"), this));
    stopRow->addWidget(m_stopColor);
    stopRow->addWidget(m_stopPos);
    stopRow->addStretch(1);
    stopRow->addWidget(m_removeStop);
    lay->addLayout(stopRow);

    m_kind = new QComboBox(this);
    m_kind->addItem(tr("Linear"), int(prism::TextGradientKind::Linear));
    m_kind->addItem(tr("Radial"), int(prism::TextGradientKind::Radial));
    m_kind->addItem(tr("Sweep"), int(prism::TextGradientKind::Sweep));
    lay->addWidget(labeledRow(tr("Type"), m_kind));

    m_angle = new SliderSpinRow(tr("Angle"), -360, 360, 1, 0, QStringLiteral("°"), this);
    m_offset = new SliderSpinRow(tr("Offset"), -2, 2, 0.01, 2, QString(), this);
    m_speed = new SliderSpinRow(tr("Speed"), -4, 4, 0.05, 2, QStringLiteral("/s"), this);
    m_scale = new SliderSpinRow(tr("Scale"), 0.1, 8, 0.05, 2, QString(), this);
    m_centerX = new SliderSpinRow(tr("Center X"), 0, 1, 0.01, 2, QString(), this);
    m_centerY = new SliderSpinRow(tr("Center Y"), 0, 1, 0.01, 2, QString(), this);
    for (QWidget *w : {static_cast<QWidget *>(m_angle), static_cast<QWidget *>(m_offset), static_cast<QWidget *>(m_speed),
                       static_cast<QWidget *>(m_scale), static_cast<QWidget *>(m_centerX), static_cast<QWidget *>(m_centerY)})
        lay->addWidget(w);

    auto *flags = new QHBoxLayout;
    m_repeat = new QCheckBox(tr("Repeat"), this);
    m_oklab = new QCheckBox(tr("OKLab"), this);
    flags->addWidget(m_repeat);
    flags->addWidget(m_oklab);
    flags->addStretch(1);
    lay->addLayout(flags);

    m_space = new QComboBox(this);
    m_space->addItem(tr("Block"), int(prism::TextGradientSpace::Block));
    m_space->addItem(tr("Line"), int(prism::TextGradientSpace::Line));
    m_space->addItem(tr("Word"), int(prism::TextGradientSpace::Word));
    m_space->addItem(tr("Glyph"), int(prism::TextGradientSpace::Glyph));
    m_space->addItem(tr("Accent run"), int(prism::TextGradientSpace::AccentRun));
    m_spaceRow = labeledRow(tr("Map to"), m_space);
    m_spaceRow->setVisible(false);
    lay->addWidget(m_spaceRow);

    connect(m_bar, &GradientStopBar::selectionChanged, this, [this](int i) {
        m_selected = i;
        refreshStopRow();
    });
    connect(m_bar, &GradientStopBar::stopMoved, this, [this](int i, double pos) {
        m_gradient.stops[i].pos = pos;
        refreshStopRow();
        edit();
    });
    connect(m_bar, &GradientStopBar::stopAdded, this, [this](double pos) {
        QList<TextGradientStop> sorted = m_gradient.stops;
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.pos < b.pos; });
        QColor c = sorted.isEmpty() ? QColor(Qt::white) : sorted.first().color;
        for (int i = 0; i + 1 < sorted.size(); ++i) {
            if (pos >= sorted[i].pos && pos <= sorted[i + 1].pos) {
                const double span = sorted[i + 1].pos - sorted[i].pos;
                const double f = span > 0 ? (pos - sorted[i].pos) / span : 0.0;
                const QColor a = sorted[i].color, b = sorted[i + 1].color;
                c = QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * f, a.greenF() + (b.greenF() - a.greenF()) * f,
                                     a.blueF() + (b.blueF() - a.blueF()) * f, a.alphaF() + (b.alphaF() - a.alphaF()) * f);
                break;
            }
            if (pos > sorted[i + 1].pos)
                c = sorted[i + 1].color;
        }
        m_gradient.stops.append({pos, c});
        m_selected = m_gradient.stops.size() - 1;
        refreshStopRow();
        edit();
    });
    auto removeStop = [this](int i) {
        if (m_gradient.stops.size() <= 2 || i < 0 || i >= m_gradient.stops.size())
            return;
        m_gradient.stops.removeAt(i);
        m_selected = std::min(m_selected, int(m_gradient.stops.size()) - 1);
        refreshStopRow();
        edit();
    };
    connect(m_bar, &GradientStopBar::stopRemoved, this, removeStop);
    connect(m_removeStop, &QPushButton::clicked, this, [this, removeStop] { removeStop(m_selected); });
    connect(m_bar, &GradientStopBar::colorRequested, this, [this](int i) {
        m_selected = i;
        refreshStopRow();
        m_stopColor->click();
    });
    connect(m_stopColor, &ColorButton::colorChanged, this, [this](const QColor &c) {
        m_gradient.stops[m_selected].color = c;
        refreshStopRow();
        edit();
    });
    connect(m_stopPos, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_loading)
            return;
        m_gradient.stops[m_selected].pos = v / 100.0;
        refreshStopRow();
        edit();
    });

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading)
            return;
        m_gradient.kind = prism::TextGradientKind(m_kind->currentData().toInt());
        load();
        edit();
    });
    connect(m_space, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading)
            return;
        m_gradient.space = prism::TextGradientSpace(m_space->currentData().toInt());
        edit();
    });
    connect(m_repeat, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading) {
            m_gradient.repeat = on;
            edit();
        }
    });
    connect(m_oklab, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading) {
            m_gradient.oklab = on;
            edit();
        }
    });
    connect(m_angle, &SliderSpinRow::valueChanged, this, [this](double v) { m_gradient.angle = v; edit(); });
    connect(m_offset, &SliderSpinRow::valueChanged, this, [this](double v) { m_gradient.offset = v; edit(); });
    connect(m_speed, &SliderSpinRow::valueChanged, this, [this](double v) { m_gradient.offsetSpeed = v; edit(); });
    connect(m_scale, &SliderSpinRow::valueChanged, this, [this](double v) { m_gradient.scale = v; edit(); });
    connect(m_centerX, &SliderSpinRow::valueChanged, this, [this](double v) { m_gradient.center.setX(v); edit(); });
    connect(m_centerY, &SliderSpinRow::valueChanged, this, [this](double v) { m_gradient.center.setY(v); edit(); });

    load();
}

void GradientEditor::setGradient(const prism::TextGradient &gradient)
{
    m_gradient = gradient;
    if (m_gradient.stops.size() < 2)
        m_gradient.stops = TextGradient().stops;
    m_selected = std::clamp(m_selected, 0, int(m_gradient.stops.size()) - 1);
    load();
}

void GradientEditor::setSpaceVisible(bool visible)
{
    m_spaceRow->setVisible(visible);
}

void GradientEditor::load()
{
    m_loading = true;
    m_kind->setCurrentIndex(m_kind->findData(int(m_gradient.kind)));
    m_angle->setValue(m_gradient.angle);
    m_offset->setValue(m_gradient.offset);
    m_speed->setValue(m_gradient.offsetSpeed);
    m_scale->setValue(m_gradient.scale);
    m_centerX->setValue(m_gradient.center.x());
    m_centerY->setValue(m_gradient.center.y());
    m_repeat->setChecked(m_gradient.repeat);
    m_oklab->setChecked(m_gradient.oklab);
    m_space->setCurrentIndex(m_space->findData(int(m_gradient.space)));
    const bool radial = m_gradient.kind != prism::TextGradientKind::Linear;
    m_angle->setVisible(m_gradient.kind != prism::TextGradientKind::Radial);
    m_centerX->setVisible(radial);
    m_centerY->setVisible(radial);
    refreshStopRow();
    m_loading = false;
}

void GradientEditor::refreshStopRow()
{
    const bool was = m_loading;
    m_loading = true;
    m_bar->setStops(&m_gradient.stops, m_selected);
    const TextGradientStop &s = m_gradient.stops[m_selected];
    m_stopColor->setColor(s.color);
    m_stopPos->setValue(s.pos * 100.0);
    m_removeStop->setEnabled(m_gradient.stops.size() > 2);
    m_loading = was;
}

void GradientEditor::edit()
{
    if (!m_loading)
        emit changed();
}

} // namespace style

#include "GradientEditor.moc"
