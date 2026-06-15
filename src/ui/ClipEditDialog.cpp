#include "ui/ClipEditDialog.h"
#include "ui_ClipEditDialog.h"
#include "core/VideoPlayer.h"
#include "ui/OverlayCanvasWidget.h"
#include <QShowEvent>
#include <QColorDialog>
#include <QFileDialog>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────

ClipEditDialog::ClipEditDialog(const QString &clipPath, const ClipSettings &settings,
                               QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ClipEditDialog)
    , m_clipPath(clipPath)
    , m_startTime(settings.startTime)
    , m_endTime(settings.endTime)
    , m_cropX(settings.cropX), m_cropY(settings.cropY)
    , m_cropW(settings.cropW), m_cropH(settings.cropH)
    , m_overlays(settings.overlays)
{
    ui->setupUi(this);
    setWindowTitle("Edit Clip — " + clipPath.section('/', -1));

    // Probe duration cheaply
    {
        VideoPlayer tmp;
        if (tmp.open(clipPath))
            m_duration = tmp.getDuration();
    }
    if (m_duration <= 0.0) m_duration = 1.0;
    if (m_endTime < 0.0 || m_endTime > m_duration) m_endTime = m_duration;
    m_startTime = clampTime(m_startTime);

    // ── Trim tab setup ────────────────────────────────────────────────────
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

    // ── Crop tab setup ────────────────────────────────────────────────────
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

    // ── Overlays tab setup ────────────────────────────────────────────────
    ui->overlayCanvas->setOverlays(m_overlays);
    setPropsEnabled(false);

    connect(ui->addTextBtn,    &QPushButton::clicked, this, &ClipEditDialog::onAddTextOverlay);
    connect(ui->addImageBtn,   &QPushButton::clicked, this, &ClipEditDialog::onAddImageOverlay);
    connect(ui->removeOverlayBtn, &QPushButton::clicked, this, &ClipEditDialog::onRemoveOverlay);

    connect(ui->overlayCanvas, &OverlayCanvasWidget::overlaySelected,
            this, &ClipEditDialog::onCanvasOverlaySelected);
    connect(ui->overlayCanvas, &OverlayCanvasWidget::overlayChanged,
            this, &ClipEditDialog::onCanvasOverlayChanged);

    connect(ui->ovContentEdit, &QLineEdit::textChanged,
            this, &ClipEditDialog::onOverlayContentChanged);
    connect(ui->ovImageBtn,   &QPushButton::clicked,
            this, &ClipEditDialog::onOverlayImageBrowse);
    connect(ui->ovColorBtn,   &QPushButton::clicked,
            this, &ClipEditDialog::onOverlayColorClicked);
    connect(ui->ovOpacitySlider, &QSlider::valueChanged,
            this, &ClipEditDialog::onOverlayOpacityChanged);
    connect(ui->ovFontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ClipEditDialog::onOverlayFontSizeChanged);
    connect(ui->ovVisibleCheck, &QCheckBox::toggled,
            this, &ClipEditDialog::onOverlayVisibilityChanged);
}

ClipEditDialog::~ClipEditDialog() {
    delete ui;
}

void ClipEditDialog::hideTrimTab() {
    // Tab 0 is the Trim tab — remove it so only Crop and Overlays remain.
    ui->tabWidget->removeTab(0);
    setWindowTitle("Edit — " + m_clipPath.section('/', -1));
}

