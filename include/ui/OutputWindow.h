#pragma once

#include <QMainWindow>
#include <QPoint>

namespace Ui { class OutputWindow; }
class VideoWidget;

class OutputWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit OutputWindow(QWidget *parent = nullptr);
    ~OutputWindow();

    VideoWidget *videoWidget() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void toggleFullscreen();
    void showContextMenu(const QPoint &globalPos);

    Ui::OutputWindow *ui;
};
