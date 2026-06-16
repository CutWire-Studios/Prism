#include "ui/MainWindow.h"
#include "ui_MainWindow.h"
#include "ui/VideoWidget.h"
#include "ui/ThumbHelper.h"
#include "ui/SourceFactory.h"
#include "core/ThumbnailExtractor.h"
#include "core/VideoFileSource.h"
#include "core/SlideshowSource.h"
#include "core/CameraSource.h"
#include "core/ScreenSource.h"
#include "core/CanvasSource.h"
#include "core/ImageSource.h"
#include "core/ShaderSource.h"
#include "core/HtmlSource.h"
#include "ui/ShaderEditDialog.h"
#include "ui/HtmlEditDialog.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QShortcut>
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
#include <QCloseEvent>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <numeric>

// ── Constructor ───────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setAcceptDrops(true);

    m_outputWindow = new OutputWindow(this);
    m_outputWindow->show();

    // ── Stacked widget (node editor vs empty placeholder) ─────────────────────
    m_stackWidget = new QStackedWidget(ui->gridWidget);
    ui->gridLayout->addWidget(m_stackWidget, 0, 0, 1, 1);

    m_clipNodeEditor = new ClipNodeEditor();
    m_clipNodeEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stackWidget->addWidget(m_clipNodeEditor);

    buildEmptyPlaceholder();
    m_stackWidget->addWidget(m_emptyPlaceholder);
    m_stackWidget->setCurrentWidget(m_emptyPlaceholder);

    // ── Controllers ───────────────────────────────────────────────────────────
    m_deckController = new DeckController(m_outputWindow, m_clipNodeEditor, this);

    m_hotkeyManager = new HotkeyManager(this, m_clipNodeEditor, this);
    connect(m_hotkeyManager, &HotkeyManager::deckARequested,
            this, &MainWindow::onNodeAButtonClicked);
    connect(m_hotkeyManager, &HotkeyManager::deckBRequested,
            this, &MainWindow::onNodeBButtonClicked);

    m_sessionManager = new SessionManager(
        m_clipNodeEditor, m_outputWindow->videoWidget(), &clipManager, this, this);
    connect(m_sessionManager, &SessionManager::sessionLoaded,
            this, &MainWindow::onSessionLoaded);

    m_transitionCtrl = new TransitionController(
        m_outputWindow->videoWidget(),
        ui->transitionCombo,
        ui->durationSpin,
        ui->autoBtn,
        ui->cutBtn,
        ui->crossfaderSlider,
        this);
    m_transitionCtrl->setupConnections();

    setupConnections();
    applyTheme();

    // Prime Qt Multimedia backend.
    QTimer::singleShot(0, []() {
        [[maybe_unused]] auto _ = QMediaDevices::videoInputs();
    });

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::onTimerUpdate);
    updateTimer->start(100);

    const QString autosave = SessionManager::autosavePath();
    if (QFile::exists(autosave))
        loadFromFile(autosave, false);  // will call m_sessionManager->loadFromFile

    qDebug() << "SwitchX initialized - Live Media Control Mode";
}

