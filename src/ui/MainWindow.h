#pragma once

#include <QMainWindow>
#include <QSlider>
#include <QPushButton>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>
#include "VideoWidget.h"
#include "ClipCard.h"
#include "OutputWindow.h"
#include "../core/ClipManager.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onClipGridClicked(int index);
    void onLoadFolderClicked();
    void onCrossfaderMoved(int value);
    void onADeckPlayClicked();
    void onBDeckPlayClicked();
    void onADeckSpeedChanged(int value);
    void onBDeckSpeedChanged(int value);
    void onTimerUpdate();
    void onAButtonClicked(int index);
    void onBButtonClicked(int index);

private:
    static constexpr int CARD_WIDTH = 122;
    static constexpr int CARD_HEIGHT = 176;
    static constexpr int MIN_COLS = 2;
    int dynamicCols = 8;

    OutputWindow *outputWindow = nullptr;
    ClipCard *clipCards[512];
    QGridLayout *gridLayout = nullptr;

    QPushButton *aDeckPlayBtn = nullptr;
    QPushButton *bDeckPlayBtn = nullptr;
    QSpinBox *aDeckSpeedSpinBox = nullptr;
    QSpinBox *bDeckSpeedSpinBox = nullptr;
    QSlider *crossfaderSlider = nullptr;
    QPushButton *loadFolderBtn = nullptr;
    QLabel *aPreviewLabel = nullptr;
    QLabel *bPreviewLabel = nullptr;
    QLabel *aSelectedLabel = nullptr;
    QLabel *bSelectedLabel = nullptr;
    QSlider *aProgressSlider = nullptr;
    QSlider *bProgressSlider = nullptr;
    QLabel *aTimeLabel = nullptr;
    QLabel *bTimeLabel = nullptr;

    bool m_aSliderDragging = false;
    bool m_bSliderDragging = false;

    ClipManager clipManager;
    QTimer *updateTimer = nullptr;
    int selectedClipIndex = -1;
    int aClipIndex = -1;
    int bClipIndex = -1;

    void createUI();
    void setupConnections();
    void createControlPanel();
    void applyTheme();
    void updateGridLayout();

    static QString formatTimeShort(double secs);
};
