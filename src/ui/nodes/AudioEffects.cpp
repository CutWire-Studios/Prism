#include "ui/nodes/AudioEffects.h"

#include <QCheckBox>
#include <QJsonDocument>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDial>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLCDNumber>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <algorithm>

namespace {

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

QSlider *addDbSlider(QFormLayout *form, const QString &label, double &value,
                     double minDb, double maxDb, double step = 0.5) {
    auto *slider = new QSlider(Qt::Horizontal);
    const int scale = static_cast<int>(1.0 / step);
    slider->setRange(static_cast<int>(minDb * scale), static_cast<int>(maxDb * scale));
    slider->setValue(static_cast<int>(value * scale));
    auto *readout = new QLabel(QString::number(value, 'f', 1) + QStringLiteral(" dB"));
    QObject::connect(slider, &QSlider::valueChanged, readout, [readout, scale, step](int v) {
        readout->setText(QString::number(v * step, 'f', 1) + QStringLiteral(" dB"));
    });
    auto *row = new QHBoxLayout;
    row->addWidget(slider, 1);
    row->addWidget(readout);
    form->addRow(label, row);
    QObject::connect(slider, &QSlider::valueChanged, [&value, step](int v) { value = v * step; });
    return slider;
}

QSlider *addPercentSlider(QFormLayout *form, const QString &label, double &value,
                          double minV, double maxV) {
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(static_cast<int>(minV * 10), static_cast<int>(maxV * 10));
    slider->setValue(static_cast<int>(value * 10));
    auto *readout = new QLabel(QString::number(value, 'f', 1));
    QObject::connect(slider, &QSlider::valueChanged, readout, [readout](int v) {
        readout->setText(QString::number(v / 10.0, 'f', 1));
    });
    auto *row = new QHBoxLayout;
    row->addWidget(slider, 1);
    row->addWidget(readout);
    form->addRow(label, row);
    QObject::connect(slider, &QSlider::valueChanged, [&value](int v) { value = v / 10.0; });
    return slider;
}

bool bypassed(const QJsonObject &p) {
    return p.value(QStringLiteral("bypass")).toBool(false);
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
    auto *lcd = new QLCDNumber(5);
    lcd->setSegmentStyle(QLCDNumber::Flat);
    lcd->setMode(QLCDNumber::Dec);
    lcd->setMinimumHeight(36);

    auto *unitLabel = new QLabel(QStringLiteral("Hz"));

    auto *dial = new QDial;
    dial->setRange(0, kFreqDialSteps);
    dial->setValue(freqToDial(freq));
    dial->setNotchesVisible(true);
    dial->setMinimumSize(140, 140);

    auto updateReadout = [lcd](double f) {
        lcd->display(static_cast<int>(std::lround(f)));
    };
    updateReadout(freq);

    QObject::connect(dial, &QDial::valueChanged, [&freq, updateReadout, notify](int v) {
        freq = dialToFreq(v);
        updateReadout(freq);
        notify();
    });

    auto *readoutRow = new QHBoxLayout;
    readoutRow->addStretch();
    readoutRow->addWidget(lcd);
    readoutRow->addWidget(unitLabel);
    readoutRow->addStretch();

    auto *dialRow = new QHBoxLayout;
    dialRow->addStretch();
    dialRow->addWidget(dial);
    dialRow->addStretch();

    layout->addLayout(readoutRow);
    layout->addLayout(dialRow);
}

AudioEffectDescriptor makeGain() {
    AudioEffectDescriptor d;
    d.id = 0;
    d.name = QStringLiteral("GAIN");
    d.menuLabel = QStringLiteral("Gain");
    d.defaultParams = QJsonObject{{QStringLiteral("gainDb"), 0.0}};
    d.filterSpec = [](const QJsonObject &p) {
        if (bypassed(p)) return QString();
        return QStringLiteral("volume=%1dB")
            .arg(p[QStringLiteral("gainDb")].toDouble(0.0), 0, 'f', 1);
    };
    d.editLabel = QStringLiteral("Edit Gain");
    d.dynamicLabel = [](const QJsonObject &p) {
        return QStringLiteral("%1 dB").arg(p[QStringLiteral("gainDb")].toDouble(0.0), 0, 'f', 1);
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &) {
        double gainDb = params[QStringLiteral("gainDb")].toDouble(0.0);
        bool bypass = bypassed(params);
        return runAudioEffectDialog(parent, QStringLiteral("Gain"), [&](QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            addDbSlider(form, QStringLiteral("Gain:"), gainDb, -24.0, 24.0);
            auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
            bypassBox->setChecked(bypass);
            layout->addLayout(form);
            layout->addWidget(bypassBox);
            layout->addStretch();
            QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass](bool on) { bypass = on; });
        }) && (params = QJsonObject{{QStringLiteral("gainDb"), gainDb},
                                     {QStringLiteral("bypass"), bypass}}, true);
    };
    return d;
}

