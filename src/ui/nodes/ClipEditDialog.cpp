#include "ui/nodes/ClipEditDialog.h"
#include "ui_ClipEditDialog.h"
#include "ui/common/MaterialSymbols.h"
#include "core/media/VideoPlayer.h"
#include <QPushButton>
#include <QShowEvent>
#include <cmath>

ClipEditDialog::ClipEditDialog(const QString &clipPath, const ClipSettings &settings,
                               QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ClipEditDialog)
    , m_clipPath(clipPath)
    , m_startTime(settings.startTime)
    , m_endTime(settings.endTime)
    , m_cropX(settings.cropX), m_cropY(settings.cropY)
    , m_cropW(settings.cropW), m_cropH(settings.cropH)
{
    ui->setupUi(this);
    ui->preview->addDeckPreviewConsumer();
    setWindowTitle("Edit Clip — " + clipPath.section('/', -1));

    ui->tabWidget->setTabIcon(0, MaterialSymbols::icon(MaterialSymbols::Names::ContentCut, 16));
    MaterialSymbols::setPlayPause(ui->playPauseBtn, false, 18);

    auto setupSeekBack = [](QPushButton *btn, const QString &label) {
        btn->setText(label);
        btn->setIcon(MaterialSymbols::icon(MaterialSymbols::Names::SkipPrevious, 12));
    };
    auto setupSeekForward = [](QPushButton *btn, const QString &label) {
        btn->setText(label);
        btn->setIcon(MaterialSymbols::icon(MaterialSymbols::Names::SkipNext, 12));
        btn->setLayoutDirection(Qt::RightToLeft);
    };
    setupSeekBack(ui->btn10sBack, "10s");
    setupSeekBack(ui->btn5sBack, "5s");
    setupSeekBack(ui->btn1sBack, "1s");
    setupSeekBack(ui->btn01sBack, "0.1");
    setupSeekForward(ui->btn01sForward, "0.1");
    setupSeekForward(ui->btn1sForward, "1s");
    setupSeekForward(ui->btn5sForward, "5s");
    setupSeekForward(ui->btn10sForward, "10s");

    VideoPlayer tmp;
    if (tmp.open(clipPath))
        m_duration = tmp.getDuration();
    if (m_duration <= 0.0) m_duration = 1.0;
    if (m_endTime < 0.0 || m_endTime > m_duration) m_endTime = m_duration;
    m_startTime = clampTime(m_startTime);

    ui->progressSlider->setRange(0, toSliderVal(m_duration));
    ui->progressSlider->setValue(toSliderVal(m_startTime));
    ui->durationLabel->setText("/ " + formatTime(m_duration));
    ui->timestampEdit->setText(formatTime(m_startTime));
    ui->totalDurationLabel->setText("/ " + formatTime(m_duration));
    updateSelectionLabels();

    pollTimer = new QTimer(this);
    pollTimer->setInterval(80);
    connect(pollTimer, &QTimer::timeout, this, &ClipEditDialog::onPollTimer);

    connect(ui->playPauseBtn,  &QPushButton::clicked, this, &ClipEditDialog::onPlayPauseClicked);
    connect(ui->setStartBtn,   &QPushButton::clicked, this, &ClipEditDialog::onSetStart);
    connect(ui->setEndBtn,     &QPushButton::clicked, this, &ClipEditDialog::onSetEnd);

    connect(ui->btn10sBack,    &QPushButton::clicked, this, [this]() { seekRelative(-10.0); });
    connect(ui->btn5sBack,     &QPushButton::clicked, this, [this]() { seekRelative(-5.0); });
    connect(ui->btn1sBack,     &QPushButton::clicked, this, [this]() { seekRelative(-1.0); });
    connect(ui->btn01sBack,    &QPushButton::clicked, this, [this]() { seekRelative(-0.1); });
    connect(ui->btn01sForward, &QPushButton::clicked, this, [this]() { seekRelative(0.1); });
    connect(ui->btn1sForward,  &QPushButton::clicked, this, [this]() { seekRelative(1.0); });
    connect(ui->btn5sForward,  &QPushButton::clicked, this, [this]() { seekRelative(5.0); });
    connect(ui->btn10sForward, &QPushButton::clicked, this, [this]() { seekRelative(10.0); });

    connect(ui->progressSlider, &QSlider::sliderPressed,  this, [this]() { m_sliderDragging = true; });
    connect(ui->progressSlider, &QSlider::sliderMoved,    this, &ClipEditDialog::onProgressSliderMoved);
    connect(ui->progressSlider, &QSlider::sliderReleased, this, &ClipEditDialog::onProgressSliderReleased);
    connect(ui->timestampEdit,  &QLineEdit::editingFinished, this, &ClipEditDialog::onTimestampEditFinished);

    syncCropSpinsFromValues();

    connect(ui->cropSelector, &CropSelectorWidget::cropChanged,
            this, &ClipEditDialog::onCropSelectorChanged);
    connect(ui->cropXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ClipEditDialog::onCropSpinChanged);
    connect(ui->cropYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ClipEditDialog::onCropSpinChanged);
    connect(ui->cropWSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ClipEditDialog::onCropSpinChanged);
    connect(ui->cropHSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ClipEditDialog::onCropSpinChanged);
    connect(ui->resetCropBtn,    &QPushButton::clicked, this, &ClipEditDialog::onResetCrop);
    connect(ui->cropPreviewCheck,&QCheckBox::toggled,   this, &ClipEditDialog::onCropPreviewToggled);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &ClipEditDialog::onTabChanged);
}

