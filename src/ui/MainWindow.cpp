#include "MainWindow.h"
#include "../core/ThumbnailExtractor.h"
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QFileDialog>
#include <QTimer>
#include <QDebug>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QGroupBox>
#include <QPixmap>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("SwitchX - Live Media Control");
    setWindowIcon(QIcon());
    setGeometry(100, 100, 1200, 1000);

    setAcceptDrops(true);

    for (int i = 0; i < 512; ++i) {
        clipCards[i] = nullptr;
    }

    outputWindow = new OutputWindow(this);
    outputWindow->show();

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
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    setCentralWidget(centralWidget);

    // Clip Grid (full width)
    QGroupBox *gridGroup = new QGroupBox("Clip Grid Launcher", this);
    QVBoxLayout *gridVLayout = new QVBoxLayout(gridGroup);
    gridVLayout->setContentsMargins(10, 10, 10, 10);
    gridVLayout->setSpacing(8);

    loadFolderBtn = new QPushButton("📁 Load", this);
    loadFolderBtn->setMaximumWidth(100);
    gridVLayout->addWidget(loadFolderBtn);

    QWidget *gridWidget = new QWidget(this);
    gridLayout = new QGridLayout(gridWidget);
    gridLayout->setContentsMargins(4, 4, 4, 4);
    gridLayout->setSpacing(4);

    // Create 32 clip cards and add to grid with initial 8-column layout
    for (int i = 0; i < 32; ++i) {
        clipCards[i] = new ClipCard(i, this);
        connect(clipCards[i], &ClipCard::triggered, this, &MainWindow::onClipGridClicked);
        connect(clipCards[i], &ClipCard::aButtonClicked, this, &MainWindow::onAButtonClicked);
        connect(clipCards[i], &ClipCard::bButtonClicked, this, &MainWindow::onBButtonClicked);

        int row = i / 8;
        int col = i % 8;
        gridLayout->addWidget(clipCards[i], row, col);
    }

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(gridWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    gridVLayout->addWidget(scrollArea, 1);
    mainLayout->addWidget(gridGroup, 1);

    // Control Panel
    createControlPanel();

    applyTheme();
}