AudioEffectDescriptor makeHighPass() {
    AudioEffectDescriptor d;
    d.id = 1;
    d.name = QStringLiteral("HIGH-PASS");
    d.menuLabel = QStringLiteral("High-Pass Filter");
    d.defaultParams = QJsonObject{{QStringLiteral("freq"), 80.0}};
    d.filterSpec = [](const QJsonObject &p) {
        if (bypassed(p)) return QString();
        return QStringLiteral("highpass=f=%1")
            .arg(p[QStringLiteral("freq")].toDouble(80.0), 0, 'f', 0);
    };
    d.editLabel = QStringLiteral("Edit Filter");
    d.dynamicLabel = [](const QJsonObject &p) {
        return QStringLiteral("%1 Hz").arg(p[QStringLiteral("freq")].toDouble(80.0), 0, 'f', 0);
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &onLiveChange) {
        double freq = params[QStringLiteral("freq")].toDouble(80.0);
        bool bypass = bypassed(params);
        const auto notify = [&]() {
            if (onLiveChange) {
                onLiveChange(QJsonObject{
                    {QStringLiteral("freq"), freq},
                    {QStringLiteral("bypass"), bypass},
                });
            }
        };
        return runAudioEffectDialog(parent, QStringLiteral("High-Pass Filter"), [&](QVBoxLayout *layout) {
            addFreqDial(layout, freq, notify);
            auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
            bypassBox->setChecked(bypass);
            layout->addWidget(bypassBox);
            QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass, notify](bool on) {
                bypass = on;
                notify();
            });
        }) && (params = QJsonObject{{QStringLiteral("freq"), freq},
                                     {QStringLiteral("bypass"), bypass}}, true);
    };
    return d;
}

AudioEffectDescriptor makeLowPass() {
    AudioEffectDescriptor d;
    d.id = 2;
    d.name = QStringLiteral("LOW-PASS");
    d.menuLabel = QStringLiteral("Low-Pass Filter");
    d.defaultParams = QJsonObject{{QStringLiteral("freq"), 12000.0}};
    d.filterSpec = [](const QJsonObject &p) {
        if (bypassed(p)) return QString();
        return QStringLiteral("lowpass=f=%1")
            .arg(p[QStringLiteral("freq")].toDouble(12000.0), 0, 'f', 0);
    };
    d.editLabel = QStringLiteral("Edit Filter");
    d.dynamicLabel = [](const QJsonObject &p) {
        return QStringLiteral("%1 Hz").arg(p[QStringLiteral("freq")].toDouble(12000.0), 0, 'f', 0);
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &onLiveChange) {
        double freq = params[QStringLiteral("freq")].toDouble(12000.0);
        bool bypass = bypassed(params);
        const auto notify = [&]() {
            if (onLiveChange) {
                onLiveChange(QJsonObject{
                    {QStringLiteral("freq"), freq},
                    {QStringLiteral("bypass"), bypass},
                });
            }
        };
        return runAudioEffectDialog(parent, QStringLiteral("Low-Pass Filter"), [&](QVBoxLayout *layout) {
            addFreqDial(layout, freq, notify);
            auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
            bypassBox->setChecked(bypass);
            layout->addWidget(bypassBox);
            QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass, notify](bool on) {
                bypass = on;
                notify();
            });
        }) && (params = QJsonObject{{QStringLiteral("freq"), freq},
                                     {QStringLiteral("bypass"), bypass}}, true);
    };
    return d;
}

