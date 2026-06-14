#pragma once

#include <QMainWindow>
#include <QPushButton>
#include "VideoWidget.h"

class OutputWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit OutputWindow(QWidget *parent = nullptr);
    ~OutputWindow();

    VideoWidget *videoWidget() const { return outputWidget; }

private:
    VideoWidget *outputWidget = nullptr;
    QPushButton *fullscreenBtn = nullptr;

    void createUI();

private slots:
    void onFullscreenClicked();
};
