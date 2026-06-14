#include "MainWindow.h"
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QTimer>
#include <QDebug>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QGroupBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("SwitchX - Live Media Trigger");
    setWindowIcon(QIcon());
    setGeometry(100, 100, 1600, 1000);

    setAcceptDrops(true);

    for (int i = 0; i < GRID_COLS * GRID_ROWS; ++i) {
        clipButtons[i] = nullptr;
    }

    createUI();
    setupConnections();

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::onTimerUpdate);
    updateTimer->start(100);

    qDebug() << "SwitchX initialized - Live Media Control Mode";
}

MainWindow::~MainWindow() = default;

void MainWindow::createUI() {
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setCentralWidget(centralWidget);

    // Top area: Grid + Output
    QWidget *topWidget = new QWidget(this);
    QHBoxLayout *topLayout = new QHBoxLayout(topWidget);
    topLayout->setContentsMargins(10, 10, 10, 10);
    topLayout->setSpacing(10);

    // Clip Grid
    QGroupBox *gridGroup = new QGroupBox("Clip Grid Launcher", this);
    QVBoxLayout *gridVLayout = new QVBoxLayout(gridGroup);

    loadFolderBtn = new QPushButton("📁 Load", this);
    loadFolderBtn->setMaximumWidth(100);
    gridVLayout->addWidget(loadFolderBtn);

    QWidget *gridWidget = new QWidget(this);
    QGridLayout *gridLayout = new QGridLayout(gridWidget);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(4);

    for (int i = 0; i < GRID_ROWS; ++i) {
        for (int j = 0; j < GRID_COLS; ++j) {
            int index = i * GRID_COLS + j;
            clipButtons[index] = new QPushButton(this);
            clipButtons[index]->setMinimumSize(76, 76);
            clipButtons[index]->setMaximumSize(76, 76);
            clipButtons[index]->setProperty("gridIndex", index);
            connect(clipButtons[index], &QPushButton::clicked, this, [this, index]() {
                onClipGridClicked(index);
            });
            gridLayout->addWidget(clipButtons[index], i, j);
        }
    }
    gridVLayout->addWidget(gridWidget, 1);
    topLayout->addWidget(gridGroup, 1);

    // Output Area
    QGroupBox *outputGroup = new QGroupBox("Output Monitor", this);
    QVBoxLayout *outputLayout = new QVBoxLayout(outputGroup);

    outputWidget = new VideoWidget(this);
    outputWidget->setMinimumHeight(300);
    outputLayout->addWidget(outputWidget);

    QHBoxLayout *outputControlsLayout = new QHBoxLayout();
    outputControlsLayout->setContentsMargins(0, 6, 0, 0);
    outputControlsLayout->setSpacing(4);
    fullscreenBtn = new QPushButton("🖵", this);
    fullscreenBtn->setMaximumWidth(50);
    outputControlsLayout->addStretch();
    outputControlsLayout->addWidget(fullscreenBtn);
    outputLayout->addLayout(outputControlsLayout);

    outputGroup->setMinimumWidth(400);
    topLayout->addWidget(outputGroup, 2);

    mainLayout->addWidget(topWidget, 1);
    mainLayout->addSpacing(10);

    // Control Panel
    createControlPanel();

    applyTheme();
}

void MainWindow::createOutputArea() {
}

void MainWindow::createClipGrid() {
}