ClipEditDialog::~ClipEditDialog() {
    delete ui;
}

void ClipEditDialog::hideTrimTab() {
    ui->tabWidget->removeTab(0);
    setWindowTitle("Edit — " + m_clipPath.section('/', -1));
}

ClipSettings ClipEditDialog::resultSettings() const {
    ClipSettings s;
    s.startTime = m_startTime;
    s.endTime   = m_endTime;
    s.cropX = m_cropX; s.cropY = m_cropY;
    s.cropW = m_cropW; s.cropH = m_cropH;
    return s;
}

void ClipEditDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (!m_videoLoaded) {
        m_videoLoaded = true;
        QTimer::singleShot(60, this, [this]() {
            ui->preview->loadVideo(m_clipPath);
            applyCropToPreview();

            double d = ui->preview->getDuration();
            if (d > 0.0 && std::fabs(d - m_duration) > 0.5) {
                m_duration = d;
                ui->progressSlider->setRange(0, toSliderVal(m_duration));
                ui->durationLabel->setText("/ " + formatTime(m_duration));
                ui->totalDurationLabel->setText("/ " + formatTime(m_duration));
                if (m_endTime > m_duration) { m_endTime = m_duration; updateSelectionLabels(); }
            }
            seekTo(m_startTime);
            ui->preview->play();
            MaterialSymbols::setPlayPause(ui->playPauseBtn, true, 18);
            pollTimer->start();
        });
    }
}

void ClipEditDialog::onPlayPauseClicked() {
    if (!m_videoLoaded) return;
    if (ui->preview->isPlaying()) {
        ui->preview->pause();
        MaterialSymbols::setPlayPause(ui->playPauseBtn, false, 18);
    } else {
        ui->preview->play();
        MaterialSymbols::setPlayPause(ui->playPauseBtn, true, 18);
    }
}

void ClipEditDialog::onProgressSliderMoved(int value) {
    double t = fromSliderVal(value);
    if (!ui->timestampEdit->hasFocus())
        ui->timestampEdit->setText(formatTime(t));
}

void ClipEditDialog::onProgressSliderReleased() {
    m_sliderDragging = false;
    seekTo(fromSliderVal(ui->progressSlider->value()), false);
}

void ClipEditDialog::onTimestampEditFinished() {
    bool ok = false;
    double t = parseTime(ui->timestampEdit->text(), &ok);
    if (!ok) {
        setStatus("Invalid time format — use M:SS.ss");
        ui->timestampEdit->setText(formatTime(ui->preview->getCurrentTime()));
        return;
    }
    setStatus({});
    seekTo(clampTime(t));
}

void ClipEditDialog::onSetStart() {
    double t = ui->preview->getCurrentTime();
    if (t >= m_endTime - 0.1) {
        setStatus("Start must be at least 0.1 s before end (" + formatTime(m_endTime) + ")");
        return;
    }
    m_startTime = t;
    setStatus({});
    updateSelectionLabels();
}

void ClipEditDialog::onSetEnd() {
    double t = ui->preview->getCurrentTime();
    if (t <= m_startTime + 0.1) {
        setStatus("End must be at least 0.1 s after start (" + formatTime(m_startTime) + ")");
        return;
    }
    if (t > m_duration) {
        setStatus("End cannot exceed clip duration (" + formatTime(m_duration) + ")");
        return;
    }
    m_endTime = t;
    setStatus({});
    updateSelectionLabels();
}

void ClipEditDialog::onPollTimer() {
    if (!m_videoLoaded) return;
    MaterialSymbols::setPlayPause(ui->playPauseBtn, ui->preview->isPlaying(), 18);
    double t = ui->preview->getCurrentTime();
    if (!m_sliderDragging) {
        ui->progressSlider->blockSignals(true);
        ui->progressSlider->setValue(toSliderVal(t));
        ui->progressSlider->blockSignals(false);
    }
    if (!ui->timestampEdit->hasFocus())
        ui->timestampEdit->setText(formatTime(t));

    if (ui->tabWidget->currentWidget() == ui->cropTab) {
        QImage frame = ui->preview->getFrameA();
        if (!frame.isNull())
            ui->cropSelector->setFrame(frame);
    }
}

void ClipEditDialog::onTabChanged(int index) {
    QWidget *tab = ui->tabWidget->widget(index);
    if (!m_videoLoaded) return;

    if (tab == ui->cropTab) {
        QImage frame = ui->preview->getFrameA();
        if (!frame.isNull())
            ui->cropSelector->setFrame(frame);
        ui->cropSelector->blockSignals(true);
        ui->cropSelector->setCrop(m_cropX, m_cropY, m_cropW, m_cropH);
        ui->cropSelector->blockSignals(false);
        applyCropToPreview();
    } else if (tab == ui->trimTab) {
        ui->preview->setCropA(0.f, 0.f, 1.f, 1.f);
    }
}