AudioEffectDescriptor makeEq3Band() {
    AudioEffectDescriptor d;
    d.id = 3;
    d.name = QStringLiteral("3-BAND EQ");
    d.menuLabel = QStringLiteral("3-Band EQ");
    d.defaultParams = QJsonObject{
        {QStringLiteral("lowDb"), 0.0},
        {QStringLiteral("midDb"), 0.0},
        {QStringLiteral("highDb"), 0.0},
    };
    d.filterSpec = [](const QJsonObject &p) {
        if (bypassed(p)) return QString();
        const double low = p[QStringLiteral("lowDb")].toDouble(0.0);
        const double mid = p[QStringLiteral("midDb")].toDouble(0.0);
        const double high = p[QStringLiteral("highDb")].toDouble(0.0);
        return QStringLiteral(
                   "equalizer=f=120:width_type=o:width=1:g=%1,"
                   "equalizer=f=1000:width_type=o:width=1:g=%2,"
                   "equalizer=f=8000:width_type=o:width=1:g=%3")
            .arg(low, 0, 'f', 1)
            .arg(mid, 0, 'f', 1)
            .arg(high, 0, 'f', 1);
    };
    d.editLabel = QStringLiteral("Edit EQ");
    d.dynamicLabel = [](const QJsonObject &p) {
        return QStringLiteral("L%1 M%2 H%3")
            .arg(p[QStringLiteral("lowDb")].toDouble(0.0), 0, 'f', 0)
            .arg(p[QStringLiteral("midDb")].toDouble(0.0), 0, 'f', 0)
            .arg(p[QStringLiteral("highDb")].toDouble(0.0), 0, 'f', 0);
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &) {
        double low = params[QStringLiteral("lowDb")].toDouble(0.0);
        double mid = params[QStringLiteral("midDb")].toDouble(0.0);
        double high = params[QStringLiteral("highDb")].toDouble(0.0);
        bool bypass = bypassed(params);
        return runAudioEffectDialog(parent, QStringLiteral("3-Band EQ"), [&](QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            addDbSlider(form, QStringLiteral("Low (120 Hz):"), low, -12.0, 12.0);
            addDbSlider(form, QStringLiteral("Mid (1 kHz):"), mid, -12.0, 12.0);
            addDbSlider(form, QStringLiteral("High (8 kHz):"), high, -12.0, 12.0);
            auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
            bypassBox->setChecked(bypass);
            layout->addLayout(form);
            layout->addWidget(bypassBox);
            QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass](bool on) { bypass = on; });
        }) && (params = QJsonObject{
                   {QStringLiteral("lowDb"), low},
                   {QStringLiteral("midDb"), mid},
                   {QStringLiteral("highDb"), high},
                   {QStringLiteral("bypass"), bypass},
               }, true);
    };
    return d;
}