void MainWindow::createControlPanel() {
    QGroupBox *controlGroup = new QGroupBox("Control Panel", this);
    QHBoxLayout *mainControlLayout = new QHBoxLayout(controlGroup);
    mainControlLayout->setContentsMargins(8, 8, 8, 8);
    mainControlLayout->setSpacing(12);

    QGroupBox *aDeckGroup = new QGroupBox("A Deck", this);
    QVBoxLayout *aDeckLayout = new QVBoxLayout(aDeckGroup);
    aDeckLayout->setContentsMargins(8, 8, 8, 8);
    aDeckLayout->setSpacing(6);

    aDeckPlayBtn = new QPushButton("▶", this);
    aDeckPlayBtn->setMaximumWidth(60);
    QLabel *speedLabelA = new QLabel("Speed:", this);
    speedLabelA->setStyleSheet("font-size: 10px;");

    aDeckSpeedSpinBox = new QSpinBox(this);
    aDeckSpeedSpinBox->setRange(-200, 200);
    aDeckSpeedSpinBox->setValue(100);
    aDeckSpeedSpinBox->setSuffix("%");
    aDeckSpeedSpinBox->setMaximumWidth(80);

    aDeckLayout->addWidget(speedLabelA, 0);
    aDeckLayout->addWidget(aDeckSpeedSpinBox);
    aDeckLayout->addWidget(aDeckPlayBtn);
    aDeckLayout->addStretch();
    mainControlLayout->addWidget(aDeckGroup, 1);

    QGroupBox *faderGroup = new QGroupBox("Crossfader", this);
    QVBoxLayout *faderLayout = new QVBoxLayout(faderGroup);
    faderLayout->setContentsMargins(8, 8, 8, 8);
    faderLayout->setSpacing(4);

    crossfaderSlider = new QSlider(Qt::Horizontal, this);
    crossfaderSlider->setRange(0, 100);
    crossfaderSlider->setValue(50);

    QLabel *labelA = new QLabel("A", this);
    labelA->setStyleSheet("font-size: 10px;");
    QLabel *labelB = new QLabel("B", this);
    labelB->setStyleSheet("font-size: 10px;");

    faderLayout->addWidget(labelA, 0);
    faderLayout->addWidget(crossfaderSlider);
    faderLayout->addWidget(labelB, 0);
    faderLayout->addStretch();
    mainControlLayout->addWidget(faderGroup, 1);

    QGroupBox *bDeckGroup = new QGroupBox("B Deck", this);
    QVBoxLayout *bDeckLayout = new QVBoxLayout(bDeckGroup);
    bDeckLayout->setContentsMargins(8, 8, 8, 8);
    bDeckLayout->setSpacing(6);

    bDeckPlayBtn = new QPushButton("▶", this);
    bDeckPlayBtn->setMaximumWidth(60);
    QLabel *speedLabelB = new QLabel("Speed:", this);
    speedLabelB->setStyleSheet("font-size: 10px;");

    bDeckSpeedSpinBox = new QSpinBox(this);
    bDeckSpeedSpinBox->setRange(-200, 200);
    bDeckSpeedSpinBox->setValue(100);
    bDeckSpeedSpinBox->setSuffix("%");
    bDeckSpeedSpinBox->setMaximumWidth(80);

    bDeckLayout->addWidget(speedLabelB, 0);
    bDeckLayout->addWidget(bDeckSpeedSpinBox);
    bDeckLayout->addWidget(bDeckPlayBtn);
    bDeckLayout->addStretch();
    mainControlLayout->addWidget(bDeckGroup, 1);

    QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
    if (mainLayout) {
        mainLayout->addWidget(controlGroup);
    }
}

void MainWindow::setupConnections() {
    connect(loadFolderBtn, &QPushButton::clicked, this, &MainWindow::onLoadFolderClicked);
    connect(fullscreenBtn, &QPushButton::clicked, this, &MainWindow::onFullscreenClicked);
    connect(aDeckPlayBtn, &QPushButton::clicked, this, &MainWindow::onADeckPlayClicked);
    connect(bDeckPlayBtn, &QPushButton::clicked, this, &MainWindow::onBDeckPlayClicked);
    connect(aDeckSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onADeckSpeedChanged);
    connect(bDeckSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onBDeckSpeedChanged);
    connect(crossfaderSlider, &QSlider::valueChanged, this, &MainWindow::onCrossfaderMoved);
}

void MainWindow::onClipGridClicked(int index) {
    if (selectedClipIndex == index && outputWidget->isPlaying()) {
        outputWidget->pause();
        clipButtons[index]->setText("");
    } else {
        selectedClipIndex = index;
        for (int i = 0; i < GRID_COLS * GRID_ROWS; ++i) {
            clipButtons[i]->setStyleSheet("");
        }

        if (index < clipManager.getClipCount()) {
            QString clipPath = clipManager.getClipPath(index);
            if (!clipPath.isEmpty()) {
                outputWidget->loadVideo(clipPath);
                outputWidget->play();
                clipButtons[index]->setText("✓");
            }
        }
    }
}

