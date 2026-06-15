#include "ui/MainWindow.h"
#include "ui_MainWindow.h"
#include "ui/VideoWidget.h"
#include "core/ThumbnailExtractor.h"
#include "core/VideoFileSource.h"
#include "core/SlideshowSource.h"
#include "core/CameraSource.h"
#include "core/ScreenSource.h"
#include "core/ColorSource.h"
#include "core/ImageSource.h"
#include "core/ShaderSource.h"
#include "ui/ShaderEditDialog.h"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <glob.h>
#include <QFileDialog>
#include <QTimer>
#include <QDebug>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QMenu>
#include <QInputDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    setAcceptDrops(true);

    outputWindow = new OutputWindow(this);
    outputWindow->show();

    // Create a stacked widget to manage placeholder vs. node editor
    m_stackWidget = new QStackedWidget(ui->gridWidget);
    ui->gridLayout->addWidget(m_stackWidget, 0, 0, 1, 1);

    // Initialize the ClipNodeEditor
    m_clipNodeEditor = new ClipNodeEditor();
    m_clipNodeEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stackWidget->addWidget(m_clipNodeEditor);

    // Build the empty-state placeholder (shown before any media is loaded)
    m_emptyPlaceholder = new QWidget();
    m_emptyPlaceholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *phLayout = new QVBoxLayout(m_emptyPlaceholder);
    phLayout->setAlignment(Qt::AlignCenter);
    phLayout->setSpacing(16);

    auto *phIcon = new QLabel("📁", m_emptyPlaceholder);
    phIcon->setAlignment(Qt::AlignCenter);
    phIcon->setStyleSheet("font-size: 52px;");

    auto *phText = new QLabel("No media loaded\nLoad a folder, add another folder, or pick individual files.",
                              m_emptyPlaceholder);
    phText->setAlignment(Qt::AlignCenter);
    phText->setWordWrap(true);
    phText->setStyleSheet("color: #555; font-size: 13px;");

    // Quick-action buttons embedded in the placeholder
    auto *phBtnRow = new QWidget(m_emptyPlaceholder);
    auto *phBtns   = new QHBoxLayout(phBtnRow);
    phBtns->setSpacing(10);
    auto *phLoad  = new QPushButton("📁  Load Folder",  phBtnRow);
    auto *phAdd   = new QPushButton("📂  Add Folder",   phBtnRow);
    auto *phFiles = new QPushButton("＋  Add Files",    phBtnRow);
    for (auto *b : {phLoad, phAdd, phFiles}) {
        b->setStyleSheet("font-size: 12px; padding: 6px 16px;");
        phBtns->addWidget(b);
    }
    connect(phLoad,  &QPushButton::clicked, this, &MainWindow::onLoadFolderClicked);
    connect(phAdd,   &QPushButton::clicked, this, &MainWindow::onAddFolderClicked);
    connect(phFiles, &QPushButton::clicked, this, &MainWindow::onAddFilesClicked);

    phLayout->addWidget(phIcon);
    phLayout->addWidget(phText);
    phLayout->addWidget(phBtnRow);

    m_stackWidget->addWidget(m_emptyPlaceholder);
    m_stackWidget->setCurrentWidget(m_emptyPlaceholder);

    setupConnections();
    applyTheme();

    // Kick the Qt Multimedia GStreamer backend into life early so that
    // QMediaDevices::videoInputs() is populated by the time the user
    // clicks "Add Camera".  The singleShot lets the event loop spin once
    // first so the window is already shown.
    QTimer::singleShot(0, []() {
        [[maybe_unused]] auto _ = QMediaDevices::videoInputs();
    });

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::onTimerUpdate);
    updateTimer->start(100);

    qDebug() << "SwitchX initialized - Live Media Control Mode";
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::setupConnections() {
    connect(ui->actionLoadFolder, &QAction::triggered, this, &MainWindow::onLoadFolderClicked);
    connect(ui->actionAddFolder,  &QAction::triggered, this, &MainWindow::onAddFolderClicked);
    connect(ui->actionAddFiles,   &QAction::triggered, this, &MainWindow::onAddFilesClicked);
    connect(ui->actionClearAll,   &QAction::triggered, this, &MainWindow::onClearAllClicked);
    connect(ui->actionShowOutput, &QAction::triggered, this, [this]() {
        outputWindow->show();
        outputWindow->raise();
        outputWindow->activateWindow();
    });
    connect(ui->actionStayOnTop, &QAction::toggled, this, [this](bool on) {
        Qt::WindowFlags flags = outputWindow->windowFlags();
        if (on)
            flags |= Qt::WindowStaysOnTopHint;
        else
            flags &= ~Qt::WindowStaysOnTopHint;
        outputWindow->setWindowFlags(flags);
        outputWindow->show();
    });

    // ── Add Element submenu in menubar ────────────────────────────────────────
    auto *addElemMenu = ui->menuMedia->addMenu("Add Element");
    addElemMenu->addAction("🎬  Video File…",  this, &MainWindow::onAddFilesClicked);
    addElemMenu->addAction("🖼  Photo…",       this, [this]() {
        QStringList files = QFileDialog::getOpenFileNames(
            this, "Add Photos", "",
            "Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif)");
        if (files.isEmpty()) return;
        clipManager.addFiles(files);
        
        // Clear and rebuild the editor with all clips
        m_clipNodeEditor->clearAllNodes();
        m_aClipNodeId = 0;
        m_bClipNodeId = 0;
        
        for (int i = 0; i < clipManager.getClipCount(); ++i) {
            QString clipPath = clipManager.getClipPath(i);
            QPixmap thumb = ThumbnailExtractor::extract(clipPath, 110, 65);
            m_clipNodeEditor->addClipNode(clipPath, thumb);
        }
        m_stackWidget->setCurrentWidget(m_clipNodeEditor);
    });
    addElemMenu->addSeparator();
    addElemMenu->addAction("📁  Slideshow…",   this, &MainWindow::onAddElementSlideshow);
    addElemMenu->addSeparator();
    addElemMenu->addAction("📷  Camera…",          this, &MainWindow::onAddElementCamera);
    addElemMenu->addAction("🖥  Screen Capture…",  this, &MainWindow::onAddElementScreen);
    addElemMenu->addAction("🪟  Window / Tab…",    this, &MainWindow::onAddElementWindow);
    addElemMenu->addSeparator();
    addElemMenu->addAction("⬛  Solid Color…",     this, &MainWindow::onAddElementColor);
    addElemMenu->addSeparator();
    addElemMenu->addAction("≋  Shader…",           this, &MainWindow::onAddElementShader);

    // ── ClipNodeEditor signals ────────────────────────────────────────────────
    connect(m_clipNodeEditor, &ClipNodeEditor::deckAClipChanged, this, &MainWindow::onNodeAButtonClicked);
    connect(m_clipNodeEditor, &ClipNodeEditor::deckBClipChanged, this, &MainWindow::onNodeBButtonClicked);
    connect(m_clipNodeEditor, &ClipNodeEditor::nodeRemoved, this, &MainWindow::onNodeRemoveRequested);

    connect(ui->aDeckPlayBtn,     &QPushButton::clicked,  this, &MainWindow::onADeckPlayClicked);
    connect(ui->bDeckPlayBtn,     &QPushButton::clicked,  this, &MainWindow::onBDeckPlayClicked);
    connect(ui->aDeckSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onADeckSpeedChanged);
    connect(ui->bDeckSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onBDeckSpeedChanged);
    connect(ui->crossfaderSlider, &QSlider::valueChanged, this, &MainWindow::onCrossfaderMoved);

    connect(ui->aProgressSlider, &QSlider::sliderPressed, this, [this]() { m_aSliderDragging = true; });
    connect(ui->aProgressSlider, &QSlider::sliderReleased, this, [this]() {
        m_aSliderDragging = false;
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationA();
        if (dur > 0) {
            double t = ui->aProgressSlider->value() / 1000.0 * dur;
            out->seekA(t);
        }
    });
    connect(ui->aProgressSlider, &QSlider::sliderMoved, this, [this](int value) {
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationA();
        if (dur > 0)
            ui->aTimeLabel->setText(formatTimeShort(value / 1000.0 * dur) + " / " + formatTimeShort(dur));
    });

    connect(ui->bProgressSlider, &QSlider::sliderPressed, this, [this]() { m_bSliderDragging = true; });
    connect(ui->bProgressSlider, &QSlider::sliderReleased, this, [this]() {
        m_bSliderDragging = false;
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationB();
        if (dur > 0) {
            double t = ui->bProgressSlider->value() / 1000.0 * dur;
            out->seekB(t);
        }
    });
    connect(ui->bProgressSlider, &QSlider::sliderMoved, this, [this](int value) {
        VideoWidget *out = outputWindow->videoWidget();
        double dur = out->getDurationB();
        if (dur > 0)
            ui->bTimeLabel->setText(formatTimeShort(value / 1000.0 * dur) + " / " + formatTimeShort(dur));
    });
}