void MainWindow::buildEmptyPlaceholder() {
    m_emptyPlaceholder = new QWidget();
    m_emptyPlaceholder->setObjectName("emptyPlaceholder");
    m_emptyPlaceholder->setStyleSheet("#emptyPlaceholder { background-color: #141517; }");
    m_emptyPlaceholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *phLayout = new QVBoxLayout(m_emptyPlaceholder);
    phLayout->setSpacing(20);
    phLayout->setContentsMargins(40, 40, 40, 40);

    auto *phIcon = new QLabel("🎬", m_emptyPlaceholder);
    phIcon->setAlignment(Qt::AlignCenter);
    phIcon->setStyleSheet("font-size: 52px;");

    auto *phText = new QLabel(m_emptyPlaceholder);
    phText->setAlignment(Qt::AlignCenter);
    phText->setWordWrap(true);
    phText->setText(
        "<p style='font-size: 16px; font-weight: bold; color: #ccc; margin: 0 0 8px 0;'>Get Started with SwitchX</p>"
        "<p style='font-size: 13px; color: #888; line-height: 1.4; margin: 0; max-width: 420px;'>"
        "To begin, click the <b>Add Element</b> button below to load video files, import folders, or create live inputs like "
        "Camera, Screen Capture, or Canvas. Then connect ports to construct your media flow.</p>"
    );

    // Build the "Add Element" popup button using the QActions already defined in the .ui.
    auto *phBtnRow = new QWidget(m_emptyPlaceholder);
    auto *phBtns   = new QHBoxLayout(phBtnRow);
    phBtns->setContentsMargins(0, 0, 0, 0);
    phBtns->setAlignment(Qt::AlignCenter);

    auto *phAddElementBtn = new QPushButton("＋  Add Element", phBtnRow);
    phAddElementBtn->setObjectName("accentButton");
    phAddElementBtn->setStyleSheet(
        "QPushButton#accentButton {"
        "  font-size: 13px;"
        "  padding: 8px 28px 8px 20px;"
        "  min-height: 32px; height: 32px;"
        "}"
        "QPushButton#accentButton::menu-indicator {"
        "  subcontrol-origin: padding; subcontrol-position: right center; right: 8px;"
        "}"
    );

    // Reuse the QActions already declared in the .ui file.
    QMenu *addMenu = new QMenu(phAddElementBtn);
    addMenu->addAction(ui->actionAddVideoFile);
    addMenu->addAction(ui->actionAddPhoto);
    addMenu->addSeparator();
    addMenu->addAction(ui->actionAddSlideshow);
    addMenu->addSeparator();
    addMenu->addAction(ui->actionAddCamera);
    addMenu->addAction(ui->actionAddScreen);
    addMenu->addAction(ui->actionAddWindow);
    addMenu->addSeparator();
    addMenu->addAction(ui->actionAddCanvas);
    addMenu->addSeparator();
    addMenu->addAction(ui->actionAddShader);
    addMenu->addAction(ui->actionAddHtml);

    phAddElementBtn->setMenu(addMenu);
    phBtns->addWidget(phAddElementBtn);

    phLayout->addStretch(1);
    phLayout->addWidget(phIcon);
    phLayout->addWidget(phText, 0, Qt::AlignCenter);
    phLayout->addWidget(phBtnRow);
    phLayout->addStretch(1);
}

MainWindow::~MainWindow() {
    m_deckController->stopDeckAudio(true);
    m_deckController->stopDeckAudio(false);
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Build session JSON from current state and save.
    QFile file(SessionManager::autosavePath());
    if (file.open(QIODevice::WriteOnly)) {
        auto json = m_sessionManager->buildJson(
            ui->crossfaderSlider->value(),
            m_transitionCtrl->currentModeIndex(),
            m_transitionCtrl->currentDurationSecs(),
            m_deckController->activeNodeA(),
            m_deckController->activeNodeB(),
            m_hotkeyManager->nodeHotkeys());
        file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    }
    QMainWindow::closeEvent(event);
}

// ── Signal wiring ─────────────────────────────────────────────────────────────