void MainWindow::onLoadFolderClicked() {
    QString folderPath = QFileDialog::getExistingDirectory(this, "Select Media Folder", "");
    if (!folderPath.isEmpty()) {
        clipManager.loadFolder(folderPath);
        for (int i = 0; i < GRID_COLS * GRID_ROWS && i < clipManager.getClipCount(); ++i) {
            QString clipPath = clipManager.getClipPath(i);
            QFileInfo info(clipPath);
            QString fileName = info.baseName();
            if (fileName.length() > 8) {
                fileName = fileName.left(8) + ".";
            }
            clipButtons[i]->setText(fileName);
            clipButtons[i]->setEnabled(true);
        }
        qDebug() << "Loaded folder with" << clipManager.getClipCount() << "clips";
    }
}

void MainWindow::onCrossfaderMoved(int value) {
    qDebug() << "Crossfader:" << value;
}

void MainWindow::onADeckPlayClicked() {
    if (outputWidget->isPlaying()) {
        outputWidget->pause();
        aDeckPlayBtn->setText("▶ Play");
    } else {
        outputWidget->play();
        aDeckPlayBtn->setText("⏸ Pause");
    }
}

void MainWindow::onBDeckPlayClicked() {
    if (outputWidget->isPlaying()) {
        outputWidget->pause();
        bDeckPlayBtn->setText("▶ Play");
    } else {
        outputWidget->play();
        bDeckPlayBtn->setText("⏸ Pause");
    }
}

void MainWindow::onADeckSpeedChanged(int value) {
    qDebug() << "A Deck Speed:" << value << "%";
}

void MainWindow::onBDeckSpeedChanged(int value) {
    qDebug() << "B Deck Speed:" << value << "%";
}

void MainWindow::onTimerUpdate() {
}

void MainWindow::onFullscreenClicked() {
    if (isFullScreen()) {
        showNormal();
        fullscreenBtn->setText("🖵 Fullscreen");
    } else {
        showFullScreen();
        fullscreenBtn->setText("🖦 Exit Fullscreen");
    }
}

