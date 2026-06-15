#pragma once

#include <QDialog>
#include <QTimer>
#include <QColor>
#include "core/OverlayItem.h"

namespace Ui { class ClipEditDialog; }

class ClipEditDialog : public QDialog {
    Q_OBJECT
public:
    ClipEditDialog(const QString &clipPath, const ClipSettings &settings,
                   QWidget *parent = nullptr);
    ~ClipEditDialog();

    ClipSettings resultSettings() const;

    // Call before exec() to remove the Trim tab (e.g. for static images).
    void hideTrimTab();

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

    // ── Base placement tab ────────────────────────────────────────────────
    void onBasePlacementChanged(float x, float y, float w, float h);
    void onResetBasePlacement();

    // ── Overlays tab ──────────────────────────────────────────────────────
    void onAddTextOverlay();
    void onAddImageOverlay();
    void onRemoveOverlay();
    void onCanvasOverlaySelected(int index);
    void onCanvasOverlayChanged(int index, const OverlayItem &item);
    void onOverlayContentChanged();
    void onOverlayImageBrowse();
    void onOverlayOpacityChanged(int value);
    void onOverlayColorClicked();
    void onOverlayFontSizeChanged(int value);
    void onOverlayVisibilityChanged(bool checked);

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

    float         m_baseX = 0.f, m_baseY = 0.f, m_baseW = 1.f, m_baseH = 1.f;

    QList<OverlayItem> m_overlays;
    int                m_selectedOverlayIdx = -1;
    QColor             m_currentOverlayColor = Qt::white;
    bool               m_overlayUpdating = false;

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

    // ── Base placement helpers ────────────────────────────────────────────
    void syncBasePlacementFromValues();
    void applyBaseToPreview();

    // ── Overlay helpers ───────────────────────────────────────────────────
    void populatePropsFromOverlay(int index);
    void updateOverlayCanvas();
    void setPropsEnabled(bool enabled);
    void updateColorButton(const QColor &c);
    void updateSelectedLabel();
};