void ClipEditDialog::onCropSelectorChanged(float x, float y, float w, float h) {
    m_cropX = x; m_cropY = y; m_cropW = w; m_cropH = h;
    syncCropSpinsFromValues();
    applyCropToPreview();
}

void ClipEditDialog::onCropSpinChanged() {
    if (m_cropSpinChanging) return;
    m_cropX = static_cast<float>(ui->cropXSpin->value() / 100.0);
    m_cropY = static_cast<float>(ui->cropYSpin->value() / 100.0);
    m_cropW = static_cast<float>(ui->cropWSpin->value() / 100.0);
    m_cropH = static_cast<float>(ui->cropHSpin->value() / 100.0);
    ui->cropSelector->blockSignals(true);
    ui->cropSelector->setCrop(m_cropX, m_cropY, m_cropW, m_cropH);
    ui->cropSelector->blockSignals(false);
    applyCropToPreview();
}

void ClipEditDialog::onResetCrop() {
    m_cropX = 0.f; m_cropY = 0.f; m_cropW = 1.f; m_cropH = 1.f;
    syncCropSpinsFromValues();
    ui->cropSelector->blockSignals(true);
    ui->cropSelector->setCrop(0.f, 0.f, 1.f, 1.f);
    ui->cropSelector->blockSignals(false);
    applyCropToPreview();
}

void ClipEditDialog::onCropPreviewToggled(bool checked) {
    if (checked)
        applyCropToPreview();
    else
        ui->preview->setCropA(0.f, 0.f, 1.f, 1.f);
}

void ClipEditDialog::syncCropSpinsFromValues() {
    m_cropSpinChanging = true;
    ui->cropXSpin->setValue(m_cropX * 100.0);
    ui->cropYSpin->setValue(m_cropY * 100.0);
    ui->cropWSpin->setValue(m_cropW * 100.0);
    ui->cropHSpin->setValue(m_cropH * 100.0);
    m_cropSpinChanging = false;
}

void ClipEditDialog::applyCropToPreview() {
    if (ui->cropPreviewCheck->isChecked()) {
        ui->preview->setCropA(m_cropX, m_cropY, m_cropW, m_cropH);
        ui->preview->update();
    }
}

void ClipEditDialog::seekRelative(double delta) {
    if (!m_videoLoaded) return;
    seekTo(clampTime(ui->preview->getCurrentTime() + delta));
}

void ClipEditDialog::seekTo(double secs, bool updateSlider) {
    if (!m_videoLoaded) return;
    secs = clampTime(secs);
    bool was = ui->preview->isPlaying();
    if (was) ui->preview->pause();
    ui->preview->seek(secs);
    if (was) ui->preview->play();
    if (updateSlider) {
        ui->progressSlider->blockSignals(true);
        ui->progressSlider->setValue(toSliderVal(secs));
        ui->progressSlider->blockSignals(false);
    }
    if (!ui->timestampEdit->hasFocus())
        ui->timestampEdit->setText(formatTime(secs));
}

void ClipEditDialog::updateSelectionLabels() {
    ui->startTimeLabel->setText(formatTime(m_startTime));
    ui->endTimeLabel->setText(formatTime(m_endTime));
}

void ClipEditDialog::setStatus(const QString &msg, bool error) {
    ui->statusLabel->setText(msg);
    ui->statusLabel->setStyleSheet(
        msg.isEmpty() ? "font-size: 11px;"
        : error       ? "font-size: 11px; color: #e05050;"
                      : "font-size: 11px; color: #50c050;");
}

QString ClipEditDialog::formatTime(double secs) const {
    if (secs < 0) secs = 0;
    int m = static_cast<int>(secs) / 60;
    double s = secs - m * 60;
    return QString("%1:%2").arg(m).arg(s, 5, 'f', 2, QChar('0'));
}

double ClipEditDialog::parseTime(const QString &str, bool *ok) const {
    if (ok) *ok = false;
    QString s = str.trimmed();
    double result = 0.0;
    if (s.contains(':')) {
        QStringList parts = s.split(':');
        if (parts.size() != 2) return 0.0;
        bool okM, okS;
        int m = parts[0].toInt(&okM);
        double sec = parts[1].toDouble(&okS);
        if (!okM || !okS || m < 0 || sec < 0 || sec >= 60.0) return 0.0;
        result = m * 60.0 + sec;
    } else {
        bool okV;
        result = s.toDouble(&okV);
        if (!okV || result < 0) return 0.0;
    }
    if (ok) *ok = true;
    return result;
}

double ClipEditDialog::clampTime(double t) const {
    return t < 0.0 ? 0.0 : t > m_duration ? m_duration : t;
}

int    ClipEditDialog::toSliderVal(double secs) const { return static_cast<int>(secs * 100.0); }
double ClipEditDialog::fromSliderVal(int val)   const { return val / 100.0; }
