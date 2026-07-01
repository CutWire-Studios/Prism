#pragma once

#include <QDialog>
#include <QTimer>
#include <QColor>
#include "core/project/OverlayItem.h"

namespace Ui { class ClipEditDialog; }

/// Per-clip settings editor (trim, crop, transform, audio, …). Edits a copy of
/// ClipSettings and returns the result via resultSettings().
class ClipEditDialog : public QDialog {
    Q_OBJECT
public:
    ClipEditDialog(const QString &clipPath, const ClipSettings &settings,
                   QWidget *parent = nullptr);
    ~ClipEditDialog();

    ClipSettings resultSettings() const;

    // Call before exec() to remove the Trim tab (e.g. for static images).
    void hideTrimTab();
    // Call before exec() to remove the Crop tab (crop now lives on Process nodes).
    void hideCropTab();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    // ── Trim tab ──────────────────────────────────────────────────────────
    void onPlayPauseClicked();
    void onProgressSliderMoved(int value);
    void onProgressSliderReleased();
    void onTimestampEditFinished();
    void onSetStart();
    void onSetEnd();
    void onPollTimer();

    // ── Crop tab ──────────────────────────────────────────────────────────
    void onCropSelectorChanged(float x, float y, float w, float h);
    void onCropSpinChanged();
    void onResetCrop();
    void onCropPreviewToggled(bool checked);
    void onTabChanged(int index);

private:
    Ui::ClipEditDialog *ui;
    QTimer *pollTimer;

    QString       m_clipPath;
    double        m_startTime;
    double        m_endTime;
    double        m_duration = 0.0;
    bool          m_videoLoaded = false;
    bool          m_sliderDragging = false;

    float         m_cropX = 0.f, m_cropY = 0.f, m_cropW = 1.f, m_cropH = 1.f;
    bool          m_cropSpinChanging = false;

    // ── Trim helpers ──────────────────────────────────────────────────────
    void seekRelative(double delta);
    void seekTo(double secs, bool updateSlider = true);
    void updateSelectionLabels();
    void setStatus(const QString &msg, bool error = true);
    QString formatTime(double secs) const;
    double  parseTime(const QString &str, bool *ok = nullptr) const;
    double  clampTime(double t) const;
    int     toSliderVal(double secs) const;
    double  fromSliderVal(int val) const;

    // ── Crop helpers ──────────────────────────────────────────────────────
    void syncCropSpinsFromValues();
    void applyCropToPreview();
};