void MainWindow::onLoadFolderClicked() {
    QString path = QFileDialog::getExistingDirectory(this, "Select Media Folder");
    if (path.isEmpty()) return;
    m_clipNodeEditor->clearAllNodes();
    clipManager.loadFolder(path);
    m_aClipNodeId = 0;
    m_bClipNodeId = 0;
    outputWindow->videoWidget()->setNodeChainA({});
    outputWindow->videoWidget()->setNodeChainB({});
    for (int i = 0; i < clipManager.getClipCount(); ++i) {
        QString clipPath = clipManager.getClipPath(i);
        QPixmap thumb = ThumbnailExtractor::extract(clipPath, 110, 65);
        m_clipNodeEditor->addClipNode(clipPath, thumb);
    }
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::onAddFolderClicked() {
    QString path = QFileDialog::getExistingDirectory(this, "Add Media Folder");
    if (path.isEmpty()) return;
    clipManager.addFolder(path);
    
    // Clear and rebuild the editor with all clips
    m_clipNodeEditor->clearAllNodes();
    m_aClipNodeId = 0;
    m_bClipNodeId = 0;
    outputWindow->videoWidget()->setNodeChainA({});
    outputWindow->videoWidget()->setNodeChainB({});
    
    for (int i = 0; i < clipManager.getClipCount(); ++i) {
        QString clipPath = clipManager.getClipPath(i);
        QPixmap thumb = ThumbnailExtractor::extract(clipPath, 110, 65);
        m_clipNodeEditor->addClipNode(clipPath, thumb);
    }
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::onAddFilesClicked() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Add Media Files", "",
        "Media Files (*.mp4 *.avi *.mov *.mkv *.webm *.png *.jpg *.jpeg)");
    if (files.isEmpty()) return;
    clipManager.addFiles(files);
    
    // Clear and rebuild the editor with all clips
    m_clipNodeEditor->clearAllNodes();
    m_aClipNodeId = 0;
    m_bClipNodeId = 0;
    outputWindow->videoWidget()->setNodeChainA({});
    outputWindow->videoWidget()->setNodeChainB({});
    
    for (int i = 0; i < clipManager.getClipCount(); ++i) {
        QString clipPath = clipManager.getClipPath(i);
        QPixmap thumb = ThumbnailExtractor::extract(clipPath, 110, 65);
        m_clipNodeEditor->addClipNode(clipPath, thumb);
    }
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::onClearAllClicked() {
    clipManager.clear();
    m_clipNodeEditor->clearAllNodes();
    m_aClipNodeId = 0;
    m_bClipNodeId = 0;
    outputWindow->videoWidget()->setNodeChainA({});
    outputWindow->videoWidget()->setNodeChainB({});
    m_clipNodeEditor->hide();
    m_emptyPlaceholder->show();
}

// ── Element management ────────────────────────────────────────────────────────

void MainWindow::addElementNode(const SourceDescriptor &desc, const QPixmap &thumb) {
    m_clipNodeEditor->addSourceNode(desc, thumb);
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::assignSourceToActiveDeck(std::unique_ptr<MediaSource> src,
                                          const QString &name) {
    const bool toA = (ui->crossfaderSlider->value() <= 50);
    VideoWidget *out = outputWindow->videoWidget();
    if (toA) {
        out->setSourceA(std::move(src));
        out->playA();
    } else {
        out->setSourceB(std::move(src));
        out->playB();
    }
    updateDeckUI(toA, name, false);
}

void MainWindow::updateDeckUI(bool deckA, const QString &name, bool hasTimeline) {
    if (deckA) {
        ui->aSelectedLabel->setText(QString("A: %1").arg(name));
        ui->aProgressSlider->setEnabled(hasTimeline);
        ui->aDeckPlayBtn->setEnabled(true);
        if (!hasTimeline) { ui->aProgressSlider->setValue(0); ui->aTimeLabel->setText("—"); }
    } else {
        ui->bSelectedLabel->setText(QString("B: %1").arg(name));
        ui->bProgressSlider->setEnabled(hasTimeline);
        ui->bDeckPlayBtn->setEnabled(true);
        if (!hasTimeline) { ui->bProgressSlider->setValue(0); ui->bTimeLabel->setText("—"); }
    }
}

QPixmap MainWindow::makeIconThumb(const QString &glyph, int w, int h) {
    QPixmap pix(w, h);
    pix.fill(QColor("#1c1d1f"));
    QPainter p(&pix);
    QFont f;
    f.setPixelSize(32);
    p.setFont(f);
    p.setPen(QColor("#888888"));
    p.drawText(pix.rect(), Qt::AlignCenter, glyph);
    return pix;
}

QPixmap MainWindow::makeColorThumb(const QColor &color, int w, int h) {
    QPixmap pix(w, h);
    pix.fill(color);
    return pix;
}

QPixmap MainWindow::makeShaderThumb(const QString &code, int w, int h) {
    ShaderSource src(code, QSize(w, h));
    if (!src.nextFrame() || !src.isReady())
        return makeIconThumb("≋", w, h);
    const uint8_t *data = src.frameData();
    QImage img(data, w, h, w * 3, QImage::Format_RGB888);
    return QPixmap::fromImage(img.copy());
}

// ── Crossfader ────────────────────────────────────────────────────────────────

void MainWindow::onCrossfaderMoved(int value) {
    outputWindow->videoWidget()->setCrossfade(value / 100.f);
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

    double durA = out->getDurationA();
    double timeA = out->getCurrentTimeA();
    if (durA > 0) {
        if (!m_aSliderDragging) {
            ui->aProgressSlider->blockSignals(true);
            ui->aProgressSlider->setValue((int)(timeA / durA * 1000));
            ui->aProgressSlider->blockSignals(false);
        }
        ui->aTimeLabel->setText(formatTimeShort(timeA) + " / " + formatTimeShort(durA));
    }
    ui->aDeckPlayBtn->setText(out->isPlayingA() ? "⏸" : "▶");

    QImage frameA = out->getFrameA();
    if (!frameA.isNull())
        ui->aPreviewLabel->setPixmap(QPixmap::fromImage(
            frameA.scaled(160, 90, Qt::KeepAspectRatio, Qt::FastTransformation)));

    double durB = out->getDurationB();
    double timeB = out->getCurrentTimeB();
    if (durB > 0) {
        if (!m_bSliderDragging) {
            ui->bProgressSlider->blockSignals(true);
            ui->bProgressSlider->setValue((int)(timeB / durB * 1000));
            ui->bProgressSlider->blockSignals(false);
        }
        ui->bTimeLabel->setText(formatTimeShort(timeB) + " / " + formatTimeShort(durB));
    }
    ui->bDeckPlayBtn->setText(out->isPlayingB() ? "⏸" : "▶");

    QImage frameB = out->getFrameB();
    if (!frameB.isNull())
        ui->bPreviewLabel->setPixmap(QPixmap::fromImage(
            frameB.scaled(160, 90, Qt::KeepAspectRatio, Qt::FastTransformation)));
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    // ClipNodeEditor handles its own layout updates.
}

void MainWindow::applyTheme() {
    QString styleSheet = R"(
        QMainWindow, QDialog, QWidget {
            background-color: #242528;
            color: #E0E0E0;
            font-family: "Segoe UI", Arial, sans-serif;
            font-size: 13px;
        }

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

        QScrollBar::handle:vertical:hover { background-color: #3d4147; }

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

        QScrollBar::handle:horizontal:hover { background-color: #3d4147; }

        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            background: none;
            width: 0px;
        }

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

        QSlider::handle:horizontal:hover { background: #4a4e56; }

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

        QLabel {
            color: #E0E0E0;
            background-color: transparent;
            font-size: 12px;
        }

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

        QListWidget {
            background-color: #1c1d1f;
            border: 1px solid #151618;
            border-radius: 8px;
            color: #E0E0E0;
        }

        QListWidget::item { padding: 4px; }
        QListWidget::item:selected { background-color: #2a5c66; color: #FFFFFF; }
        QListWidget::item:hover { background-color: #2a2c30; }
    )";
    qApp->setStyle("fusion");
    qApp->setStyleSheet(styleSheet);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasUrls()) return;

    bool hasContent = false;
    for (const QUrl &url : mimeData->urls()) {
        QString p = url.toLocalFile();
        QFileInfo fi(p);
        if (fi.isDir()) {
            clipManager.addFolder(p);
            hasContent = true;
        }
    }

    QStringList paths;
    for (const QUrl &url : mimeData->urls()) {
        QString p = url.toLocalFile();
        QFileInfo fi(p);
        if (!fi.isDir()) {
            paths << p;
            hasContent = true;
        }
    }

    if (!paths.isEmpty()) {
        clipManager.addFiles(paths);
        for (const auto &path : paths) {
            QPixmap thumb = ThumbnailExtractor::extract(path, 110, 65);
            m_clipNodeEditor->addClipNode(path, thumb);
        }
    }

    if (hasContent) {
        m_stackWidget->setCurrentWidget(m_clipNodeEditor);
    }

    event->acceptProposedAction();
}

static QString formatTimeShort(double secs) {
    if (secs < 0) secs = 0;
    int m = (int)secs / 60, s = (int)secs % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

// Assign a clip node to a deck using the clip's SourceDescriptor.
static void assignNodeToDeck(ClipNodeModel *node, bool deckA,
                             VideoWidget *out,
                             QSlider *progressSlider, QPushButton *playBtn,
                             QLabel *selectedLabel, QLabel *timeLabel) {
    if (!node) return;

    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor &desc = node->sourceDescriptor();

    switch (desc.kind) {

    case Kind::VideoFile:
    case Kind::Image: {
        if (deckA) {
            out->setRepeatA(node->isRepeat());
            out->setTrimPointsA(node->startTime(), node->endTime());
            out->setBaseA(node->settings().baseX, node->settings().baseY,
                          node->settings().baseW, node->settings().baseH);
            out->setCropA(node->cropX(), node->cropY(), node->cropW(), node->cropH());
            out->setOverlaysA(node->overlays());
            out->loadVideoA(desc.path);
            if (node->startTime() > 0) out->seekA(node->startTime());
            out->playA();
        } else {
            out->setRepeatB(node->isRepeat());
            out->setTrimPointsB(node->startTime(), node->endTime());
            out->setBaseB(node->settings().baseX, node->settings().baseY,
                          node->settings().baseW, node->settings().baseH);
            out->setCropB(node->cropX(), node->cropY(), node->cropW(), node->cropH());
            out->setOverlaysB(node->overlays());
            out->loadVideoB(desc.path);
            if (node->startTime() > 0) out->seekB(node->startTime());
            out->playB();
        }
        double dur = deckA ? out->getDurationA() : out->getDurationB();
        progressSlider->setRange(0, 1000);
        progressSlider->setValue(0);
        progressSlider->setEnabled(desc.kind == Kind::VideoFile && dur > 0);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText(formatTimeShort(node->startTime()) + " / " + formatTimeShort(dur));
        break;
    }

    case Kind::Slideshow: {
        auto src = std::make_unique<SlideshowSource>();
        if (!src->loadFolder(desc.path, desc.slideshowIntervalMs)) {
            QMessageBox::warning(nullptr, "Slideshow", "No images found in folder.");
            return;
        }
        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else        { out->setSourceB(std::move(src)); out->playB(); }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("—");
        break;
    }

    case Kind::Camera: {
        auto src = std::make_unique<CameraSource>();

        if (desc.path.isEmpty()) {
            src->start({});
        } else {
            bool matched = false;
            const auto qtDevices = QMediaDevices::videoInputs();
            for (const auto &dev : qtDevices) {
                if (QString::fromUtf8(dev.id()) == desc.path) {
                    src->start(dev);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                src->startDevice(desc.path);
            }
        }

        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else        { out->setSourceB(std::move(src)); out->playB(); }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    case Kind::Screen: {
        auto src = std::make_unique<ScreenSource>();
        if (!src->start(ScreenSource::CaptureType::Monitor)) return;
        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else        { out->setSourceB(std::move(src)); out->playB(); }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    case Kind::Window: {
        auto src = std::make_unique<ScreenSource>();
        if (!src->start(ScreenSource::CaptureType::Window)) return;
        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else        { out->setSourceB(std::move(src)); out->playB(); }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(true);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    case Kind::Color: {
        auto src = std::make_unique<ColorSource>(desc.color);
        if (deckA) out->setSourceA(std::move(src));
        else        out->setSourceB(std::move(src));
        progressSlider->setEnabled(false);
        playBtn->setEnabled(false);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("—");
        break;
    }

    case Kind::Shader: {
        auto src = std::make_unique<ShaderSource>(desc.shaderCode);
        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else        { out->setSourceB(std::move(src)); out->playB(); }
        progressSlider->setEnabled(false);
        playBtn->setEnabled(false);
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("LIVE");
        break;
    }

    }
}

// Creates a VideoWidget chain source for a single node (used for upstream overlays).
// Mirrors the source-creation logic in assignNodeToDeck without the UI side effects.
static VideoWidget::NodeChainSource makeNodeChainSource(ClipNodeModel *node) {
    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor &desc = node->sourceDescriptor();
    VideoWidget::NodeChainSource entry;
    entry.cropX = node->cropX(); entry.cropY = node->cropY();
    entry.cropW = node->cropW(); entry.cropH = node->cropH();
    entry.baseX = node->settings().baseX;
    entry.baseY = node->settings().baseY;
    entry.baseW = node->settings().baseW;
    entry.baseH = node->settings().baseH;

    switch (desc.kind) {
    case Kind::VideoFile: {
        auto src = std::make_unique<VideoFileSource>();
        if (!src->open(desc.path)) break;
        if (node->startTime() > 0) src->seek(node->startTime());
        src->nextFrame();
        entry.playing = true;
        entry.source  = std::move(src);
        break;
    }
    case Kind::Image: {
        auto src = std::make_unique<ImageSource>();
        if (!src->load(desc.path)) break;
        src->nextFrame();
        entry.source = std::move(src);
        break;
    }
    case Kind::Color:
        entry.source  = std::make_unique<ColorSource>(desc.color);
        entry.playing = true;
        break;
    case Kind::Slideshow: {
        auto src = std::make_unique<SlideshowSource>();
        if (!src->loadFolder(desc.path, desc.slideshowIntervalMs)) break;
        entry.source  = std::move(src);
        entry.playing = true;
        break;
    }
    case Kind::Camera: {
        auto src = std::make_unique<CameraSource>();
        if (desc.path.isEmpty()) {
            src->start({});
        } else {
            bool matched = false;
            const auto devs = QMediaDevices::videoInputs();
            for (const auto &dev : devs) {
                if (QString::fromUtf8(dev.id()) == desc.path) {
                    src->start(dev); matched = true; break;
                }
            }
            if (!matched) src->startDevice(desc.path);
        }
        entry.source  = std::move(src);
        entry.playing = true;
        break;
    }
    case Kind::Screen: {
        auto src = std::make_unique<ScreenSource>();
        if (src->start(ScreenSource::CaptureType::Monitor)) {
            entry.source = std::move(src);
            entry.playing = true;
        }
        break;
    }
    case Kind::Window: {
        auto src = std::make_unique<ScreenSource>();
        if (src->start(ScreenSource::CaptureType::Window)) {
            entry.source = std::move(src);
            entry.playing = true;
        }
        break;
    }
    case Kind::Shader:
        entry.source  = std::make_unique<ShaderSource>(desc.shaderCode);
        entry.playing = true;
        break;
    }
    return entry;
}

// Builds the VideoWidget chain from chain[1..n] (upstream nodes, 0 = base).
static std::vector<VideoWidget::NodeChainSource>
buildNodeChain(const QVector<ClipNodeModel *> &chain) {
    std::vector<VideoWidget::NodeChainSource> out;
    for (int i = 1; i < chain.size(); ++i) {
        auto entry = makeNodeChainSource(chain[i]);
        if (entry.source) out.push_back(std::move(entry));
    }
    return out;
}

void MainWindow::onNodeAButtonClicked(NodeId nodeId) {
    if (!nodeId) return;
    ClipNodeModel *node = m_clipNodeEditor->nodeAt(nodeId);
    if (!node || !node->hasSource()) return;

    if (m_aClipNodeId) {
        if (auto *oldNode = m_clipNodeEditor->nodeAt(m_aClipNodeId))
            oldNode->setASelected(false);
    }
    m_aClipNodeId = nodeId;
    node->setASelected(true);

    auto *out = outputWindow->videoWidget();
    // Assign the selected (base) node to deck A as usual
    assignNodeToDeck(node, true, out, ui->aProgressSlider, ui->aDeckPlayBtn,
                     ui->aSelectedLabel, ui->aTimeLabel);
    // Overlay upstream nodes on top, in chain order (nearest first)
    out->setNodeChainA(buildNodeChain(m_clipNodeEditor->getClipChain(nodeId)));
}

void MainWindow::onNodeBButtonClicked(NodeId nodeId) {
    if (!nodeId) return;
    ClipNodeModel *node = m_clipNodeEditor->nodeAt(nodeId);
    if (!node || !node->hasSource()) return;

    if (m_bClipNodeId) {
        if (auto *oldNode = m_clipNodeEditor->nodeAt(m_bClipNodeId))
            oldNode->setBSelected(false);
    }
    m_bClipNodeId = nodeId;
    node->setBSelected(true);

    auto *out = outputWindow->videoWidget();
    assignNodeToDeck(node, false, out, ui->bProgressSlider, ui->bDeckPlayBtn,
                     ui->bSelectedLabel, ui->bTimeLabel);
    out->setNodeChainB(buildNodeChain(m_clipNodeEditor->getClipChain(nodeId)));
}

void MainWindow::onNodeRemoveRequested(NodeId nodeId) {
    m_clipNodeEditor->removeNode(nodeId);
    auto *out = outputWindow->videoWidget();
    if (m_aClipNodeId == nodeId) { m_aClipNodeId = 0; out->setNodeChainA({}); }
    if (m_bClipNodeId == nodeId) { m_bClipNodeId = 0; out->setNodeChainB({}); }
    if (m_clipNodeEditor->allNodes().isEmpty()) {
        m_clipNodeEditor->hide();
        m_emptyPlaceholder->show();
    }
}

QString MainWindow::formatTimeShort(double secs) {
    return ::formatTimeShort(secs);   // delegates to the file-scope helper above
}

// ── Add Element handlers ──────────────────────────────────────────────────────

void MainWindow::onAddElementSlideshow() {
    QString folder = QFileDialog::getExistingDirectory(this, "Select Image Folder for Slideshow");
    if (folder.isEmpty()) return;

    bool ok = false;
    int interval = QInputDialog::getInt(this, "Slideshow Interval",
                                       "Seconds per slide:", 3, 1, 60, 1, &ok);
    if (!ok) return;

    // Use the first image as the thumbnail.
    QDir dir(folder);
    QStringList imgs = dir.entryList({"*.png","*.jpg","*.jpeg","*.bmp","*.webp"},
                                    QDir::Files, QDir::Name);
    QPixmap thumb;
    if (!imgs.isEmpty())
        thumb = ThumbnailExtractor::extract(dir.absoluteFilePath(imgs.first()), 110, 65);
    if (thumb.isNull())
        thumb = makeIconThumb("📁");

    SourceDescriptor desc;
    desc.kind                = SourceDescriptor::Kind::Slideshow;
    desc.path                = folder;
    desc.displayName         = QFileInfo(folder).fileName();
    desc.slideshowIntervalMs = interval * 1000;

    addElementNode(desc, thumb);
}

void MainWindow::onAddElementCamera() {
    auto qtDevices = QMediaDevices::videoInputs();
    if (qtDevices.isEmpty()) {
        QEventLoop loop;
        QTimer::singleShot(1200, &loop, &QEventLoop::quit);
        loop.exec();
        qtDevices = QMediaDevices::videoInputs();
    }

    struct CamEntry { QString id; QString label; bool isDefault; };
    QList<CamEntry> devices;

    for (const auto &d : qtDevices) {
        QString id    = QString::fromUtf8(d.id());
        QString label = d.description().isEmpty() ? id
                      : QString("%1  [%2]").arg(d.description(), id);
        devices.append({id, label, false});
    }

    {
        glob_t g{};
        if (::glob("/dev/video*", GLOB_NOSORT, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) {
                QString path = QString::fromLocal8Bit(g.gl_pathv[i]);
                bool already = std::any_of(devices.begin(), devices.end(),
                                          [&](const CamEntry &e){ return e.id == path; });
                if (!already)
                    devices.append({path, path, false});
            }
        }
        ::globfree(&g);
    }

    devices.append({"", "Default Camera  (let the system choose)", true});

    QStringList names;
    for (const auto &e : devices) names << e.label;

    bool ok = false;
    QString chosen = QInputDialog::getItem(this, "Select Camera",
                                          "Camera device:", names, 0, false, &ok);
    if (!ok) return;

    int idx = names.indexOf(chosen);
    const CamEntry &entry = devices[idx];

    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Camera;
    desc.path        = entry.id;
    desc.displayName = entry.isDefault ? "Default Camera"
                     : entry.label.section("  [", 0, 0).trimmed();
    if (desc.displayName.isEmpty()) desc.displayName = entry.id;
    desc.cameraIndex = idx;

    addElementNode(desc, makeIconThumb("📷"));
}

void MainWindow::onAddElementScreen() {
    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Screen;
    desc.displayName = "Screen Capture";
    desc.screenIndex = 0;

    addElementNode(desc, makeIconThumb("🖥"));
}

void MainWindow::onAddElementWindow() {
    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Window;
    desc.displayName = "Window / Tab";
    desc.windowIndex = 0;

    addElementNode(desc, makeIconThumb("🪟"));
}

void MainWindow::onAddElementColor() {
    QColor color = QColorDialog::getColor(Qt::black, this, "Pick Solid Color");
    if (!color.isValid()) return;

    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Color;
    desc.color       = color;
    desc.displayName = color.name().toUpper();

    addElementNode(desc, makeColorThumb(color));
}

void MainWindow::onAddElementShader() {
    ShaderEditDialog dlg(QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString code = dlg.resultCode().trimmed();
    if (code.isEmpty()) return;

    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Shader;
    desc.shaderCode  = code;
    desc.displayName = "Shader";

    addElementNode(desc, makeShaderThumb(code));
}

// ── Card management (obsolete methods removed - now handled by ClipNodeEditor) ──