void MainWindow::createControlPanel() {
    QGroupBox *controlGroup = new QGroupBox("Control Panel", this);
    QHBoxLayout *mainControlLayout = new QHBoxLayout(controlGroup);
    mainControlLayout->setContentsMargins(10, 10, 10, 10);
    mainControlLayout->setSpacing(12);

    // ── A Deck ────────────────────────────────────────────────────────────
    QGroupBox *aDeckGroup = new QGroupBox("A Deck", this);
    QVBoxLayout *aDeckLayout = new QVBoxLayout(aDeckGroup);
    aDeckLayout->setContentsMargins(8, 8, 8, 8);
    aDeckLayout->setSpacing(4);

    aSelectedLabel = new QLabel("(None selected)", this);
    aSelectedLabel->setStyleSheet("font-size: 9px; color: #2a8fa0;");
    aSelectedLabel->setAlignment(Qt::AlignCenter);
    aDeckLayout->addWidget(aSelectedLabel);

    aPreviewLabel = new QLabel(this);
    aPreviewLabel->setFixedSize(160, 90);
    aPreviewLabel->setStyleSheet("background-color: #18191b; border: 1px solid #2a2c30; border-radius: 4px;");
    aPreviewLabel->setAlignment(Qt::AlignCenter);
    aPreviewLabel->setText("Empty");
    aDeckLayout->addWidget(aPreviewLabel, 0, Qt::AlignHCenter);

    aProgressSlider = new QSlider(Qt::Horizontal, this);
    aProgressSlider->setRange(0, 1000);
    aProgressSlider->setValue(0);
    aProgressSlider->setEnabled(false);
    aDeckLayout->addWidget(aProgressSlider);

    aTimeLabel = new QLabel("0:00 / 0:00", this);
    aTimeLabel->setStyleSheet("font-size: 9px; color: #888; font-family: monospace;");
    aTimeLabel->setAlignment(Qt::AlignCenter);
    aDeckLayout->addWidget(aTimeLabel);

    aDeckPlayBtn = new QPushButton("▶", this);
    aDeckPlayBtn->setMaximumWidth(60);
    aDeckPlayBtn->setEnabled(false);

    QLabel *speedLabelA = new QLabel("Speed:", this);
    speedLabelA->setStyleSheet("font-size: 10px;");
    aDeckSpeedSpinBox = new QSpinBox(this);
    aDeckSpeedSpinBox->setRange(-200, 200);
    aDeckSpeedSpinBox->setValue(100);
    aDeckSpeedSpinBox->setSuffix("%");
    aDeckSpeedSpinBox->setMaximumWidth(80);

    aDeckLayout->addWidget(speedLabelA);
    aDeckLayout->addWidget(aDeckSpeedSpinBox);
    aDeckLayout->addWidget(aDeckPlayBtn);
    aDeckLayout->addStretch();
    mainControlLayout->addWidget(aDeckGroup, 1);

    // ── Crossfader ────────────────────────────────────────────────────────
    QGroupBox *faderGroup = new QGroupBox("A/B Crossfader", this);
    QVBoxLayout *faderLayout = new QVBoxLayout(faderGroup);
    faderLayout->setContentsMargins(8, 8, 8, 8);
    faderLayout->setSpacing(4);

    crossfaderSlider = new QSlider(Qt::Horizontal, this);
    crossfaderSlider->setRange(0, 100);
    crossfaderSlider->setValue(0); // start on A

    QLabel *labelA = new QLabel("◄ A", this);
    labelA->setStyleSheet("font-size: 10px;");
    QLabel *labelB = new QLabel("B ►", this);
    labelB->setStyleSheet("font-size: 10px;");

    faderLayout->addWidget(labelA);
    faderLayout->addWidget(crossfaderSlider);
    faderLayout->addWidget(labelB);
    faderLayout->addStretch();
    mainControlLayout->addWidget(faderGroup, 1);

    // ── B Deck ────────────────────────────────────────────────────────────
    QGroupBox *bDeckGroup = new QGroupBox("B Deck", this);
    QVBoxLayout *bDeckLayout = new QVBoxLayout(bDeckGroup);
    bDeckLayout->setContentsMargins(8, 8, 8, 8);
    bDeckLayout->setSpacing(4);

    bSelectedLabel = new QLabel("(None selected)", this);
    bSelectedLabel->setStyleSheet("font-size: 9px; color: #2a8fa0;");
    bSelectedLabel->setAlignment(Qt::AlignCenter);
    bDeckLayout->addWidget(bSelectedLabel);

    bPreviewLabel = new QLabel(this);
    bPreviewLabel->setFixedSize(160, 90);
    bPreviewLabel->setStyleSheet("background-color: #18191b; border: 1px solid #2a2c30; border-radius: 4px;");
    bPreviewLabel->setAlignment(Qt::AlignCenter);
    bPreviewLabel->setText("Empty");
    bDeckLayout->addWidget(bPreviewLabel, 0, Qt::AlignHCenter);

    bProgressSlider = new QSlider(Qt::Horizontal, this);
    bProgressSlider->setRange(0, 1000);
    bProgressSlider->setValue(0);
    bProgressSlider->setEnabled(false);
    bDeckLayout->addWidget(bProgressSlider);

    bTimeLabel = new QLabel("0:00 / 0:00", this);
    bTimeLabel->setStyleSheet("font-size: 9px; color: #888; font-family: monospace;");
    bTimeLabel->setAlignment(Qt::AlignCenter);
    bDeckLayout->addWidget(bTimeLabel);

    bDeckPlayBtn = new QPushButton("▶", this);
    bDeckPlayBtn->setMaximumWidth(60);
    bDeckPlayBtn->setEnabled(false);

    QLabel *speedLabelB = new QLabel("Speed:", this);
    speedLabelB->setStyleSheet("font-size: 10px;");
    bDeckSpeedSpinBox = new QSpinBox(this);
    bDeckSpeedSpinBox->setRange(-200, 200);
    bDeckSpeedSpinBox->setValue(100);
    bDeckSpeedSpinBox->setSuffix("%");
    bDeckSpeedSpinBox->setMaximumWidth(80);

    bDeckLayout->addWidget(speedLabelB);
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
    connect(aDeckPlayBtn, &QPushButton::clicked, this, &MainWindow::onADeckPlayClicked);
    connect(bDeckPlayBtn, &QPushButton::clicked, this, &MainWindow::onBDeckPlayClicked);
    connect(aDeckSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onADeckSpeedChanged);
    connect(bDeckSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onBDeckSpeedChanged);
    connect(crossfaderSlider, &QSlider::valueChanged, this, &MainWindow::onCrossfaderMoved);

    // A progress slider
    connect(aProgressSlider, &QSlider::sliderPressed,  this, [this]() { m_aSliderDragging = true; });
    connect(aProgressSlider, &QSlider::sliderReleased, this, [this]() {
        m_aSliderDragging = false;
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationA();
        if (dur > 0) {
            double t = aProgressSlider->value() / 1000.0 * dur;
            out->seekA(t);
        }
    });
    connect(aProgressSlider, &QSlider::sliderMoved, this, [this](int value) {
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationA();
        if (dur > 0)
            aTimeLabel->setText(formatTimeShort(value / 1000.0 * dur) + " / " + formatTimeShort(dur));
    });

    // B progress slider
    connect(bProgressSlider, &QSlider::sliderPressed,  this, [this]() { m_bSliderDragging = true; });
    connect(bProgressSlider, &QSlider::sliderReleased, this, [this]() {
        m_bSliderDragging = false;
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationB();
        if (dur > 0) {
            double t = bProgressSlider->value() / 1000.0 * dur;
            out->seekB(t);
        }
    });
    connect(bProgressSlider, &QSlider::sliderMoved, this, [this](int value) {
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationB();
        if (dur > 0)
            bTimeLabel->setText(formatTimeShort(value / 1000.0 * dur) + " / " + formatTimeShort(dur));
    });
}