AudioEffectDescriptor makeCompressor() {
    AudioEffectDescriptor d;
    d.id = 4;
    d.name = QStringLiteral("COMPRESS");
    d.menuLabel = QStringLiteral("Compressor");
    d.defaultParams = QJsonObject{
        {QStringLiteral("threshold"), -18.0},
        {QStringLiteral("ratio"), 4.0},
        {QStringLiteral("attack"), 20.0},
        {QStringLiteral("release"), 250.0},
    };
    d.filterSpec = [](const QJsonObject &p) {
        if (bypassed(p)) return QString();
        return QStringLiteral("acompressor=threshold=%1dB:ratio=%2:attack=%3:release=%4")
            .arg(p[QStringLiteral("threshold")].toDouble(-18.0), 0, 'f', 1)
            .arg(p[QStringLiteral("ratio")].toDouble(4.0), 0, 'f', 1)
            .arg(p[QStringLiteral("attack")].toDouble(20.0), 0, 'f', 0)
            .arg(p[QStringLiteral("release")].toDouble(250.0), 0, 'f', 0);
    };
    d.editLabel = QStringLiteral("Edit Compressor");
    d.dynamicLabel = [](const QJsonObject &p) {
        return QStringLiteral("%1:%2")
            .arg(p[QStringLiteral("threshold")].toDouble(-18.0), 0, 'f', 0)
            .arg(p[QStringLiteral("ratio")].toDouble(4.0), 0, 'f', 1);
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &) {
        double threshold = params[QStringLiteral("threshold")].toDouble(-18.0);
        double ratio = params[QStringLiteral("ratio")].toDouble(4.0);
        double attack = params[QStringLiteral("attack")].toDouble(20.0);
        double release = params[QStringLiteral("release")].toDouble(250.0);
        bool bypass = bypassed(params);
        return runAudioEffectDialog(parent, QStringLiteral("Compressor"), [&](QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            addDbSlider(form, QStringLiteral("Threshold:"), threshold, -40.0, 0.0);
            addPercentSlider(form, QStringLiteral("Ratio:"), ratio, 1.0, 20.0);
            auto *attackSpin = new QSpinBox;
            attackSpin->setRange(1, 200);
            attackSpin->setSuffix(QStringLiteral(" ms"));
            attackSpin->setValue(static_cast<int>(attack));
            form->addRow(QStringLiteral("Attack:"), attackSpin);
            auto *releaseSpin = new QSpinBox;
            releaseSpin->setRange(10, 2000);
            releaseSpin->setSuffix(QStringLiteral(" ms"));
            releaseSpin->setValue(static_cast<int>(release));
            form->addRow(QStringLiteral("Release:"), releaseSpin);
            auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
            bypassBox->setChecked(bypass);
            layout->addLayout(form);
            layout->addWidget(bypassBox);
            QObject::connect(attackSpin, QOverload<int>::of(&QSpinBox::valueChanged), [&attack](int v) { attack = v; });
            QObject::connect(releaseSpin, QOverload<int>::of(&QSpinBox::valueChanged), [&release](int v) { release = v; });
            QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass](bool on) { bypass = on; });
        }) && (params = QJsonObject{
                   {QStringLiteral("threshold"), threshold},
                   {QStringLiteral("ratio"), ratio},
                   {QStringLiteral("attack"), attack},
                   {QStringLiteral("release"), release},
                   {QStringLiteral("bypass"), bypass},
               }, true);
    };
    return d;
}

AudioEffectDescriptor makeLimiter() {
    AudioEffectDescriptor d;
    d.id = 5;
    d.name = QStringLiteral("LIMITER");
    d.menuLabel = QStringLiteral("Limiter");
    d.defaultParams = QJsonObject{
        {QStringLiteral("limit"), 0.95},
        {QStringLiteral("attack"), 5.0},
        {QStringLiteral("release"), 50.0},
    };
    d.filterSpec = [](const QJsonObject &p) {
        if (bypassed(p)) return QString();
        return QStringLiteral("alimiter=limit=%1:attack=%2:release=%3")
            .arg(p[QStringLiteral("limit")].toDouble(0.95), 0, 'f', 2)
            .arg(p[QStringLiteral("attack")].toDouble(5.0), 0, 'f', 0)
            .arg(p[QStringLiteral("release")].toDouble(50.0), 0, 'f', 0);
    };
    d.editLabel = QStringLiteral("Edit Limiter");
    d.dynamicLabel = [](const QJsonObject &p) {
        return QStringLiteral("ceil %1")
            .arg(p[QStringLiteral("limit")].toDouble(0.95), 0, 'f', 2);
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &) {
        double limit = params[QStringLiteral("limit")].toDouble(0.95);
        double attack = params[QStringLiteral("attack")].toDouble(5.0);
        double release = params[QStringLiteral("release")].toDouble(50.0);
        bool bypass = bypassed(params);
        return runAudioEffectDialog(parent, QStringLiteral("Limiter"), [&](QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            addPercentSlider(form, QStringLiteral("Ceiling:"), limit, 0.5, 1.0);
            auto *attackSpin = new QSpinBox;
            attackSpin->setRange(1, 50);
            attackSpin->setSuffix(QStringLiteral(" ms"));
            attackSpin->setValue(static_cast<int>(attack));
            form->addRow(QStringLiteral("Attack:"), attackSpin);
            auto *releaseSpin = new QSpinBox;
            releaseSpin->setRange(10, 500);
            releaseSpin->setSuffix(QStringLiteral(" ms"));
            releaseSpin->setValue(static_cast<int>(release));
            form->addRow(QStringLiteral("Release:"), releaseSpin);
            auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
            bypassBox->setChecked(bypass);
            layout->addLayout(form);
            layout->addWidget(bypassBox);
            QObject::connect(attackSpin, QOverload<int>::of(&QSpinBox::valueChanged), [&attack](int v) { attack = v; });
            QObject::connect(releaseSpin, QOverload<int>::of(&QSpinBox::valueChanged), [&release](int v) { release = v; });
            QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass](bool on) { bypass = on; });
        }) && (params = QJsonObject{
                   {QStringLiteral("limit"), limit},
                   {QStringLiteral("attack"), attack},
                   {QStringLiteral("release"), release},
                   {QStringLiteral("bypass"), bypass},
               }, true);
    };
    return d;
}

