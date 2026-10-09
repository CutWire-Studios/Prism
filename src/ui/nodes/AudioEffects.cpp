#include "ui/nodes/AudioEffects.h"

#include "core/audio/AudioRack.h"
#include "ui/nodes/PedalboardDialog.h"

#include <QCheckBox>
#include <QJsonDocument>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDial>
#include <QHBoxLayout>
#include <QLCDNumber>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>
#include <algorithm>

namespace AudioEffects {

bool runAudioEffectDialog(QWidget *parent, const QString &title,
                          const std::function<void(QVBoxLayout *)> &build) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    build(layout);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    return dialog.exec() == QDialog::Accepted;
}

bool bypassed(const QJsonObject &p) {
    return p.value(QStringLiteral("bypass")).toBool(false);
}

constexpr int kLinearDialSteps = 1000;

double snapStep(double value, double step) {
    if (step <= 0.0)
        return value;
    return std::round(value / step) * step;
}

int linearToDial(double value, double minV, double maxV) {
    value = std::clamp(value, minV, maxV);
    const double t = (maxV > minV) ? (value - minV) / (maxV - minV) : 0.0;
    return static_cast<int>(std::lround(t * kLinearDialSteps));
}

double dialToLinear(int dial, double minV, double maxV, double step) {
    const double t = static_cast<double>(dial) / kLinearDialSteps;
    return snapStep(minV + t * (maxV - minV), step);
}

void updateLcd(QLCDNumber *lcd, double value, int precision) {
    if (precision <= 0)
        lcd->display(static_cast<int>(std::lround(value)));
    else
        lcd->display(QString::number(value, 'f', precision));
}

void wireDial(QDial *dial, QLCDNumber *lcd, double &value,
              double minV, double maxV, double step, int precision,
              const std::function<void()> &notify) {
    updateLcd(lcd, value, precision);
    QObject::connect(dial, &QDial::valueChanged, [&value, minV, maxV, step, precision, lcd, notify](int v) {
        value = dialToLinear(v, minV, maxV, step);
        updateLcd(lcd, value, precision);
        notify();
    });
}

void addDialColumn(QHBoxLayout *row, const QString &title, double &value,
                   double minV, double maxV, double step, const QString &unit,
                   int dialSize, int precision, const std::function<void()> &notify) {
    auto *col = new QVBoxLayout;
    auto *titleLabel = new QLabel(title);
    titleLabel->setAlignment(Qt::AlignHCenter);
    col->addWidget(titleLabel);

    auto *lcd = new QLCDNumber(5);
    lcd->setSegmentStyle(QLCDNumber::Flat);
    lcd->setMode(QLCDNumber::Dec);
    lcd->setMinimumHeight(32);

    auto *unitLabel = new QLabel(unit);
    auto *readoutRow = new QHBoxLayout;
    readoutRow->addStretch();
    readoutRow->addWidget(lcd);
    if (!unit.isEmpty())
        readoutRow->addWidget(unitLabel);
    readoutRow->addStretch();
    col->addLayout(readoutRow);

    auto *dial = new QDial;
    dial->setRange(0, kLinearDialSteps);
    dial->setValue(linearToDial(value, minV, maxV));
    dial->setNotchesVisible(true);
    dial->setMinimumSize(dialSize, dialSize);
    col->addWidget(dial, 0, Qt::AlignHCenter);

    wireDial(dial, lcd, value, minV, maxV, step, precision, notify);
    row->addLayout(col, 1);
}

void addPrimaryDial(QVBoxLayout *layout, double &value, double minV, double maxV,
                    double step, const QString &unit, int precision,
                    const std::function<void()> &notify) {
    auto *row = new QHBoxLayout;
    addDialColumn(row, QString(), value, minV, maxV, step, unit, 140, precision, notify);
    layout->addLayout(row);
}

void addBypassCheckbox(QVBoxLayout *layout, bool &bypass, const std::function<void()> &notify) {
    auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
    bypassBox->setChecked(bypass);
    layout->addWidget(bypassBox);
    QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass, notify](bool on) {
        bypass = on;
        notify();
    });
}

constexpr double kFreqDialMinHz = 20.0;
constexpr double kFreqDialMaxHz = 20000.0;
constexpr int kFreqDialSteps = 1000;

int freqToDial(double freq) {
    freq = std::clamp(freq, kFreqDialMinHz, kFreqDialMaxHz);
    const double t = std::log(freq / kFreqDialMinHz) / std::log(kFreqDialMaxHz / kFreqDialMinHz);
    return static_cast<int>(std::lround(t * kFreqDialSteps));
}

double dialToFreq(int dial) {
    const double t = static_cast<double>(dial) / kFreqDialSteps;
    return kFreqDialMinHz * std::pow(kFreqDialMaxHz / kFreqDialMinHz, t);
}

void addFreqDial(QVBoxLayout *layout, double &freq, const std::function<void()> &notify) {
    auto *row = new QHBoxLayout;
    auto *col = new QVBoxLayout;

    auto *lcd = new QLCDNumber(5);
    lcd->setSegmentStyle(QLCDNumber::Flat);
    lcd->setMode(QLCDNumber::Dec);
    lcd->setMinimumHeight(36);

    auto *unitLabel = new QLabel(QStringLiteral("Hz"));
    auto *readoutRow = new QHBoxLayout;
    readoutRow->addStretch();
    readoutRow->addWidget(lcd);
    readoutRow->addWidget(unitLabel);
    readoutRow->addStretch();
    col->addLayout(readoutRow);

    auto *dial = new QDial;
    dial->setRange(0, kFreqDialSteps);
    dial->setValue(freqToDial(freq));
    dial->setNotchesVisible(true);
    dial->setMinimumSize(140, 140);
    col->addWidget(dial, 0, Qt::AlignHCenter);

    updateLcd(lcd, freq, 0);
    QObject::connect(dial, &QDial::valueChanged, [&freq, lcd, notify](int v) {
        freq = dialToFreq(v);
        updateLcd(lcd, freq, 0);
        notify();
    });

    row->addLayout(col, 1);
    layout->addLayout(row);
}

const QVector<AudioEffectDescriptor> &all() {
    static const QVector<AudioEffectDescriptor> registry = [] {
        AudioEffectDescriptor d;
        d.id = kAudioFxEffectId;
        d.name = QStringLiteral("Audio FX");
        d.menuLabel = QStringLiteral("Audio FX");
        d.defaultParams = prism::audiofx::toJson({});
        d.editLabel = QStringLiteral("Edit");
        d.dynamicLabel = [](const QJsonObject &p) {
            const size_t n = prism::audiofx::fromJson(p).items.size();
            return n == 0 ? QStringLiteral("Empty") : QStringLiteral("%1 pedals").arg(n);
        };
        d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &onLiveChange) {
            PedalboardDialog dialog(params, onLiveChange, parent);
            if (dialog.exec() != QDialog::Accepted)
                return false;
            params = dialog.params();
            return true;
        };
        return QVector<AudioEffectDescriptor>{d};
    }();
    return registry;
}

const AudioEffectDescriptor *byId(int id) {
    for (const AudioEffectDescriptor &d : all())
        if (d.id == id) return &d;
    return nullptr;
}

} // namespace AudioEffects