void MainWindow::onClipGridClicked(int index) {
    ClipCard *card = clipCards[index];
    if (!card || card->clipPath().isEmpty()) return;

    for (int i = 0; i < 32; ++i) {
        if (clipCards[i]) clipCards[i]->setActive(false);
    }
    selectedClipIndex = index;
    card->setActive(true);

    // Load into whichever deck is currently shown
    if (crossfaderSlider->value() <= 50) {
        onAButtonClicked(index);
    } else {
        onBButtonClicked(index);
    }
}

void MainWindow::onLoadFolderClicked() {
    QString folderPath = QFileDialog::getExistingDirectory(this, "Select Media Folder", "");
    if (!folderPath.isEmpty()) {
        clipManager.loadFolder(folderPath);
        for (int i = 0; i < 32; ++i) {
            if (i < clipManager.getClipCount()) {
                QString clipPath = clipManager.getClipPath(i);
                QPixmap thumb = ThumbnailExtractor::extract(clipPath, 110, 65);
                clipCards[i]->loadClip(clipPath, thumb);
            } else {
                clipCards[i]->clearClip();
            }
        }
        qDebug() << "Loaded folder with" << clipManager.getClipCount() << "clips";
    }
}

void MainWindow::onCrossfaderMoved(int value) {
    outputWindow->videoWidget()->setShowA(value <= 50);
}

void MainWindow::onADeckPlayClicked() {
    VideoWidget *output = outputWindow->videoWidget();
    if (output->isPlayingA())
        output->pauseA();
    else
        output->playA();
}

void MainWindow::onBDeckPlayClicked() {
    VideoWidget *output = outputWindow->videoWidget();
    if (output->isPlayingB())
        output->pauseB();
    else
        output->playB();
}

void MainWindow::onADeckSpeedChanged(int value) {
    qDebug() << "A Deck Speed:" << value << "%";
}

void MainWindow::onBDeckSpeedChanged(int value) {
    qDebug() << "B Deck Speed:" << value << "%";
}