AudioEffectDescriptor makeGate() {
    AudioEffectDescriptor d;
    d.id = 6;
    d.name = QStringLiteral("GATE");
    d.menuLabel = QStringLiteral("Noise Gate");
    d.defaultParams = QJsonObject{
        {QStringLiteral("threshold"), -40.0},
        {QStringLiteral("ratio"), 2.0},
        {QStringLiteral("attack"), 20.0},
        {QStringLiteral("release"), 250.0},
    };
    d.filterSpec = [](const QJsonObject &p) {
        if (bypassed(p)) return QString();
        return QStringLiteral("agate=threshold=%1dB:ratio=%2:attack=%3:release=%4")
            .arg(p[QStringLiteral("threshold")].toDouble(-40.0), 0, 'f', 1)
            .arg(p[QStringLiteral("ratio")].toDouble(2.0), 0, 'f', 1)
            .arg(p[QStringLiteral("attack")].toDouble(20.0), 0, 'f', 0)
            .arg(p[QStringLiteral("release")].toDouble(250.0), 0, 'f', 0);
    };
    d.editLabel = QStringLiteral("Edit Gate");
    d.dynamicLabel = [](const QJsonObject &p) {
        return QStringLiteral("%1 dB")
            .arg(p[QStringLiteral("threshold")].toDouble(-40.0), 0, 'f', 0);
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &) {
        double threshold = params[QStringLiteral("threshold")].toDouble(-40.0);
        double ratio = params[QStringLiteral("ratio")].toDouble(2.0);
        double attack = params[QStringLiteral("attack")].toDouble(20.0);
        double release = params[QStringLiteral("release")].toDouble(250.0);
        bool bypass = bypassed(params);
        return runAudioEffectDialog(parent, QStringLiteral("Noise Gate"), [&](QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            addDbSlider(form, QStringLiteral("Threshold:"), threshold, -60.0, 0.0);
            addPercentSlider(form, QStringLiteral("Ratio:"), ratio, 1.0, 10.0);
            auto *attackSpin = new QSpinBox;
            attackSpin->setRange(1, 200);
            attackSpin->setSuffix(QStringLiteral(" ms"));
            attackSpin->setValue(static_cast<int>(attack));
            form->addRow(QStringLiteral("Attack:"), attackSpin);
            auto *releaseSpin = new QSpinBox;
            releaseSpin->setRange(10, 2000);
            releaseSpin->setSuffix(QStringLiteral(" ms"));
            releaseSpin->setValue(static_cast<int>(release));
            form->addRow(QStringLiteral("Release:"), releaseSpin);
            auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
            bypassBox->setChecked(bypass);
            layout->addLayout(form);
            layout->addWidget(bypassBox);
            QObject::connect(attackSpin, QOverload<int>::of(&QSpinBox::valueChanged), [&attack](int v) { attack = v; });
            QObject::connect(releaseSpin, QOverload<int>::of(&QSpinBox::valueChanged), [&release](int v) { release = v; });
            QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass](bool on) { bypass = on; });
        }) && (params = QJsonObject{
                   {QStringLiteral("threshold"), threshold},
                   {QStringLiteral("ratio"), ratio},
                   {QStringLiteral("attack"), attack},
                   {QStringLiteral("release"), release},
                   {QStringLiteral("bypass"), bypass},
               }, true);
    };
    return d;
}

