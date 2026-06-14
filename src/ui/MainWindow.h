#pragma once

#include <QMainWindow>
#include <QSlider>
#include <QPushButton>
#include <QToolButton>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>
#include "VideoWidget.h"
#include "../core/ClipManager.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onClipGridClicked(int index);
    void onLoadFolderClicked();
    void onCrossfaderMoved(int value);
    void onADeckPlayClicked();
    void onBDeckPlayClicked();
    void onADeckSpeedChanged(int value);
    void onBDeckSpeedChanged(int value);
    void onTimerUpdate();
    void onFullscreenClicked();

private:
    static const int GRID_COLS = 4;
    static const int GRID_ROWS = 4;

    VideoWidget *outputWidget = nullptr;
    QToolButton *clipButtons[GRID_COLS * GRID_ROWS];

    QPushButton *aDeckPlayBtn = nullptr;
    QPushButton *bDeckPlayBtn = nullptr;
    QSpinBox *aDeckSpeedSpinBox = nullptr;
    QSpinBox *bDeckSpeedSpinBox = nullptr;
    QSlider *crossfaderSlider = nullptr;
    QPushButton *fullscreenBtn = nullptr;
    QPushButton *loadFolderBtn = nullptr;

    ClipManager clipManager;
    QTimer *updateTimer = nullptr;
    int selectedClipIndex = -1;

    void createUI();
    void setupConnections();
    void createClipGrid();
    void createControlPanel();
    void createOutputArea();
    void applyTheme();
};