ClipSettings ClipEditDialog::resultSettings() const {
    ClipSettings s;
    s.startTime = m_startTime;
    s.endTime   = m_endTime;
    s.cropX = m_cropX; s.cropY = m_cropY;
    s.cropW = m_cropW; s.cropH = m_cropH;
    s.overlays = m_overlays;
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// Show event — load video into preview
// ─────────────────────────────────────────────────────────────────────────────

void ClipEditDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (!m_videoLoaded) {
        m_videoLoaded = true;
        QTimer::singleShot(60, this, [this]() {
            ui->preview->loadVideo(m_clipPath);
            ui->preview->setOverlaysA(m_overlays);
            ui->overlayCanvas->setOverlays(m_overlays);
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
        ui->preview->play();            // auto-play so crop/overlay tabs show live video
        ui->playPauseBtn->setText("⏸");
        pollTimer->start();
        });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Trim tab slots
// ─────────────────────────────────────────────────────────────────────────────

void ClipEditDialog::onPlayPauseClicked() {
    if (!m_videoLoaded) return;
    if (ui->preview->isPlaying()) {
        ui->preview->pause();
        ui->playPauseBtn->setText("▶");
    } else {
        ui->preview->play();
        ui->playPauseBtn->setText("⏸");
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
    ui->playPauseBtn->setText(ui->preview->isPlaying() ? "⏸" : "▶");
    double t = ui->preview->getCurrentTime();
    if (!m_sliderDragging) {
        ui->progressSlider->blockSignals(true);
        ui->progressSlider->setValue(toSliderVal(t));
        ui->progressSlider->blockSignals(false);
    }
    if (!ui->timestampEdit->hasFocus())
        ui->timestampEdit->setText(formatTime(t));

    // Always feed the latest frame into the crop selector so the user
    // sees live video (or the current paused frame) while cropping/adding overlays.
    QWidget *cur = ui->tabWidget->currentWidget();
    if (cur == ui->cropTab || cur == ui->overlaysTab) {
        QImage frame = ui->preview->getFrameA();
        if (!frame.isNull()) {
            ui->cropSelector->setFrame(frame);
            ui->overlayCanvas->setFrame(frame);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Crop tab slots
// ─────────────────────────────────────────────────────────────────────────────

void ClipEditDialog::onTabChanged(int index) {
    QWidget *tab = ui->tabWidget->widget(index);
    if (!m_videoLoaded) return;

    if (tab == ui->cropTab) {
        // Grab current frame for the crop selector
        QImage frame = ui->preview->getFrameA();
        if (!frame.isNull())
            ui->cropSelector->setFrame(frame);
        // Sync selector to current crop values
        ui->cropSelector->blockSignals(true);
        ui->cropSelector->setCrop(m_cropX, m_cropY, m_cropW, m_cropH);
        ui->cropSelector->blockSignals(false);
        // Apply crop to the preview VideoWidget above the tabs
        applyCropToPreview();
    } else if (tab == ui->trimTab) {
        // Remove crop from preview when back on Trim tab so scrubbing
        // shows the full frame (makes trimming easier).
        ui->preview->setCropA(0.f, 0.f, 1.f, 1.f);
    } else if (tab == ui->overlaysTab) {
        // Show crop on preview while editing overlays so the framing is correct.
        applyCropToPreview();
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
    // Sync selector without triggering signals
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
        ui->preview->update();   // force immediate repaint
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlays tab slots
// ─────────────────────────────────────────────────────────────────────────────

void ClipEditDialog::onAddTextOverlay() {
    OverlayItem ov;
    ov.type    = OverlayItem::Type::Text;
    ov.content = "New Text";
    ov.x = 0.25f; ov.y = 0.82f; ov.w = 0.5f; ov.h = 0.10f;
    m_overlays.append(ov);
    m_selectedOverlayIdx = m_overlays.size() - 1;
    updateOverlayCanvas();
    populatePropsFromOverlay(m_selectedOverlayIdx);
    setPropsEnabled(true);
    updateSelectedLabel();
}

void ClipEditDialog::onAddImageOverlay() {
    QString path = QFileDialog::getOpenFileName(this, "Select Image",
        m_clipPath.section('/', 0, -2),
        "Images (*.png *.jpg *.jpeg *.bmp *.gif *.svg *.webp)");
    if (path.isEmpty()) return;
    OverlayItem ov;
    ov.type    = OverlayItem::Type::Image;
    ov.content = path;
    ov.x = 0.75f; ov.y = 0.04f; ov.w = 0.20f; ov.h = 0.18f;
    m_overlays.append(ov);
    m_selectedOverlayIdx = m_overlays.size() - 1;
    updateOverlayCanvas();
    populatePropsFromOverlay(m_selectedOverlayIdx);
    setPropsEnabled(true);
    updateSelectedLabel();
}

void ClipEditDialog::onRemoveOverlay() {
    if (m_selectedOverlayIdx < 0 || m_selectedOverlayIdx >= m_overlays.size()) return;
    m_overlays.removeAt(m_selectedOverlayIdx);
    m_selectedOverlayIdx = -1;
    updateOverlayCanvas();
    setPropsEnabled(false);
    updateSelectedLabel();
    if (m_videoLoaded) ui->preview->setOverlaysA(m_overlays);
}

void ClipEditDialog::onCanvasOverlaySelected(int index) {
    m_selectedOverlayIdx = index;
    if (index >= 0 && index < m_overlays.size()) {
        populatePropsFromOverlay(index);
        setPropsEnabled(true);
    } else {
        setPropsEnabled(false);
    }
    updateSelectedLabel();
}

void ClipEditDialog::onCanvasOverlayChanged(int index, const OverlayItem &item) {
    if (index < 0 || index >= m_overlays.size()) return;
    m_overlays[index] = item;
    if (m_videoLoaded) ui->preview->setOverlaysA(m_overlays);
}

void ClipEditDialog::onOverlayContentChanged() {
    if (m_overlayUpdating) return;
    if (m_selectedOverlayIdx < 0 || m_selectedOverlayIdx >= m_overlays.size()) return;
    m_overlays[m_selectedOverlayIdx].content = ui->ovContentEdit->text();
    updateOverlayCanvas();
    updateSelectedLabel();
    if (m_videoLoaded) ui->preview->setOverlaysA(m_overlays);
}

void ClipEditDialog::onOverlayImageBrowse() {
    QString path = QFileDialog::getOpenFileName(this, "Select Image",
        m_clipPath.section('/', 0, -2),
        "Images (*.png *.jpg *.jpeg *.bmp *.gif *.svg *.webp)");
    if (path.isEmpty()) return;
    if (m_selectedOverlayIdx < 0 || m_selectedOverlayIdx >= m_overlays.size()) return;
    m_overlays[m_selectedOverlayIdx].content = path;
    m_overlayUpdating = true;
    ui->ovContentEdit->setText(path);
    m_overlayUpdating = false;
    updateOverlayCanvas();
    updateSelectedLabel();
    if (m_videoLoaded) ui->preview->setOverlaysA(m_overlays);
}

void ClipEditDialog::onOverlayOpacityChanged(int value) {
    if (m_overlayUpdating) return;
    if (m_selectedOverlayIdx < 0 || m_selectedOverlayIdx >= m_overlays.size()) return;
    m_overlays[m_selectedOverlayIdx].opacity = value / 100.f;
    ui->ovOpacityLabel->setText(QString::number(value) + "%");
    updateOverlayCanvas();
    if (m_videoLoaded) ui->preview->setOverlaysA(m_overlays);
}

void ClipEditDialog::onOverlayColorClicked() {
    QColor c = QColorDialog::getColor(m_currentOverlayColor, this, "Pick Text Color",
                                      QColorDialog::ShowAlphaChannel);
    if (!c.isValid()) return;
    m_currentOverlayColor = c;
    updateColorButton(c);
    if (m_selectedOverlayIdx < 0 || m_selectedOverlayIdx >= m_overlays.size()) return;
    m_overlays[m_selectedOverlayIdx].color = c;
    updateOverlayCanvas();
    if (m_videoLoaded) ui->preview->setOverlaysA(m_overlays);
}

void ClipEditDialog::onOverlayFontSizeChanged(int value) {
    if (m_overlayUpdating) return;
    if (m_selectedOverlayIdx < 0 || m_selectedOverlayIdx >= m_overlays.size()) return;
    m_overlays[m_selectedOverlayIdx].fontSize = value;
    updateOverlayCanvas();
    if (m_videoLoaded) ui->preview->setOverlaysA(m_overlays);
}

void ClipEditDialog::onOverlayVisibilityChanged(bool checked) {
    if (m_overlayUpdating) return;
    if (m_selectedOverlayIdx < 0 || m_selectedOverlayIdx >= m_overlays.size()) return;
    m_overlays[m_selectedOverlayIdx].visible = checked;
    updateOverlayCanvas();
    if (m_videoLoaded) ui->preview->setOverlaysA(m_overlays);
}

// ── Overlay helpers ───────────────────────────────────────────────────────────

void ClipEditDialog::populatePropsFromOverlay(int idx) {
    if (idx < 0 || idx >= m_overlays.size()) return;
    m_overlayUpdating = true;
    const OverlayItem &ov = m_overlays[idx];
    bool isText = (ov.type == OverlayItem::Type::Text);
    ui->ovContentEdit->setText(ov.content);
    ui->ovContentEdit->setPlaceholderText(isText ? "Type your text here…" : "Image file path…");
    ui->ovImageBtn->setVisible(!isText);
    ui->ovColorBtn->setVisible(isText);
    ui->ovColorHeadLabel->setVisible(isText);
    ui->ovFontSizeLabel->setVisible(isText);
    ui->ovFontSizeSpin->setVisible(isText);
    ui->ovOpacitySlider->setValue(static_cast<int>(ov.opacity * 100));
    ui->ovOpacityLabel->setText(QString::number(static_cast<int>(ov.opacity * 100)) + "%");
    ui->ovFontSizeSpin->setValue(ov.fontSize);
    ui->ovVisibleCheck->setChecked(ov.visible);
    m_currentOverlayColor = ov.color;
    updateColorButton(ov.color);
    m_overlayUpdating = false;
}

void ClipEditDialog::updateOverlayCanvas() {
    ui->overlayCanvas->setOverlays(m_overlays);
    ui->overlayCanvas->setSelectedIndex(m_selectedOverlayIdx);
}

void ClipEditDialog::setPropsEnabled(bool enabled) {
    ui->overlayPropsGroup->setEnabled(enabled);
    ui->removeOverlayBtn->setEnabled(enabled);
}

void ClipEditDialog::updateSelectedLabel() {
    if (m_selectedOverlayIdx >= 0 && m_selectedOverlayIdx < m_overlays.size())
        ui->selectedOverlayLabel->setText(m_overlays[m_selectedOverlayIdx].displayName());
    else
        ui->selectedOverlayLabel->setText("No overlay selected");
}

void ClipEditDialog::updateColorButton(const QColor &c) {
    ui->ovColorBtn->setStyleSheet(
        QString("background-color:%1;color:%2;border:1px solid #444;padding:2px 8px;")
        .arg(c.name()).arg(c.lightness() > 128 ? "#000" : "#fff"));
    ui->ovColorBtn->setText(c.name().toUpper());
}

// ─────────────────────────────────────────────────────────────────────────────
// Trim private helpers (unchanged)
// ─────────────────────────────────────────────────────────────────────────────

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