AudioEffectDescriptor makeDelay() {
    AudioEffectDescriptor d;
    d.id = 7;
    d.name = QStringLiteral("DELAY");
    d.menuLabel = QStringLiteral("Delay / Echo");
    d.defaultParams = QJsonObject{
        {QStringLiteral("delayMs"), 250.0},
        {QStringLiteral("feedback"), 0.35},
        {QStringLiteral("mix"), 0.4},
    };
    d.filterSpec = [](const QJsonObject &p) {
        if (bypassed(p)) return QString();
        const int ms = static_cast<int>(p[QStringLiteral("delayMs")].toDouble(250.0));
        const double fb = p[QStringLiteral("feedback")].toDouble(0.35);
        const double mix = p[QStringLiteral("mix")].toDouble(0.4);
        return QStringLiteral("aecho=in_gain=%1:out_gain=%2:delays=%3|%3:decays=%4|%4")
            .arg(1.0 - mix * 0.5, 0, 'f', 2)
            .arg(mix, 0, 'f', 2)
            .arg(ms)
            .arg(fb, 0, 'f', 2);
    };
    d.editLabel = QStringLiteral("Edit Delay");
    d.dynamicLabel = [](const QJsonObject &p) {
        return QStringLiteral("%1 ms")
            .arg(p[QStringLiteral("delayMs")].toDouble(250.0), 0, 'f', 0);
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &) {
        double delayMs = params[QStringLiteral("delayMs")].toDouble(250.0);
        double feedback = params[QStringLiteral("feedback")].toDouble(0.35);
        double mix = params[QStringLiteral("mix")].toDouble(0.4);
        bool bypass = bypassed(params);
        return runAudioEffectDialog(parent, QStringLiteral("Delay / Echo"), [&](QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            auto *delaySpin = new QSpinBox;
            delaySpin->setRange(20, 2000);
            delaySpin->setSuffix(QStringLiteral(" ms"));
            delaySpin->setValue(static_cast<int>(delayMs));
            form->addRow(QStringLiteral("Delay:"), delaySpin);
            addPercentSlider(form, QStringLiteral("Feedback:"), feedback, 0.0, 0.95);
            addPercentSlider(form, QStringLiteral("Mix:"), mix, 0.0, 1.0);
            auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
            bypassBox->setChecked(bypass);
            layout->addLayout(form);
            layout->addWidget(bypassBox);
            QObject::connect(delaySpin, QOverload<int>::of(&QSpinBox::valueChanged), [&delayMs](int v) { delayMs = v; });
            QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass](bool on) { bypass = on; });
        }) && (params = QJsonObject{
                   {QStringLiteral("delayMs"), delayMs},
                   {QStringLiteral("feedback"), feedback},
                   {QStringLiteral("mix"), mix},
                   {QStringLiteral("bypass"), bypass},
               }, true);
    };
    return d;
}

AudioEffectDescriptor makeReverb() {
    AudioEffectDescriptor d;
    d.id = 8;
    d.name = QStringLiteral("REVERB");
    d.menuLabel = QStringLiteral("Reverb");
    d.defaultParams = QJsonObject{
        {QStringLiteral("room"), 0.5},
        {QStringLiteral("mix"), 0.3},
    };
    d.filterSpec = [](const QJsonObject &p) {
        if (bypassed(p)) return QString();
        const double room = p[QStringLiteral("room")].toDouble(0.5);
        const double mix = p[QStringLiteral("mix")].toDouble(0.3);
        const int d1 = static_cast<int>(40 + room * 80);
        const int d2 = static_cast<int>(90 + room * 160);
        const int d3 = static_cast<int>(150 + room * 250);
        const double decay = 0.25 + room * 0.35;
        return QStringLiteral("aecho=in_gain=%1:out_gain=%2:delays=%3|%4|%5:decays=%6|%7|%8")
            .arg(1.0 - mix * 0.4, 0, 'f', 2)
            .arg(mix, 0, 'f', 2)
            .arg(d1).arg(d2).arg(d3)
            .arg(decay, 0, 'f', 2)
            .arg(decay * 0.7, 0, 'f', 2)
            .arg(decay * 0.45, 0, 'f', 2);
    };
    d.editLabel = QStringLiteral("Edit Reverb");
    d.dynamicLabel = [](const QJsonObject &p) {
        return QStringLiteral("room %1%")
            .arg(static_cast<int>(p[QStringLiteral("room")].toDouble(0.5) * 100.0));
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &) {
        double room = params[QStringLiteral("room")].toDouble(0.5);
        double mix = params[QStringLiteral("mix")].toDouble(0.3);
        bool bypass = bypassed(params);
        return runAudioEffectDialog(parent, QStringLiteral("Reverb"), [&](QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            addPercentSlider(form, QStringLiteral("Room size:"), room, 0.0, 1.0);
            addPercentSlider(form, QStringLiteral("Mix:"), mix, 0.0, 1.0);
            auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
            bypassBox->setChecked(bypass);
            layout->addLayout(form);
            layout->addWidget(bypassBox);
            QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass](bool on) { bypass = on; });
        }) && (params = QJsonObject{
                   {QStringLiteral("room"), room},
                   {QStringLiteral("mix"), mix},
                   {QStringLiteral("bypass"), bypass},
               }, true);
    };
    return d;
}

