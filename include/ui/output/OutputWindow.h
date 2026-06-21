#pragma once

#include <QMainWindow>
#include <QPoint>

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

    VideoWidget *videoWidget() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void toggleFullscreen();
    void showContextMenu(const QPoint &globalPos);

    Ui::OutputWindow *ui;
};