void MainWindow::setupConnections() {
    // Menubar Media actions
    connect(ui->actionLoadFolder, &QAction::triggered, this, &MainWindow::onLoadFolderClicked);
    connect(ui->actionAddFolder,  &QAction::triggered, this, &MainWindow::onAddFolderClicked);
    connect(ui->actionAddFiles,   &QAction::triggered, this, &MainWindow::onAddFilesClicked);
    connect(ui->actionAddPhotos,  &QAction::triggered, this, &MainWindow::onAddPhotosClicked);
    connect(ui->actionClearAll,   &QAction::triggered, this, &MainWindow::onClearAllClicked);
    connect(ui->actionSaveSession,&QAction::triggered, this, &MainWindow::onSaveSessionClicked);
    connect(ui->actionLoadSession,&QAction::triggered, this, &MainWindow::onLoadSessionClicked);

    // Add Element actions (shared between menubar and placeholder button)
    connect(ui->actionAddVideoFile, &QAction::triggered, this, &MainWindow::onAddFilesClicked);
    connect(ui->actionAddPhoto,     &QAction::triggered, this, &MainWindow::onAddPhotosClicked);
    connect(ui->actionAddSlideshow, &QAction::triggered, this, &MainWindow::onAddElementSlideshow);
    connect(ui->actionAddCamera,    &QAction::triggered, this, &MainWindow::onAddElementCamera);
    connect(ui->actionAddScreen,    &QAction::triggered, this, &MainWindow::onAddElementScreen);
    connect(ui->actionAddWindow,    &QAction::triggered, this, &MainWindow::onAddElementWindow);
    connect(ui->actionAddCanvas,    &QAction::triggered, this, &MainWindow::onAddElementCanvas);
    connect(ui->actionAddShader,    &QAction::triggered, this, &MainWindow::onAddElementShader);
    connect(ui->actionAddHtml,      &QAction::triggered, this, &MainWindow::onAddElementDynamicInterface);

    // View menu
    connect(ui->actionShowOutput, &QAction::triggered, this, [this]() {
        m_outputWindow->show();
        m_outputWindow->raise();
        m_outputWindow->activateWindow();
    });
    connect(ui->actionStayOnTop, &QAction::toggled, this, [this](bool on) {
        Qt::WindowFlags flags = m_outputWindow->windowFlags();
        if (on) flags |= Qt::WindowStaysOnTopHint;
        else    flags &= ~Qt::WindowStaysOnTopHint;
        m_outputWindow->setWindowFlags(flags);
        m_outputWindow->show();
    });

    // ClipNodeEditor signals
    connect(m_clipNodeEditor, &ClipNodeEditor::deckAClipChanged,
            this, &MainWindow::onNodeAButtonClicked);
    connect(m_clipNodeEditor, &ClipNodeEditor::deckBClipChanged,
            this, &MainWindow::onNodeBButtonClicked);
    connect(m_clipNodeEditor, &ClipNodeEditor::nodeAdded,
            m_hotkeyManager, &HotkeyManager::assignHotkeyToNode);
    connect(m_clipNodeEditor, &ClipNodeEditor::nodeRemoved,
            this, &MainWindow::onNodeRemoveRequested);
    connect(m_clipNodeEditor, &ClipNodeEditor::audioGraphChanged, this, [this]() {
        if (m_deckController->activeNodeA())
            m_deckController->applyAudioControllerToDeck(true,  m_deckController->activeNodeA());
        if (m_deckController->activeNodeB())
            m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB());
    });
    connect(m_clipNodeEditor, &ClipNodeEditor::audioControllerChanged, this, [this](NodeId clipId) {
        if (clipId == m_deckController->activeNodeA())
            m_deckController->applyAudioControllerToDeck(true,  clipId);
        if (clipId == m_deckController->activeNodeB())
            m_deckController->applyAudioControllerToDeck(false, clipId);
    });

    // Deck controls
    connect(ui->aDeckPlayBtn,      &QPushButton::clicked,  this, &MainWindow::onADeckPlayClicked);
    connect(ui->bDeckPlayBtn,      &QPushButton::clicked,  this, &MainWindow::onBDeckPlayClicked);
    connect(ui->aDeckSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onADeckSpeedChanged);
    connect(ui->bDeckSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onBDeckSpeedChanged);
    connect(ui->crossfaderSlider, &QSlider::valueChanged,
            this, &MainWindow::onCrossfaderMoved);

    // Progress sliders — A deck
    connect(ui->aProgressSlider, &QSlider::sliderPressed,  this, [this]() { m_aSliderDragging = true;  });
    connect(ui->aProgressSlider, &QSlider::sliderReleased, this, [this]() {
        m_aSliderDragging = false;
        auto *out = m_outputWindow->videoWidget();
        double dur = out->getDurationA();
        if (dur > 0) {
            double t = ui->aProgressSlider->value() / 1000.0 * dur;
            out->seekA(t);
            NodeId id = m_deckController->activeNodeA();
            if (id)
                if (auto *node = m_clipNodeEditor->nodeAt(id))
                    m_deckController->updateDeckAudio(true, id, node, t, true);
        }
    });
    connect(ui->aProgressSlider, &QSlider::sliderMoved, this, [this](int value) {
        auto *out = m_outputWindow->videoWidget();
        double dur = out->getDurationA();
        if (dur > 0)
            ui->aTimeLabel->setText(
                DeckController::formatTimeShort(value / 1000.0 * dur)
                + " / " + DeckController::formatTimeShort(dur));
    });

    // Progress sliders — B deck
    connect(ui->bProgressSlider, &QSlider::sliderPressed,  this, [this]() { m_bSliderDragging = true;  });
    connect(ui->bProgressSlider, &QSlider::sliderReleased, this, [this]() {
        m_bSliderDragging = false;
        auto *out = m_outputWindow->videoWidget();
        double dur = out->getDurationB();
        if (dur > 0) {
            double t = ui->bProgressSlider->value() / 1000.0 * dur;
            out->seekB(t);
            NodeId id = m_deckController->activeNodeB();
            if (id)
                if (auto *node = m_clipNodeEditor->nodeAt(id))
                    m_deckController->updateDeckAudio(false, id, node, t, true);
        }
    });
    connect(ui->bProgressSlider, &QSlider::sliderMoved, this, [this](int value) {
        auto *out = m_outputWindow->videoWidget();
        double dur = out->getDurationB();
        if (dur > 0)
            ui->bTimeLabel->setText(
                DeckController::formatTimeShort(value / 1000.0 * dur)
                + " / " + DeckController::formatTimeShort(dur));
    });
}

