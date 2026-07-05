#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QRect>

namespace Ui { class OutputWindow; }
class VideoWidget;

/// Detached program-output window hosting a VideoWidget that mirrors the live
/// program feed (for a second monitor / projector).
class OutputWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit OutputWindow(QWidget *parent = nullptr);
    ~OutputWindow();

    void setRecordingActive(bool active);
    void setStayOnTop(bool on);

    VideoWidget *videoWidget() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void toggleFullscreen();
    void enterFullscreen();
    void exitFullscreen();
    bool isFullscreenActive() const;
    void showContextMenu(const QPoint &globalPos);

    Ui::OutputWindow *ui;
    // Frameless windows don't reliably reach true fullscreen via showFullScreen()
    // (macOS menu bar / notch, Windows taskbar gaps), so snap to screen geometry
    // and track state ourselves — isFullScreen() can't be trusted here.
    bool  m_fullscreen = false;
    QRect m_normalGeometry;
};