void MainWindow::applyTheme() {
    QString styleSheet = R"(
        /* ==========================================================================
           Global & Window Backgrounds
           ========================================================================== */
        QMainWindow, QDialog, QWidget {
            background-color: #242528;
            color: #E0E0E0;
            font-family: "Segoe UI", Arial, sans-serif;
            font-size: 13px;
        }

        /* ==========================================================================
           Cards / Containers (Panels, GroupBoxes)
           ========================================================================== */
        QGroupBox {
            background-color: #242528;
            border: 1px solid #1c1d1f;
            border-radius: 12px;
            padding-top: 10px;
            margin-top: 10px;
            color: #E0E0E0;
            font-weight: bold;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px 0 5px;
            color: #2a8fa0;
            font-weight: bold;
            font-size: 12px;
        }

        /* ==========================================================================
           Buttons (Neumorphic Raised to Sunken)
           ========================================================================== */
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2a2c30, stop:1 #1e1f22);
            color: #E0E0E0;
            border-top: 1px solid #33363b;
            border-left: 1px solid #33363b;
            border-bottom: 1px solid #151618;
            border-right: 1px solid #151618;
            border-radius: 6px;
            padding: 4px 10px;
            font-weight: bold;
            font-size: 11px;
            min-height: 22px;
            height: 22px;
        }

        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2e3136, stop:1 #222326);
            color: #FFFFFF;
        }

        QPushButton:pressed {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #191a1c, stop:1 #2b2d32);
            border-top: 1px solid #121314;
            border-left: 1px solid #121314;
            border-bottom: 1px solid #3a3d43;
            border-right: 1px solid #3a3d43;
            color: #aaaaaa;
        }

        /* Accent Buttons (e.g., Play, Load) */
        QPushButton#accentButton, QPushButton[text*="Load"], QPushButton[text*="Play"], QPushButton[text*="Fullscreen"] {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3a6670, stop:1 #1f3d45);
            color: #FFFFFF;
            border-top: 1px solid #4a7f8c;
            border-left: 1px solid #4a7f8c;
            border-bottom: 1px solid #112226;
            border-right: 1px solid #112226;
            padding: 4px 12px;
            font-size: 11px;
            min-height: 22px;
            height: 22px;
        }

        QPushButton#accentButton:pressed, QPushButton[text*="Play"]:pressed {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #15292e, stop:1 #2f545c);
            border-top: 1px solid #0f1d21;
            border-left: 1px solid #0f1d21;
        }

        /* Grid Clip Buttons */
        QPushButton[objectName*="clipButton"] {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2a2c30, stop:1 #1e1f22);
            border-radius: 6px;
            padding: 4px;
            font-size: 10px;
            min-height: 76px;
            min-width: 76px;
            max-height: 76px;
            max-width: 76px;
        }

        QPushButton[objectName*="clipButton"]:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3a5c6a, stop:1 #2a4a58);
        }

        /* ==========================================================================
           Scroll Bars
           ========================================================================== */
        QScrollBar:vertical {
            background-color: #1c1d1f;
            width: 12px;
            margin: 0px;
            border-radius: 6px;
        }

        QScrollBar::handle:vertical {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #32353a, stop:1 #242528);
            min-height: 20px;
            border-radius: 6px;
            border: 1px solid #151618;
        }

        QScrollBar::handle:vertical:hover {
            background-color: #3d4147;
        }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            background: none;
            height: 0px;
        }

        QScrollBar:horizontal {
            background-color: #1c1d1f;
            height: 12px;
            margin: 0px;
            border-radius: 6px;
        }

        QScrollBar::handle:horizontal {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #32353a, stop:1 #242528);
            min-width: 20px;
            border-radius: 6px;
            border: 1px solid #151618;
        }

        QScrollBar::handle:horizontal:hover {
            background-color: #3d4147;
        }

        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            background: none;
            width: 0px;
        }

        /* ==========================================================================
           Sliders (Crossfader, Speed Control)
           ========================================================================== */
        QSlider::groove:horizontal {
            border: 1px solid #1c1d1f;
            height: 6px;
            background: #18191b;
            border-radius: 3px;
        }

        QSlider::sub-page:horizontal {
            background: #2a5c66;
            border-radius: 3px;
        }

        QSlider::handle:horizontal {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3a3d43, stop:1 #1c1d1f);
            border: 1px solid #4a4e56;
            width: 14px;
            margin-top: -5px;
            margin-bottom: -5px;
            border-radius: 7px;
        }

        QSlider::handle:horizontal:hover {
            background: #4a4e56;
        }

        QSlider::groove:vertical {
            border: 1px solid #1c1d1f;
            width: 6px;
            background: #18191b;
            border-radius: 3px;
        }

        QSlider::handle:vertical {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3a3d43, stop:1 #1c1d1f);
            border: 1px solid #4a4e56;
            height: 14px;
            margin-left: -5px;
            margin-right: -5px;
            border-radius: 7px;
        }

        /* ==========================================================================
           Labels
           ========================================================================== */
        QLabel {
            color: #E0E0E0;
            background-color: transparent;
            font-size: 12px;
        }

        /* ==========================================================================
           Spinbox (Speed Control)
           ========================================================================== */
        QSpinBox {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2a2c30, stop:1 #1e1f22);
            color: #E0E0E0;
            border-top: 1px solid #33363b;
            border-left: 1px solid #33363b;
            border-bottom: 1px solid #151618;
            border-right: 1px solid #151618;
            border-radius: 6px;
            padding: 4px;
            selection-background-color: #2a5c66;
        }

        QSpinBox:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2e3136, stop:1 #222326);
        }

        QSpinBox::up-button, QSpinBox::down-button {
            background-color: #1e1f22;
            border: 1px solid #151618;
            border-radius: 3px;
            width: 16px;
        }

        QSpinBox::up-button:pressed, QSpinBox::down-button:pressed {
            background-color: #151618;
        }

        /* ==========================================================================
           Misc Widgets
           ========================================================================== */
        QListWidget {
            background-color: #1c1d1f;
            border: 1px solid #151618;
            border-radius: 8px;
            color: #E0E0E0;
        }

        QListWidget::item {
            padding: 4px;
        }

        QListWidget::item:selected {
            background-color: #2a5c66;
            color: #FFFFFF;
        }

        QListWidget::item:hover {
            background-color: #2a2c30;
        }
    )";
    qApp->setStyle("fusion");
    qApp->setStyleSheet(styleSheet);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        QUrl url = mimeData->urls().first();
        QString filePath = url.toLocalFile();
        outputWidget->loadVideo(filePath);
        outputWidget->play();
        event->acceptProposedAction();
    }
}