// ── Folder / file loading ─────────────────────────────────────────────────────

void MainWindow::onLoadFolderClicked() {
    QString path = QFileDialog::getExistingDirectory(this, "Select Media Folder");
    if (path.isEmpty()) return;
    m_clipNodeEditor->clearAllNodes();
    clipManager.loadFolder(path);
    m_deckController->setActiveNodeA(0);
    m_deckController->setActiveNodeB(0);
    m_deckController->stopDeckAudio(true);
    m_deckController->stopDeckAudio(false);
    m_outputWindow->videoWidget()->setNodeChainA({});
    m_outputWindow->videoWidget()->setNodeChainB({});
    for (int i = 0; i < clipManager.getClipCount(); ++i) {
        const QString p = clipManager.getClipPath(i);
        m_clipNodeEditor->addClipNode(p, ThumbnailExtractor::extract(p, 110, 65));
    }
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::onAddFolderClicked() {
    QString path = QFileDialog::getExistingDirectory(this, "Add Media Folder");
    if (path.isEmpty()) return;
    const QStringList before = clipManager.getClips();
    clipManager.addFolder(path);
    QStringList added;
    for (const QString &p : clipManager.getClips())
        if (!before.contains(p)) added << p;
    appendClipsToEditor(added);
}

void MainWindow::onAddFilesClicked() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Add Media Files", "",
        "Media Files (*.mp4 *.avi *.mov *.mkv *.webm *.png *.jpg *.jpeg *.bmp *.webp *.gif)");
    if (files.isEmpty()) return;
    const QStringList before = clipManager.getClips();
    clipManager.addFiles(files);
    QStringList added;
    for (const QString &p : clipManager.getClips())
        if (!before.contains(p)) added << p;
    appendClipsToEditor(added);
}

void MainWindow::onAddPhotosClicked() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Add Photos", "",
        "Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif)");
    if (files.isEmpty()) return;
    const QStringList before = clipManager.getClips();
    clipManager.addFiles(files);
    QStringList added;
    for (const QString &p : clipManager.getClips())
        if (!before.contains(p)) added << p;
    appendClipsToEditor(added);
}

void MainWindow::onClearAllClicked() {
    clipManager.clear();
    m_clipNodeEditor->clearAllNodes();
    m_deckController->setActiveNodeA(0);
    m_deckController->setActiveNodeB(0);
    m_deckController->stopDeckAudio(true);
    m_deckController->stopDeckAudio(false);
    m_outputWindow->videoWidget()->setNodeChainA({});
    m_outputWindow->videoWidget()->setNodeChainB({});
    m_stackWidget->setCurrentWidget(m_emptyPlaceholder);
}

// ── Element management ────────────────────────────────────────────────────────