AudioEffectDescriptor makeStereoWiden() {
    AudioEffectDescriptor d;
    d.id = 9;
    d.name = QStringLiteral("STEREO");
    d.menuLabel = QStringLiteral("Stereo Widen");
    d.defaultParams = QJsonObject{{QStringLiteral("amount"), 1.8}};
    d.filterSpec = [](const QJsonObject &p) {
        if (bypassed(p)) return QString();
        return QStringLiteral("extrastereo=m=%1")
            .arg(p[QStringLiteral("amount")].toDouble(1.8), 0, 'f', 2);
    };
    d.editLabel = QStringLiteral("Edit Stereo");
    d.dynamicLabel = [](const QJsonObject &p) {
        return QStringLiteral("x%1")
            .arg(p[QStringLiteral("amount")].toDouble(1.8), 0, 'f', 1);
    };
    d.editDialog = [](QWidget *parent, QJsonObject &params, const AudioEffectLiveUpdate &) {
        double amount = params[QStringLiteral("amount")].toDouble(1.8);
        bool bypass = bypassed(params);
        return runAudioEffectDialog(parent, QStringLiteral("Stereo Widen"), [&](QVBoxLayout *layout) {
            auto *form = new QFormLayout;
            addPercentSlider(form, QStringLiteral("Width:"), amount, 0.5, 4.0);
            auto *bypassBox = new QCheckBox(QStringLiteral("Bypass"));
            bypassBox->setChecked(bypass);
            layout->addLayout(form);
            layout->addWidget(bypassBox);
            QObject::connect(bypassBox, &QCheckBox::toggled, [&bypass](bool on) { bypass = on; });
        }) && (params = QJsonObject{
                   {QStringLiteral("amount"), amount},
                   {QStringLiteral("bypass"), bypass},
               }, true);
    };
    return d;
}

} // namespace

namespace AudioEffects {

const QVector<AudioEffectDescriptor> &all() {
    static const QVector<AudioEffectDescriptor> registry{
        makeGain(),
        makeHighPass(),
        makeLowPass(),
        makeEq3Band(),
        makeCompressor(),
        makeLimiter(),
        makeGate(),
        makeDelay(),
        makeReverb(),
        makeStereoWiden(),
    };
    return registry;
}

const AudioEffectDescriptor *byId(int id) {
    for (const AudioEffectDescriptor &d : all())
        if (d.id == id) return &d;
    return nullptr;
}

QString buildFilterChain(const QVector<AudioEffectRef> &effects) {
    QStringList parts;
    for (const AudioEffectRef &ref : effects) {
        const AudioEffectDescriptor *d = byId(ref.effectId);
        if (!d) continue;
        const QString spec = d->filterSpec(ref.params);
        if (!spec.isEmpty())
            parts << spec;
    }
    return parts.join(QLatin1Char(','));
}

QString effectsKey(const QVector<AudioEffectRef> &effects) {
    QStringList keys;
    for (const AudioEffectRef &ref : effects) {
        const AudioEffectDescriptor *d = byId(ref.effectId);
        keys << QStringLiteral("%1:%2")
                    .arg(ref.effectId)
                    .arg(QString::fromUtf8(QJsonDocument(ref.params).toJson(QJsonDocument::Compact)));
        if (d) (void)d;
    }
    return keys.join(QLatin1Char('|'));
}

} // namespace AudioEffects