void MainWindow::onTimerUpdate() {
    VideoWidget *out = outputWindow->videoWidget();

    // ── A deck ──────────────────────────────────────────────────────────
    double durA = out->getDurationA();
    double timeA = out->getCurrentTimeA();
    if (durA > 0) {
        if (!m_aSliderDragging) {
            aProgressSlider->blockSignals(true);
            aProgressSlider->setValue((int)(timeA / durA * 1000));
            aProgressSlider->blockSignals(false);
        }
        aTimeLabel->setText(formatTimeShort(timeA) + " / " + formatTimeShort(durA));
    }
    aDeckPlayBtn->setText(out->isPlayingA() ? "⏸" : "▶");

    QImage frameA = out->getFrameA();
    if (!frameA.isNull())
        aPreviewLabel->setPixmap(QPixmap::fromImage(
            frameA.scaled(160, 90, Qt::KeepAspectRatio, Qt::FastTransformation)));

    // ── B deck ──────────────────────────────────────────────────────────
    double durB = out->getDurationB();
    double timeB = out->getCurrentTimeB();
    if (durB > 0) {
        if (!m_bSliderDragging) {
            bProgressSlider->blockSignals(true);
            bProgressSlider->setValue((int)(timeB / durB * 1000));
            bProgressSlider->blockSignals(false);
        }
        bTimeLabel->setText(formatTimeShort(timeB) + " / " + formatTimeShort(durB));
    }
    bDeckPlayBtn->setText(out->isPlayingB() ? "⏸" : "▶");

    QImage frameB = out->getFrameB();
    if (!frameB.isNull())
        bPreviewLabel->setPixmap(QPixmap::fromImage(
            frameB.scaled(160, 90, Qt::KeepAspectRatio, Qt::FastTransformation)));
}


void MainWindow::updateGridLayout() {
    if (!gridLayout) return;

    // Clear the grid layout
    while (QLayoutItem *item = gridLayout->takeAt(0)) {
        // Don't delete the widget, just remove from layout
        if (item->widget()) item->widget()->hide();
    }

    // Calculate how many columns fit in the available width
    int availableWidth = width();
    if (availableWidth <= 0) availableWidth = 1200; // Default width if not yet sized

    availableWidth -= 60; // Account for margins and scrollbar
    dynamicCols = std::max(MIN_COLS, availableWidth / (CARD_WIDTH + 4));

    // Add cards to grid with calculated columns
    for (int i = 0; i < 32; ++i) {
        clipCards[i]->show();
        int row = i / dynamicCols;
        int col = i % dynamicCols;
        gridLayout->addWidget(clipCards[i], row, col);
    }

    gridLayout->update();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateGridLayout();
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
        outputWindow->videoWidget()->loadVideo(filePath);
        outputWindow->videoWidget()->play();
        event->acceptProposedAction();
    }
}

void MainWindow::onAButtonClicked(int index) {
    ClipCard *card = clipCards[index];
    if (!card || card->clipPath().isEmpty()) return;

    if (aClipIndex >= 0 && aClipIndex < 32)
        clipCards[aClipIndex]->setASelected(false);
    aClipIndex = index;
    card->setASelected(true);

    VideoWidget *out = outputWindow->videoWidget();
    out->setRepeatA(card->isRepeat());
    out->setTrimPointsA(card->startTime(), card->endTime());
    out->loadVideoA(card->clipPath());
    if (card->startTime() > 0) out->seekA(card->startTime());
    out->playA();

    double dur = out->getDurationA();
    aProgressSlider->setRange(0, 1000);
    aProgressSlider->setValue(0);
    aProgressSlider->setEnabled(true);
    aDeckPlayBtn->setEnabled(true);
    aSelectedLabel->setText(QString("A: %1").arg(QFileInfo(card->clipPath()).baseName()));
    aTimeLabel->setText(formatTimeShort(card->startTime()) + " / " + formatTimeShort(dur));
}

void MainWindow::onBButtonClicked(int index) {
    ClipCard *card = clipCards[index];
    if (!card || card->clipPath().isEmpty()) return;

    if (bClipIndex >= 0 && bClipIndex < 32)
        clipCards[bClipIndex]->setBSelected(false);
    bClipIndex = index;
    card->setBSelected(true);

    VideoWidget *out = outputWindow->videoWidget();
    out->setRepeatB(card->isRepeat());
    out->setTrimPointsB(card->startTime(), card->endTime());
    out->loadVideoB(card->clipPath());
    if (card->startTime() > 0) out->seekB(card->startTime());
    out->playB();

    double dur = out->getDurationB();
    bProgressSlider->setRange(0, 1000);
    bProgressSlider->setValue(0);
    bProgressSlider->setEnabled(true);
    bDeckPlayBtn->setEnabled(true);
    bSelectedLabel->setText(QString("B: %1").arg(QFileInfo(card->clipPath()).baseName()));
    bTimeLabel->setText(formatTimeShort(card->startTime()) + " / " + formatTimeShort(dur));
}

QString MainWindow::formatTimeShort(double secs) {
    if (secs < 0) secs = 0;
    int m = (int)secs / 60;
    int s = (int)secs % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}