void MainWindow::addElementNode(const SourceDescriptor &desc, const QPixmap &thumb) {
    m_clipNodeEditor->addSourceNode(desc, thumb);
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::appendClipsToEditor(const QStringList &clipPaths) {
    if (clipPaths.isEmpty()) return;
    for (const QString &path : clipPaths)
        m_clipNodeEditor->addClipNode(path, ThumbnailExtractor::extract(path, 110, 65));
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

// ── Node deck assignment ──────────────────────────────────────────────────────

void MainWindow::onNodeAButtonClicked(NodeId nodeId) {
    if (!nodeId) return;
    auto *node = m_clipNodeEditor->nodeAt(nodeId);
    if (!node || !node->hasSource()) return;

    if (m_deckController->activeNodeA())
        if (auto *old = m_clipNodeEditor->nodeAt(m_deckController->activeNodeA()))
            old->setASelected(false);
    m_deckController->setActiveNodeA(nodeId);
    node->setASelected(true);

    int canvasW = 0, canvasH = 0;
    m_clipNodeEditor->contextCanvasSize(nodeId, canvasW, canvasH);
    auto *out = m_outputWindow->videoWidget();
    out->setCanvasSizeA(canvasW, canvasH);

    m_deckController->assignNodeToDeck(node, nodeId, true,
        ui->aProgressSlider, ui->aDeckPlayBtn, ui->aSelectedLabel, ui->aTimeLabel);
    out->setNodeChainA(SourceFactory::buildChain(
        m_clipNodeEditor->getClipChain(nodeId), m_clipNodeEditor, canvasW, canvasH));
}

void MainWindow::onNodeBButtonClicked(NodeId nodeId) {
    if (!nodeId) return;
    auto *node = m_clipNodeEditor->nodeAt(nodeId);
    if (!node || !node->hasSource()) return;

    if (m_deckController->activeNodeB())
        if (auto *old = m_clipNodeEditor->nodeAt(m_deckController->activeNodeB()))
            old->setBSelected(false);
    m_deckController->setActiveNodeB(nodeId);
    node->setBSelected(true);

    int canvasW = 0, canvasH = 0;
    m_clipNodeEditor->contextCanvasSize(nodeId, canvasW, canvasH);
    auto *out = m_outputWindow->videoWidget();
    out->setCanvasSizeB(canvasW, canvasH);

    m_deckController->assignNodeToDeck(node, nodeId, false,
        ui->bProgressSlider, ui->bDeckPlayBtn, ui->bSelectedLabel, ui->bTimeLabel);
    out->setNodeChainB(SourceFactory::buildChain(
        m_clipNodeEditor->getClipChain(nodeId), m_clipNodeEditor, canvasW, canvasH));
}

void MainWindow::onNodeRemoveRequested(NodeId nodeId) {
    m_hotkeyManager->releaseHotkeyForNode(nodeId);
    m_clipNodeEditor->removeNode(nodeId);
    auto *out = m_outputWindow->videoWidget();
    if (m_deckController->activeNodeA() == nodeId) { m_deckController->setActiveNodeA(0); out->setNodeChainA({}); }
    if (m_deckController->activeNodeB() == nodeId) { m_deckController->setActiveNodeB(0); out->setNodeChainB({}); }
    if (!m_deckController->activeNodeA()) m_deckController->stopDeckAudio(true);
    if (!m_deckController->activeNodeB()) m_deckController->stopDeckAudio(false);
    if (m_clipNodeEditor->allNodes().isEmpty())
        m_stackWidget->setCurrentWidget(m_emptyPlaceholder);
}

// ── Crossfader / deck play ────────────────────────────────────────────────────

void MainWindow::onCrossfaderMoved(int value) {
    m_outputWindow->videoWidget()->setCrossfade(value / 100.f);
    if (m_deckController->activeNodeA())
        m_deckController->applyAudioControllerToDeck(true,  m_deckController->activeNodeA());
    if (m_deckController->activeNodeB())
        m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB());
}

void MainWindow::onADeckPlayClicked() {
    auto *out = m_outputWindow->videoWidget();
    if (out->isPlayingA()) out->pauseA(); else out->playA();
    m_deckController->applyAudioControllerToDeck(true, m_deckController->activeNodeA());
}

void MainWindow::onBDeckPlayClicked() {
    auto *out = m_outputWindow->videoWidget();
    if (out->isPlayingB()) out->pauseB(); else out->playB();
    m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB());
}

void MainWindow::onADeckSpeedChanged(int value) {
    qDebug() << "A Deck Speed:" << value << "%";
}

void MainWindow::onBDeckSpeedChanged(int value) {
    qDebug() << "B Deck Speed:" << value << "%";
}

// ── Timer update (preview labels + progress sliders) ─────────────────────────

void MainWindow::onTimerUpdate() {
    auto *out = m_outputWindow->videoWidget();

    // A deck
    double durA  = out->getDurationA();
    double timeA = out->getCurrentTimeA();
    if (durA > 0) {
        if (!m_aSliderDragging) {
            ui->aProgressSlider->blockSignals(true);
            ui->aProgressSlider->setValue((int)(timeA / durA * 1000));
            ui->aProgressSlider->blockSignals(false);
        }
        ui->aTimeLabel->setText(
            DeckController::formatTimeShort(timeA) + " / " + DeckController::formatTimeShort(durA));

        // Restart audio on loop/backward jump
        if (timeA < m_deckController->lastTimeA() - 0.2) {
            NodeId id = m_deckController->activeNodeA();
            if (id)
                if (auto *node = m_clipNodeEditor->nodeAt(id))
                    m_deckController->updateDeckAudio(true, id, node, timeA, true);
        }
        m_deckController->setLastTimeA(timeA);
    }
    ui->aDeckPlayBtn->setText(out->isPlayingA() ? "⏸" : "▶");

    QImage frameA = out->getFrameA();
    if (!frameA.isNull())
        ui->aPreviewLabel->setPixmap(QPixmap::fromImage(
            frameA.scaled(160, 90, Qt::KeepAspectRatio, Qt::FastTransformation)));

    // B deck
    double durB  = out->getDurationB();
    double timeB = out->getCurrentTimeB();
    if (durB > 0) {
        if (!m_bSliderDragging) {
            ui->bProgressSlider->blockSignals(true);
            ui->bProgressSlider->setValue((int)(timeB / durB * 1000));
            ui->bProgressSlider->blockSignals(false);
        }
        ui->bTimeLabel->setText(
            DeckController::formatTimeShort(timeB) + " / " + DeckController::formatTimeShort(durB));

        if (timeB < m_deckController->lastTimeB() - 0.2) {
            NodeId id = m_deckController->activeNodeB();
            if (id)
                if (auto *node = m_clipNodeEditor->nodeAt(id))
                    m_deckController->updateDeckAudio(false, id, node, timeB, true);
        }
        m_deckController->setLastTimeB(timeB);
    }
    ui->bDeckPlayBtn->setText(out->isPlayingB() ? "⏸" : "▶");

    QImage frameB = out->getFrameB();
    if (!frameB.isNull())
        ui->bPreviewLabel->setPixmap(QPixmap::fromImage(
            frameB.scaled(160, 90, Qt::KeepAspectRatio, Qt::FastTransformation)));
}

