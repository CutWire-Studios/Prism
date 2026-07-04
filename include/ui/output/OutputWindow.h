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

private:
    void toggleFullscreen();
    void enterFullscreen();
    void exitFullscreen();
    bool isFullscreenActive() const;
    void showContextMenu(const QPoint &globalPos);

    Ui::OutputWindow *ui;
    // macOS: showFullScreen() on a frameless window leaves the menu-bar strip
    // uncovered, so fullscreen is done by snapping to the screen geometry. That
    // means isFullScreen() can't be trusted; track it and the pre-fullscreen
    // geometry ourselves.
    bool  m_fullscreen = false;
    QRect m_normalGeometry;
};