// ── Drag & drop ───────────────────────────────────────────────────────────────

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasUrls()) return;

    const QStringList before = clipManager.getClips();
    QStringList filePaths;
    for (const QUrl &url : mimeData->urls()) {
        const QString p = url.toLocalFile();
        QFileInfo fi(p);
        if (fi.isDir()) clipManager.addFolder(p);
        else            filePaths << p;
    }
    if (!filePaths.isEmpty()) clipManager.addFiles(filePaths);

    QStringList added;
    for (const QString &p : clipManager.getClips())
        if (!before.contains(p)) added << p;
    appendClipsToEditor(added);

    event->acceptProposedAction();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
}

// ── Theme ─────────────────────────────────────────────────────────────────────

void MainWindow::applyTheme() {
    qApp->setStyle("fusion");
    qApp->setStyleSheet(R"(
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
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #2a2c30,stop:1 #1e1f22);
            color: #E0E0E0;
            border-top: 1px solid #33363b;
            border-left: 1px solid #33363b;
            border-bottom: 1px solid #151618;
            border-right: 1px solid #151618;
            border-radius: 6px;
            padding: 4px 10px;
            font-weight: bold;
            font-size: 11px;
            min-height: 22px; height: 22px;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #2e3136,stop:1 #222326);
            color: #FFFFFF;
        }
        QPushButton:pressed {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #191a1c,stop:1 #2b2d32);
            border-top: 1px solid #121314; border-left: 1px solid #121314;
            border-bottom: 1px solid #3a3d43; border-right: 1px solid #3a3d43;
            color: #aaaaaa;
        }
        QPushButton#accentButton, QPushButton[text*="Load"], QPushButton[text*="Play"], QPushButton[text*="Fullscreen"] {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #3a6670,stop:1 #1f3d45);
            color: #FFFFFF;
            border-top: 1px solid #4a7f8c; border-left: 1px solid #4a7f8c;
            border-bottom: 1px solid #112226; border-right: 1px solid #112226;
            padding: 4px 12px; font-size: 11px; min-height: 22px; height: 22px;
        }
        QPushButton#accentButton:pressed, QPushButton[text*="Play"]:pressed {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #15292e,stop:1 #2f545c);
            border-top: 1px solid #0f1d21; border-left: 1px solid #0f1d21;
        }
        QScrollBar:vertical { background-color: #1c1d1f; width: 12px; border-radius: 6px; }
        QScrollBar::handle:vertical { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #32353a,stop:1 #242528); min-height: 20px; border-radius: 6px; border: 1px solid #151618; }
        QScrollBar::handle:vertical:hover { background-color: #3d4147; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { background: none; height: 0px; }
        QScrollBar:horizontal { background-color: #1c1d1f; height: 12px; border-radius: 6px; }
        QScrollBar::handle:horizontal { background-color: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #32353a,stop:1 #242528); min-width: 20px; border-radius: 6px; border: 1px solid #151618; }
        QScrollBar::handle:horizontal:hover { background-color: #3d4147; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { background: none; width: 0px; }
        QSlider::groove:horizontal { border: 1px solid #1c1d1f; height: 6px; background: #18191b; border-radius: 3px; }
        QSlider::sub-page:horizontal { background: #2a5c66; border-radius: 3px; }
        QSlider::handle:horizontal { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #3a3d43,stop:1 #1c1d1f); border: 1px solid #4a4e56; width: 14px; margin-top: -5px; margin-bottom: -5px; border-radius: 7px; }
        QSlider::handle:horizontal:hover { background: #4a4e56; }
        QSlider::groove:vertical { border: 1px solid #1c1d1f; width: 6px; background: #18191b; border-radius: 3px; }
        QSlider::handle:vertical { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #3a3d43,stop:1 #1c1d1f); border: 1px solid #4a4e56; height: 14px; margin-left: -5px; margin-right: -5px; border-radius: 7px; }
        QLabel { color: #E0E0E0; background-color: transparent; font-size: 12px; }
        QSpinBox, QDoubleSpinBox {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #2a2c30,stop:1 #1e1f22);
            color: #E0E0E0;
            border-top: 1px solid #33363b; border-left: 1px solid #33363b;
            border-bottom: 1px solid #151618; border-right: 1px solid #151618;
            border-radius: 6px; padding: 4px;
            selection-background-color: #2a5c66;
        }
        QSpinBox:hover, QDoubleSpinBox:hover {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #2e3136,stop:1 #222326);
        }
        QSpinBox::up-button, QSpinBox::down-button,
        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
            background-color: #1e1f22; border: 1px solid #151618; border-radius: 3px; width: 16px;
        }
        QSpinBox::up-button:pressed, QSpinBox::down-button:pressed,
        QDoubleSpinBox::up-button:pressed, QDoubleSpinBox::down-button:pressed {
            background-color: #151618;
        }
        QListWidget { background-color: #1c1d1f; border: 1px solid #151618; border-radius: 8px; color: #E0E0E0; }
        QListWidget::item { padding: 4px; }
        QListWidget::item:selected { background-color: #2a5c66; color: #FFFFFF; }
        QListWidget::item:hover { background-color: #2a2c30; }
    )");
}

// ── Session save / load ───────────────────────────────────────────────────────

void MainWindow::onSaveSessionClicked() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Session", QString(), "SwitchX Session (*.sxs);;All Files (*)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".sxs", Qt::CaseInsensitive)) path += ".sxs";

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Save Session",
                             QString("Cannot write to file:\n%1").arg(path));
        return;
    }
    auto json = m_sessionManager->buildJson(
        ui->crossfaderSlider->value(),
        m_transitionCtrl->currentModeIndex(),
        m_transitionCtrl->currentDurationSecs(),
        m_deckController->activeNodeA(),
        m_deckController->activeNodeB(),
        m_hotkeyManager->nodeHotkeys());
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
}

void MainWindow::onLoadSessionClicked() {
    QString path = QFileDialog::getOpenFileName(
        this, "Load Session", QString(), "SwitchX Session (*.sxs);;All Files (*)");
    if (path.isEmpty()) return;
    loadFromFile(path, true);
}

void MainWindow::loadFromFile(const QString &path, bool showErrors) {
    m_deckController->setActiveNodeA(0);
    m_deckController->setActiveNodeB(0);
    m_deckController->stopDeckAudio(true);
    m_deckController->stopDeckAudio(false);

    // Block auto-hotkey assignment during restore.
    disconnect(m_clipNodeEditor, &ClipNodeEditor::nodeAdded,
               m_hotkeyManager, &HotkeyManager::assignHotkeyToNode);

    if (!m_sessionManager->loadFromFile(path, showErrors)) {
        connect(m_clipNodeEditor, &ClipNodeEditor::nodeAdded,
                m_hotkeyManager, &HotkeyManager::assignHotkeyToNode);
        return;
    }

    connect(m_clipNodeEditor, &ClipNodeEditor::nodeAdded,
            m_hotkeyManager, &HotkeyManager::assignHotkeyToNode);
}

void MainWindow::onSessionLoaded() {
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);

    // Restore hotkeys.
    m_hotkeyManager->restoreHotkeys(m_sessionManager->restoredHotkeys());

    // Restore crossfader + transition settings.
    ui->crossfaderSlider->setValue(m_sessionManager->restoredCrossfader());
    ui->transitionCombo->setCurrentIndex(m_sessionManager->restoredTransitionMode());
    ui->durationSpin->setValue(m_sessionManager->restoredTransitionDuration());

    // Re-assign active decks.
    const NodeId savedA = m_sessionManager->restoredActiveNodeA();
    const NodeId savedB = m_sessionManager->restoredActiveNodeB();
    if (savedA) onNodeAButtonClicked(savedA);
    if (savedB) onNodeBButtonClicked(savedB);
}

// ── Add Element handlers ──────────────────────────────────────────────────────

void MainWindow::onAddElementSlideshow() {
    QString folder = QFileDialog::getExistingDirectory(this, "Select Image Folder for Slideshow");
    if (folder.isEmpty()) return;

    bool ok = false;
    int interval = QInputDialog::getInt(this, "Slideshow Interval",
                                        "Seconds per slide:", 3, 1, 60, 1, &ok);
    if (!ok) return;

    QDir dir(folder);
    QStringList imgs = dir.entryList({"*.png","*.jpg","*.jpeg","*.bmp","*.webp"},
                                     QDir::Files, QDir::Name);
    QPixmap thumb;
    if (!imgs.isEmpty())
        thumb = ThumbnailExtractor::extract(dir.absoluteFilePath(imgs.first()), 110, 65);
    if (thumb.isNull()) thumb = ThumbHelper::makeIconThumb("📁");

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

    struct CamEntry { QString id, label; bool isDefault; };
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
                if (!already) devices.append({path, path, false});
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

    addElementNode(desc, ThumbHelper::makeIconThumb("📷"));
}

void MainWindow::onAddElementScreen() {
    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Screen;
    desc.displayName = "Screen Capture";
    desc.screenIndex = 0;
    addElementNode(desc, ThumbHelper::makeIconThumb("🖥"));
}

void MainWindow::onAddElementWindow() {
    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Window;
    desc.displayName = "Window / Tab";
    desc.windowIndex = 0;
    addElementNode(desc, ThumbHelper::makeIconThumb("🪟"));
}

void MainWindow::onAddElementCanvas() {
    struct CanvasPreset { const char *label; int width, height; };
    const CanvasPreset presets[] = {
        {"16:9  (1280x720)",  1280, 720},
        {"4:3  (1024x768)",   1024, 768},
        {"1:1  (1080x1080)", 1080, 1080},
        {"9:16  (1080x1920)",1080, 1920},
    };

    QStringList options;
    for (const auto &p : presets) options << QString::fromUtf8(p.label);
    options << "Custom…";

    bool ok = false;
    const QString choice = QInputDialog::getItem(this, "Canvas",
                                                  "Aspect ratio:", options, 0, false, &ok);
    if (!ok || choice.isEmpty()) return;

    int width = 1280, height = 720;
    if (choice == "Custom…") {
        width  = QInputDialog::getInt(this, "Canvas Width",  "Width:",  1280, 16, 16384, 1, &ok); if (!ok) return;
        height = QInputDialog::getInt(this, "Canvas Height", "Height:", 720,  16, 16384, 1, &ok); if (!ok) return;
    } else {
        for (const auto &p : presets)
            if (choice == QString::fromUtf8(p.label)) { width = p.width; height = p.height; break; }
    }

    const int g = std::gcd(width, height);
    const QString ratioText = QString("%1:%2").arg(width / g).arg(height / g);

    const QStringList fillOptions = {"Checkered", "Transparent", "Color"};
    const QString fillChoice = QInputDialog::getItem(this, "Canvas Fill",
                                                      "Fill type:", fillOptions, 0, false, &ok);
    if (!ok || fillChoice.isEmpty()) return;

    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Canvas;
    desc.canvasWidth = width; desc.canvasHeight = height;
    desc.canvasFill  = SourceDescriptor::CanvasFill::Checkered;
    desc.color       = Qt::white;
    QString fillLabel = "Checkered";

    if (fillChoice == "Transparent") {
        desc.canvasFill = SourceDescriptor::CanvasFill::Transparent;
        fillLabel = "Transparent";
    } else if (fillChoice == "Color") {
        QColor c = QColorDialog::getColor(Qt::white, this, "Pick Canvas Color");
        if (!c.isValid()) return;
        desc.canvasFill = SourceDescriptor::CanvasFill::Color;
        desc.color = c;
        fillLabel = c.name().toUpper();
    }

    desc.displayName = QString("Canvas %1 (%2)").arg(ratioText, fillLabel);
    addElementNode(desc, ThumbHelper::makeCanvasThumb(ratioText, desc.canvasFill, desc.color));
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

    addElementNode(desc, ThumbHelper::makeShaderThumb(code));
}

void MainWindow::onAddElementDynamicInterface() {
    HtmlEditDialog dlg(QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString filePath = dlg.resultFilePath();
    QString html     = dlg.resultHtml().trimmed();
    if (filePath.isEmpty() && html.isEmpty()) return;

    SourceDescriptor desc;
    desc.kind        = SourceDescriptor::Kind::Html;
    desc.htmlContent = html;
    desc.path        = filePath;
    desc.displayName = filePath.isEmpty() ? "HTML Overlay"
                                          : QFileInfo(filePath).fileName();

    addElementNode(desc, ThumbHelper::makeHtmlThumb(html, filePath));
}
