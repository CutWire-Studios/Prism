#include "ui/mainwindow/MainWindow.h"
#include "ui_MainWindow.h"
#include "ui/canvas/VideoWidget.h"
#include "ui/output/MirrorOutputWindow.h"
#include "ui/mainwindow/SourcePrompt.h"
#include "ui/mainwindow/SourceFactory.h"
#include "core/media/ThumbnailExtractor.h"
#include "core/sources/VideoFileSource.h"
#include "core/sources/SlideshowSource.h"
#include "core/sources/CameraSource.h"
#include "core/sources/ScreenSource.h"
#include "core/sources/CanvasSource.h"
#include "core/sources/ImageSource.h"
#include "core/sources/ShaderSource.h"
#include "core/sources/HtmlSource.h"
#include "core/sources/NdiSource.h"
#ifdef PRISM_HAVE_WEBRTC
#include "core/sources/WebRtcSource.h"
#endif
#include "ui/obs/ObsWebSocketClient.h"
#include "ui/hotkeys/HotkeyEditorDialog.h"
#include "ui/session/SessionRecoveryDialog.h"
#include "ui/remote/RemoteControlServer.h"
#include "ui/remote/RemoteServerDialog.h"
#include "ui/mainwindow/MainWindowUtils.h"
#include "ui/common/MaterialSymbols.h"
#include "ui/output/FrameCaptureHelper.h"
#include "ui/recording/RecordingSettingsDialog.h"
#include "core/project/ClipManager.h"
#include "ui/common/AssetLibrary.h"
#include "ui/common/ThumbHelper.h"
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QShortcut>
#include <QKeySequence>
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
#include <QLineEdit>
#include <QInputDialog>
#include <QMessageBox>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QStackedWidget>
#include <QCloseEvent>
#include <QStatusBar>
#include <QFont>
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
    m_baseWindowTitle = windowTitle();

    MaterialSymbols::setActionIcon(ui->actionLoadFolder, MaterialSymbols::Names::Description);
    MaterialSymbols::setActionIcon(ui->actionAddFolder, MaterialSymbols::Names::FolderOpen);
    MaterialSymbols::setActionIcon(ui->actionSaveSession, MaterialSymbols::Names::Save);
    MaterialSymbols::setActionIcon(ui->actionLoadSession, MaterialSymbols::Names::FolderOpen);
    MaterialSymbols::setActionIcon(ui->actionExportProject, MaterialSymbols::Names::Inventory);
    MaterialSymbols::setActionIcon(ui->actionImportProject, MaterialSymbols::Names::Download);
    MaterialSymbols::setActionIcon(ui->actionClearAll, MaterialSymbols::Names::Delete);
    MaterialSymbols::setPlayPause(ui->aDeckPlayBtn, false, 22);
    MaterialSymbols::setPlayPause(ui->bDeckPlayBtn, false, 22);

    // Deck transport controls stay hidden until a clip that supports them is
    // assigned (pushDecks / assignNodeToDeck toggle them per capability).
    for (QWidget *w : std::initializer_list<QWidget *>{
             ui->aProgressSlider, ui->aDeckPlayBtn, ui->aDeckSpeedSlider, ui->speedLabelALabel,
             ui->bProgressSlider, ui->bDeckPlayBtn, ui->bDeckSpeedSlider, ui->speedLabelBLabel})
        w->setVisible(false);

    const RecordingOptions recOpts = RecordingSettingsDialog::loadSavedOptions();

    m_outputWindow = new OutputWindow();
    m_outputWindow->show();

    setupPreviewSplitters();
    setupRecordingStatusBar();

    m_outputHub = new OutputHub(this);
    m_outputHub->setProgramSource(m_outputWindow->videoWidget());
    m_outputHub->setOutputDir(recOpts.effectiveOutputDir());
    m_outputWindow->videoWidget()->addDeckPreviewConsumer();

    m_obsIntegration = new ObsIntegration(this);
    m_obsScenesMenu = new QMenu(tr("OBS Scenes"), this);
    ui->menuView->insertMenu(ui->actionStayOnTop, m_obsScenesMenu);
    m_obsScenesMenu->setEnabled(false);
    rebuildObsScenesMenu({});
    ui->actionConnectObs->setEnabled(ObsWebSocketClient::isAvailable());
    ui->actionLinkClipObsScene->setEnabled(ObsWebSocketClient::isAvailable());
    if (!ObsWebSocketClient::isAvailable()) {
        const QString tip = tr("Requires qt6-websockets at build time");
        ui->actionConnectObs->setToolTip(tip);
        ui->actionLinkClipObsScene->setToolTip(tip);
    }

    ui->actionNdiOutput->setEnabled(m_outputHub->ndiAvailable());
    if (!m_outputHub->ndiAvailable()) {
        ui->actionNdiOutput->setToolTip(
            tr("NDI SDK not found at build time. Install the NDI SDK and rebuild with -DNDI_ROOT=…"));
    }

    ui->actionVirtualCameraOutput->setEnabled(m_outputHub->virtualCameraAvailable());
    if (!m_outputHub->virtualCameraAvailable()) {
        ui->actionVirtualCameraOutput->setToolTip(
            tr("Virtual camera output is not available on this platform."));
    } else {
        const QString dev = m_outputHub->virtualCameraDevicePath();
#ifdef Q_OS_LINUX
        ui->actionVirtualCameraOutput->setToolTip(
            tr("Expose the program mix as a webcam via %1 (v4l2loopback). "
               "Select as a camera source in OBS, Zoom, etc.")
                .arg(dev.isEmpty() ? tr("a v4l2loopback device") : dev));
#else
        ui->actionVirtualCameraOutput->setToolTip(
            tr("Expose the program mix as a webcam named \"%1\". "
               "Select it as a camera source in OBS, Zoom, browsers, etc.")
                .arg(dev.isEmpty() ? QStringLiteral("DirectShow Softcam") : dev));
#endif
    }

    setupAddElementMenu(ui->menuAddElement);

    // ── Asset library + node editor (left sidebar + canvas) ───────────────────
    m_assetLibrary = new AssetLibrary(&clipManager);
    m_assetLibrary->setMinimumWidth(160);

    m_stackWidget = new QStackedWidget(ui->gridWidget);
    m_clipNodeEditor = new ClipNodeEditor();
    m_clipNodeEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stackWidget->addWidget(m_clipNodeEditor);

    buildEmptyPlaceholder();
    m_stackWidget->addWidget(m_emptyPlaceholder);
    m_stackWidget->setCurrentWidget(m_emptyPlaceholder);

    m_editorSplitter = new QSplitter(Qt::Horizontal, ui->gridWidget);
    m_editorSplitter->addWidget(m_assetLibrary);
    m_editorSplitter->addWidget(m_stackWidget);
    m_editorSplitter->setStretchFactor(0, 0);
    m_editorSplitter->setStretchFactor(1, 1);
    m_editorSplitter->setSizes({220, 800});
    ui->gridLayout->addWidget(m_editorSplitter, 0, 0, 1, 1);

    connect(m_assetLibrary, &AssetLibrary::addAsClipRequested,
            this, [this](const QString &path, const QPixmap &thumb) {
        m_clipNodeEditor->addClipNode(path, thumb);
        m_stackWidget->setCurrentWidget(m_clipNodeEditor);
    });

    // ── Controllers ───────────────────────────────────────────────────────────
    m_deckController = new DeckController(m_outputWindow, m_clipNodeEditor, m_outputHub, this);

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
        ui->durationSlider,
        ui->autoBtn,
        ui->cutBtn,
        ui->crossfaderSlider,
        this);
    m_transitionCtrl->setupConnections();

    m_remoteServer = new RemoteControlServer(this, m_transitionCtrl, ui->crossfaderSlider, this);

    setupConnections();
    applyTheme();

    // Prime Qt Multimedia backend.
    QTimer::singleShot(0, []() {
        [[maybe_unused]] auto _ = QMediaDevices::videoInputs();
    });

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::onTimerUpdate);
    updateTimer->start(100);

    handleStartupRecovery();

    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::performAutosave);
    m_autosaveTimer->start(SessionManager::kDefaultAutosaveIntervalMs);

    SessionManager::markRunning();

    qDebug() << "CutWire Prism initialized - Live Media Control Mode";
}

void MainWindow::buildEmptyPlaceholder() {
    m_emptyPlaceholder = new QWidget();
    m_emptyPlaceholder->setObjectName("emptyPlaceholder");
    m_emptyPlaceholder->setStyleSheet("#emptyPlaceholder { background-color: #141517; }");
    m_emptyPlaceholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *phLayout = new QVBoxLayout(m_emptyPlaceholder);
    phLayout->setSpacing(20);
    phLayout->setContentsMargins(40, 40, 40, 40);

    auto *phIcon = new QLabel(m_emptyPlaceholder);
    MaterialSymbols::setLabelText(phIcon, MaterialSymbols::Names::Movie, 52);
    phIcon->setAlignment(Qt::AlignCenter);

    auto *phText = new QLabel(m_emptyPlaceholder);
    phText->setAlignment(Qt::AlignCenter);
    phText->setWordWrap(true);
    phText->setText(
        "<p style='font-size: 16px; font-weight: bold; color: #ccc; margin: 0 0 8px 0;'>Get Started with CutWire Prism</p>"
        "<p style='font-size: 13px; color: #888; line-height: 1.4; margin: 0; max-width: 420px;'>"
        "To begin, click the <b>Add Element</b> button below to load video files, import folders, or create live inputs like "
        "Camera, Screen Capture, or Canvas. Then connect ports to construct your media flow.</p>"
    );

    // Build the "Add Element" popup button.
    auto *phBtnRow = new QWidget(m_emptyPlaceholder);
    auto *phBtns   = new QHBoxLayout(phBtnRow);
    phBtns->setContentsMargins(0, 0, 0, 0);
    phBtns->setAlignment(Qt::AlignCenter);

    auto *phAddElementBtn = new QPushButton(tr("Add Element"), phBtnRow);
    phAddElementBtn->setIcon(MaterialSymbols::icon(MaterialSymbols::Names::Add, 16));
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

    QMenu *addMenu = new QMenu(phAddElementBtn);
    setupAddElementMenu(addMenu);
    phAddElementBtn->setMenu(addMenu);
    phBtns->addWidget(phAddElementBtn);

    phLayout->addStretch(1);
    phLayout->addWidget(phIcon);
    phLayout->addWidget(phText, 0, Qt::AlignCenter);
    phLayout->addWidget(phBtnRow);
    phLayout->addStretch(1);
}

MainWindow::~MainWindow() {
    shutdownLivePipeline();
    if (m_outputHub) {
        m_outputHub->setNdiOutputEnabled(false);
        m_outputHub->setVirtualCameraEnabled(false);
    }
    if (m_clipNodeEditor) {
        m_clipNodeEditor->blockSignals(true);
        disconnect(m_clipNodeEditor, nullptr, nullptr, nullptr);
    }
    delete ui;
    ui = nullptr;
    m_clipNodeEditor = nullptr;
    m_stackWidget = nullptr;
    m_assetLibrary = nullptr;
    m_editorSplitter = nullptr;
    m_emptyPlaceholder = nullptr;
}

void MainWindow::shutdownLivePipeline() {
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    if (updateTimer)
        updateTimer->stop();
    if (m_autosaveTimer)
        m_autosaveTimer->stop();

    if (m_remoteServer)
        m_remoteServer->stopServer();

    if (m_transitionCtrl)
        m_transitionCtrl->shutdown();

    if (m_deckController) {
        m_deckController->setActiveNodeA(0);
        m_deckController->setActiveNodeB(0);
        m_deckController->releaseAllDeckAudio();
        m_deckController->releaseAllMasterAudioInputs();
    }
    m_deckBaseA.clear(); m_deckOverlaysA.clear();
    m_deckBaseB.clear(); m_deckOverlaysB.clear();

    if (m_outputHub) {
        disconnect(m_outputHub, nullptr, this, nullptr);
        m_outputHub->stopAllRecording();
        m_outputHub->setProgramSource(nullptr);
    }

    if (m_outputWindow) {
        if (VideoWidget *vw = m_outputWindow->videoWidget()) {
            vw->removeDeckPreviewConsumer();
            vw->shutdownPlayback();
        }
    }

    if (m_obsIntegration)
        m_obsIntegration->disconnectFromObs();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    shutdownLivePipeline();
    if (m_outputWindow)
        m_outputWindow->close();
    performAutosave();
    SessionManager::markCleanExit();
    QMainWindow::closeEvent(event);
}

QJsonObject MainWindow::currentSessionJson() const {
    return currentSessionJson(SessionManager::autosavePath());
}

QJsonObject MainWindow::currentSessionJson(const QString &sessionFilePath) const {
    return m_sessionManager->buildJson(
        ui->crossfaderSlider->value(),
        m_transitionCtrl->currentModeIndex(),
        m_transitionCtrl->currentDurationSecs(),
        m_deckController->activeNodeA(),
        m_deckController->activeNodeB(),
        m_hotkeyManager->nodeHotkeys(),
        sessionFilePath);
}

void MainWindow::performAutosave() {
    if (!m_sessionManager)
        return;
    m_sessionManager->saveAutosave(currentSessionJson());
}

void MainWindow::handleStartupRecovery() {
    const SessionManager::RecoveryInfo recovery = SessionManager::checkRecovery();
    if (recovery.uncleanShutdown) {
        SessionRecoveryDialog dlg(recovery.autosavePath, recovery.backupPaths, this);
        if (dlg.exec() != QDialog::Accepted)
            return;

        switch (dlg.choice()) {
        case SessionRecoveryDialog::Choice::RecoverAutosave:
            if (QFile::exists(recovery.autosavePath))
                loadFromFile(recovery.autosavePath, false);
            break;
        case SessionRecoveryDialog::Choice::RecoverBackup: {
            const QString path = dlg.selectedBackupPath();
            if (!path.isEmpty())
                loadFromFile(path, false);
            break;
        }
        case SessionRecoveryDialog::Choice::StartFresh:
            break;
        }
        return;
    }

    const QString autosave = SessionManager::autosavePath();
    if (QFile::exists(autosave))
        loadFromFile(autosave, false);
}

// ── Preview splitters (manual resize via dividers) ────────────────────────────

void MainWindow::setupDeckPreviewSplitter(bool deckA) {
    QSplitter *split = deckA ? ui->aPreviewSplitter : ui->bPreviewSplitter;
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({120, 160});

    (deckA ? ui->aDeckLayout : ui->bDeckLayout)->setStretch(1, 1);
}

void MainWindow::setupPreviewSplitters() {
    ui->deckHorizontalSplitter->setStretchFactor(0, 0);
    ui->deckHorizontalSplitter->setStretchFactor(1, 1);
    ui->deckHorizontalSplitter->setStretchFactor(2, 0);
    ui->deckHorizontalSplitter->setSizes({220, 200, 220});

    setupDeckPreviewSplitter(true);
    setupDeckPreviewSplitter(false);

    const auto connectSplitter = [this](QSplitter *splitter) {
        connect(splitter, &QSplitter::splitterMoved,
                this, &MainWindow::refreshPreviewPixmaps);
    };
    connectSplitter(ui->deckHorizontalSplitter);
    connectSplitter(ui->aPreviewSplitter);
    connectSplitter(ui->bPreviewSplitter);
}

void MainWindow::refreshPreviewPixmaps() {
    if (!m_outputWindow)
        return;
    auto *out = m_outputWindow->videoWidget();
    if (!out)
        return;

    const QImage frameA = out->getFrameA();
    if (!frameA.isNull()) {
        ui->aPreviewLabel->setPixmap(QPixmap::fromImage(
            frameA.scaled(ui->aPreviewLabel->size(), Qt::KeepAspectRatio,
                          Qt::SmoothTransformation)));
    }

    const QImage frameB = out->getFrameB();
    if (!frameB.isNull()) {
        ui->bPreviewLabel->setPixmap(QPixmap::fromImage(
            frameB.scaled(ui->bPreviewLabel->size(), Qt::KeepAspectRatio,
                          Qt::SmoothTransformation)));
    }
}

// ── Signal wiring ─────────────────────────────────────────────────────────────

void MainWindow::setupConnections() {
    // Menubar Media actions
    connect(ui->actionLoadFolder, &QAction::triggered, this, &MainWindow::onLoadFolderClicked);
    connect(ui->actionAddFolder,  &QAction::triggered, this, &MainWindow::onAddFolderClicked);
    connect(ui->actionClearAll,   &QAction::triggered, this, &MainWindow::onClearAllClicked);
    connect(ui->actionSaveSession,&QAction::triggered, this, &MainWindow::onSaveSessionClicked);
    connect(ui->actionLoadSession,&QAction::triggered, this, &MainWindow::onLoadSessionClicked);
    connect(ui->actionExportProject, &QAction::triggered, this, &MainWindow::onExportProjectClicked);
    connect(ui->actionImportProject, &QAction::triggered, this, &MainWindow::onImportProjectClicked);

    connect(ui->actionFreezeFrameCapture, &QAction::triggered, this, &MainWindow::onFreezeFrameCapture);

    // View menu
    connect(ui->actionShowOutput, &QAction::triggered, this, [this]() {
        m_outputWindow->show();
        m_outputWindow->raise();
        m_outputWindow->activateWindow();
    });
    connect(ui->actionShowPreview, &QAction::triggered, this, [this]() {
        for (const auto &mirror : m_outputHub->mirrorOutputs()) {
            if (mirror) {
                mirror->show();
                mirror->raise();
                mirror->activateWindow();
                return;
            }
        }
        auto *preview = m_outputHub->addMirrorOutput(tr("CutWire Prism - Preview Output"));
        if (preview) {
            preview->raise();
            preview->activateWindow();
        }
    });
    connect(ui->actionNdiOutput, &QAction::toggled, this, [this](bool on) {
        if (!m_outputHub->setNdiOutputEnabled(on)) {
            ui->actionNdiOutput->blockSignals(true);
            ui->actionNdiOutput->setChecked(false);
            ui->actionNdiOutput->blockSignals(false);
            if (on) {
                QMessageBox::warning(this, tr("NDI Output"),
                    m_outputHub->ndiAvailable()
                        ? tr("Could not start NDI program output.")
                        : tr("NDI is not available. Install the NDI SDK and rebuild CutWire Prism."));
            }
        }
    });
    connect(m_outputHub, &OutputHub::ndiOutputEnabledChanged, this, [this](bool on) {
        ui->actionNdiOutput->blockSignals(true);
        ui->actionNdiOutput->setChecked(on);
        ui->actionNdiOutput->blockSignals(false);
    });
    connect(ui->actionVirtualCameraOutput, &QAction::toggled, this, [this](bool on) {
        if (!m_outputHub->setVirtualCameraEnabled(on)) {
            ui->actionVirtualCameraOutput->blockSignals(true);
            ui->actionVirtualCameraOutput->setChecked(false);
            ui->actionVirtualCameraOutput->blockSignals(false);
            if (on) {
                const QString dev = m_outputHub->virtualCameraDevicePath();
#ifdef Q_OS_LINUX
                QMessageBox::warning(this, tr("Virtual Camera Output"),
                    tr("Could not start virtual camera output on %1.\n\n"
                       "Ensure v4l2loopback is loaded:\n"
                       "  sudo modprobe v4l2loopback\n\n"
                       "You may need to specify a device path in settings if the "
                       "loopback device is not at the default location.")
                        .arg(dev.isEmpty() ? tr("(unknown device)") : dev));
#else
                QMessageBox::warning(this, tr("Virtual Camera Output"),
                    tr("Could not start virtual camera output (%1).\n\n"
                       "Another application may already be using the virtual camera, "
                       "or softcam.dll may be missing next to Prism.exe.")
                        .arg(dev.isEmpty() ? QStringLiteral("DirectShow Softcam") : dev));
#endif
            }
        }
    });
    connect(m_outputHub, &OutputHub::virtualCameraEnabledChanged, this, [this](bool on) {
        ui->actionVirtualCameraOutput->blockSignals(true);
        ui->actionVirtualCameraOutput->setChecked(on);
        ui->actionVirtualCameraOutput->blockSignals(false);
    });
    connect(ui->actionRecordingPanel, &QAction::triggered, this, &MainWindow::onRecordingPanel);
    connect(m_outputHub, &OutputHub::recordingStateChanged, this, [this]() {
        ui->actionDropMarker->setEnabled(m_outputHub->isRecording());
        updateRecordingUi();
        if (m_recordingPanel)
            m_recordingPanel->syncFromHub();
    });
    connect(m_outputHub, &OutputHub::recordingProgress, this, [this](qint64 ms) {
        updateRecordingUi(ms);
    });
    connect(m_outputHub, &OutputHub::recordingError, this, [this](const QString &msg) {
        if (!msg.isEmpty())
            QMessageBox::warning(this, tr("Recording Error"), msg);
    });
    connect(ui->actionDropMarker, &QAction::triggered, this, [this]() {
        if (!m_outputHub->isRecording()) return;
        bool ok = false;
        const QString label = QInputDialog::getText(
            this, tr("Drop Marker"), tr("Marker label:"),
            QLineEdit::Normal, tr("Marker"), &ok);
        if (ok && !label.isEmpty())
            m_outputHub->addRecordingMarker(label);
    });
    auto *markerShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_M), this);
    connect(markerShortcut, &QShortcut::activated, ui->actionDropMarker, &QAction::trigger);
    connect(ui->actionConnectObs, &QAction::triggered, this, &MainWindow::onConnectObs);
    connect(ui->actionEditHotkeys, &QAction::triggered, this, &MainWindow::onEditHotkeys);
    connect(ui->actionLinkClipObsScene, &QAction::triggered, this, &MainWindow::onLinkClipObsScene);
    connect(m_obsIntegration, &ObsIntegration::connectedChanged, this, [this](bool on) {
        m_obsScenesMenu->setEnabled(on);
        if (!on) rebuildObsScenesMenu({});
    });
    connect(m_obsIntegration, &ObsIntegration::sceneListUpdated,
            this, &MainWindow::rebuildObsScenesMenu);
    connect(ui->actionStayOnTop, &QAction::toggled, this, [this](bool on) {
        m_outputWindow->setStayOnTop(on);
    });
    ui->actionStayOnTop->setChecked(true);

    connect(ui->actionStartRemoteControl, &QAction::triggered, this, &MainWindow::onStartRemoteControl);

    // ClipNodeEditor signals
    connect(m_clipNodeEditor, &ClipNodeEditor::clipChainChanged,
            this, &MainWindow::pushDecks);
    connect(m_clipNodeEditor, &ClipNodeEditor::addInputNodeRequested,
            this, [this]() {
        QMenu menu(this);
        setupAddElementMenu(&menu);
        menu.exec(QCursor::pos());
    });
    connect(m_clipNodeEditor, &ClipNodeEditor::nodeAdded,
            m_hotkeyManager, &HotkeyManager::assignHotkeyToNode);
    connect(m_clipNodeEditor, &ClipNodeEditor::nodeRemoved,
            this, &MainWindow::onNodeRemoveRequested);
    connect(m_clipNodeEditor, &ClipNodeEditor::audioGraphChanged, this, [this]() {
        if (m_deckController->activeNodeA())
            m_deckController->applyAudioControllerToDeck(true,  m_deckController->activeNodeA());
        if (m_deckController->activeNodeB())
            m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB());
        m_deckController->refreshShaderAudioForActiveDecks();
        m_deckController->refreshTextDataForActiveDecks();
        m_deckController->syncMasterAudioInputs();
        rebuildActiveDeckChains();
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
    connect(ui->aDeckSpeedSlider, &QSlider::valueChanged,
            this, &MainWindow::onADeckSpeedChanged);
    connect(ui->bDeckSpeedSlider, &QSlider::valueChanged,
            this, &MainWindow::onBDeckSpeedChanged);
    connect(ui->crossfaderSlider, &QSlider::valueChanged,
            this, &MainWindow::onCrossfaderMoved);
    connect(ui->durationSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->durationLabel->setText(
            tr("Time: %1 s").arg(value / 100.0, 0, 'f', 2));
    });

    connect(ui->panicBlackoutBtn,   &QPushButton::toggled, this, &MainWindow::onPanicBlackoutClicked);
    connect(ui->panicPauseBtn,      &QPushButton::toggled, this, &MainWindow::onPanicPauseClicked);
    connect(ui->panicStayTunedBtn,  &QPushButton::toggled, this, &MainWindow::onPanicStayTunedClicked);

    ui->panicBlackoutBtn->setToolTip(tr("Cut program output to black (%1)").arg(
        QKeySequence(Qt::Key_B).toString(QKeySequence::NativeText)));
    ui->panicPauseBtn->setToolTip(tr("Freeze the program output on the current frame (%1)").arg(
        QKeySequence(Qt::Key_P).toString(QKeySequence::NativeText)));

    auto *blackoutShortcut = new QShortcut(QKeySequence(Qt::Key_B), this);
    blackoutShortcut->setContext(Qt::ApplicationShortcut);
    connect(blackoutShortcut, &QShortcut::activated, ui->panicBlackoutBtn, &QPushButton::toggle);

    auto *pauseShortcut = new QShortcut(QKeySequence(Qt::Key_P), this);
    pauseShortcut->setContext(Qt::ApplicationShortcut);
    connect(pauseShortcut, &QShortcut::activated, ui->panicPauseBtn, &QPushButton::toggle);

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
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Files"), "",
        tr("Media Files (*.mp4 *.avi *.mov *.mkv *.webm *.png *.jpg *.jpeg *.bmp *.webp *.gif)"));
    if (files.isEmpty()) return;
    m_assetLibrary->addFiles(files);
}

void MainWindow::onAddFolderClicked() {
    QString path = QFileDialog::getExistingDirectory(this, tr("Add Media Folder"));
    if (path.isEmpty()) return;
    m_assetLibrary->addFolder(path);
}

void MainWindow::onAddFilesClicked() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Media Files"), "",
        tr("Media Files (*.mp4 *.avi *.mov *.mkv *.webm *.png *.jpg *.jpeg *.bmp *.webp *.gif)"));
    if (files.isEmpty()) return;
    if (!m_assetLibrary->addFiles(files))
        return;
    for (const QString &path : files) {
        if (ClipManager::isMediaPath(path))
            m_clipNodeEditor->addClipNode(path, ThumbnailExtractor::extract(path, 110, 65));
    }
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::onAddVideoUrlClicked() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Video from URL"));
    dlg.setMinimumWidth(450);

    QFormLayout *layout = new QFormLayout(&dlg);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    QLineEdit *urlEdit = new QLineEdit(&dlg);
    urlEdit->setPlaceholderText("e.g. http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4");
    urlEdit->setClearButtonEnabled(true);
    layout->addRow(tr("Video URL:"), urlEdit);

    QLineEdit *nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(tr("Optional custom name"));
    nameEdit->setClearButtonEnabled(true);
    layout->addRow(tr("Display Name:"), nameEdit);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QString url = urlEdit->text().trimmed();
    if (url.isEmpty()) {
        return;
    }

    // Determine default name
    QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QUrl parsedUrl(url);
        name = parsedUrl.fileName();
        if (name.isEmpty()) {
            name = tr("Network Video");
        }
    }

    // Validate the URL using VideoPlayer before adding the node
    QApplication::setOverrideCursor(Qt::WaitCursor);
    VideoPlayer testPlayer;
    bool canOpen = testPlayer.open(url);
    QApplication::restoreOverrideCursor();

    if (!canOpen) {
        QMessageBox::critical(this, tr("Invalid Video URL"),
            tr("Could not open or play the video from URL:\n%1\n\n"
               "Please check the URL and ensure the video stream is accessible and valid.").arg(url));
        return;
    }

    // Create the Clip Node
    QPixmap thumb = ThumbHelper::makeIconThumb(MaterialSymbols::Names::Link);
    ClipNodeModel *node = m_clipNodeEditor->addClipNode(url, thumb);
    if (node) {
        node->setDisplayName(name);
    }
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::onClearAllClicked() {
    m_clipNodeEditor->clearAllNodes();
    m_deckController->setActiveNodeA(0);
    m_deckController->setActiveNodeB(0);
    m_deckController->stopDeckAudio(true);
    m_deckController->stopDeckAudio(false);
    m_outputWindow->videoWidget()->clearDeckA();
    m_outputWindow->videoWidget()->clearDeckB();
    m_deckBaseA.clear(); m_deckOverlaysA.clear();
    m_deckBaseB.clear(); m_deckOverlaysB.clear();
    m_stackWidget->setCurrentWidget(m_emptyPlaceholder);
}

// ── Element management ────────────────────────────────────────────────────────

void MainWindow::addElementNode(const SourceDescriptor &desc, const QPixmap &thumb) {
    m_clipNodeEditor->addSourceNode(desc, thumb);
    if ((desc.kind == SourceDescriptor::Kind::VideoFile
         || desc.kind == SourceDescriptor::Kind::Image)
        && !desc.path.isEmpty() && QFileInfo(desc.path).isFile()) {
        m_assetLibrary->addFiles({desc.path});
    }
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);
}

void MainWindow::addSourceOfKind(SourceDescriptor::Kind kind) {
    SourceDescriptor desc;
    QPixmap thumb;
    if (SourcePrompt::prompt(kind, this, desc, thumb))
        addElementNode(desc, thumb);
}

void MainWindow::setupAddElementMenu(QMenu *menu) {
    if (!menu) return;
    SourcePrompt::buildMenu(menu,
                            [this]() { onAddFilesClicked(); },
                            [this]() { onAddVideoUrlClicked(); },
                            [this](SourceDescriptor::Kind kind) { addSourceOfKind(kind); },
                            NdiSource::isAvailable(),
#ifdef PRISM_HAVE_WEBRTC
                            WebRtcSource::isAvailable());
#else
                            false);
#endif
}

// ── Node deck assignment ──────────────────────────────────────────────────────

void MainWindow::onNodeAButtonClicked(NodeId nodeId) {
    if (nodeId) m_clipNodeEditor->assignInputToDeck(nodeId, true);
    else pushDecks();
}
void MainWindow::onNodeBButtonClicked(NodeId nodeId) {
    if (nodeId) m_clipNodeEditor->assignInputToDeck(nodeId, false);
    else pushDecks();
}
void MainWindow::rebuildActiveDeckChains() { pushDecks(); }

void MainWindow::pushDecks() {
    auto *out = m_outputWindow->videoWidget();
    const bool single = m_clipNodeEditor->outputIsSingleStream();

    out->setSingleStreamMode(single);
    ui->crossfaderSlider->setEnabled(!single);
    ui->transitionCombo->setEnabled(!single);

    // A stream's source identity: base source key + ordered overlay keys. The key
    // captures file/kind/trim but NOT placement, so equal keys mean the decoded
    // MediaSources can be reused (only transforms/order updated), never reopened.
    auto layerKey = [&](const ResolvedLayer &l) -> QString {
        ClipNodeModel *n = m_clipNodeEditor->nodeAt(l.inputNodeId);
        if (!n) return QStringLiteral("%1:?").arg(l.inputNodeId);
        const SourceDescriptor &d = n->sourceDescriptor();
        return QStringLiteral("%1:%2:%3:%4:%5")
            .arg(l.inputNodeId).arg((int)d.kind).arg(d.path)
            .arg(n->startTime()).arg(n->endTime());
    };
    auto keysOf = [&](const ResolvedStream &s, QString &base, QStringList &ov) {
        base.clear(); ov.clear();
        if (s.layers.isEmpty()) return;
        base = layerKey(s.layers.first());
        for (int i = 1; i < s.layers.size(); ++i) ov << layerKey(s.layers[i]);
    };
    auto sameStream = [](const QString &b1, const QStringList &o1,
                         const QString &b2, const QStringList &o2) {
        return b1 == b2 && o1 == o2;
    };
    auto isPermutation = [](QStringList a, QStringList b) {
        a.sort(); b.sort(); return a == b;
    };

    const NodeId prodA = single ? m_clipNodeEditor->outputSingleProducer()
                                : m_clipNodeEditor->deckAInput();
    const NodeId prodB = single ? 0 : m_clipNodeEditor->deckBInput();
    ResolvedStream streamA = prodA ? m_clipNodeEditor->evaluateVideoInput(prodA) : ResolvedStream{};
    ResolvedStream streamB = prodB ? m_clipNodeEditor->evaluateVideoInput(prodB) : ResolvedStream{};

    QString dBaseA, dBaseB;
    QStringList dOvA, dOvB;
    keysOf(streamA, dBaseA, dOvA);
    keysOf(streamB, dBaseB, dOvB);

    // Cross-deck reuse: if a deck wants exactly what the other deck currently holds,
    // exchange decoded content (video + audio) rather than re-decoding from disk.
    const bool aWantsCurB = !dBaseA.isEmpty() && sameStream(dBaseA, dOvA, m_deckBaseB, m_deckOverlaysB);
    const bool bWantsCurA = !dBaseB.isEmpty() && sameStream(dBaseB, dOvB, m_deckBaseA, m_deckOverlaysA);
    const bool aUnchanged = sameStream(dBaseA, dOvA, m_deckBaseA, m_deckOverlaysA);
    const bool bUnchanged = sameStream(dBaseB, dOvB, m_deckBaseB, m_deckOverlaysB);
    if (!(aUnchanged && bUnchanged) && (aWantsCurB || bWantsCurA)) {
        out->swapDeckContents();
        m_deckController->swapDeckAudio();
        std::swap(m_deckBaseA, m_deckBaseB);
        std::swap(m_deckOverlaysA, m_deckOverlaysB);
    }

    auto pushDeck = [&](bool deckA, const ResolvedStream &stream,
                        const QString &dBase, const QStringList &dOv,
                        QSlider *slider, QPushButton *playBtn,
                        QLabel *selLabel, QLabel *timeLabel) {
        QString &curBase = deckA ? m_deckBaseA : m_deckBaseB;
        QStringList &curOv = deckA ? m_deckOverlaysA : m_deckOverlaysB;

        QSlider *speedSlider = deckA ? ui->aDeckSpeedSlider : ui->bDeckSpeedSlider;
        QLabel  *speedLabel  = deckA ? ui->speedLabelALabel : ui->speedLabelBLabel;

        if (stream.layers.isEmpty()) {
            if (deckA) { out->clearDeckA(); m_deckController->setActiveNodeA(0); }
            else       { out->clearDeckB(); m_deckController->setActiveNodeB(0); }
            m_deckController->stopDeckAudio(deckA);
            slider->setVisible(false);
            playBtn->setVisible(false);
            speedSlider->setVisible(false);
            speedLabel->setVisible(false);
            curBase.clear(); curOv.clear();
            return;
        }
        const ResolvedLayer base = stream.layers.first();
        ClipNodeModel *node = m_clipNodeEditor->nodeAt(base.inputNodeId);
        if (!node) return;
        const bool speedControl = node->sourceDescriptor().hasSpeedControl();
        speedSlider->setVisible(speedControl);
        speedLabel->setVisible(speedControl);

        auto applyBase = [&]() {
            if (deckA) {
                out->setBaseA(base.baseX, base.baseY, base.baseW, base.baseH);
                out->setCropA(base.cropX, base.cropY, base.cropW, base.cropH);
                out->setFlipA(base.flipH, base.flipV);
                out->setCanvasSizeA(stream.canvasWidth, stream.canvasHeight);
            } else {
                out->setBaseB(base.baseX, base.baseY, base.baseW, base.baseH);
                out->setCropB(base.cropX, base.cropY, base.cropW, base.cropH);
                out->setFlipB(base.flipH, base.flipV);
                out->setCanvasSizeB(stream.canvasWidth, stream.canvasHeight);
            }
        };
        auto applyOverlayPlacements = [&]() {
            for (int i = 1; i < stream.layers.size(); ++i) {
                const ResolvedLayer &l = stream.layers[i];
                out->setChainPlacement(deckA, i - 1,
                                       l.cropX, l.cropY, l.cropW, l.cropH,
                                       l.flipH, l.flipV,
                                       l.baseX, l.baseY, l.baseW, l.baseH,
                                       l.visible);
            }
        };
        auto refreshUI = [&]() {
            m_deckController->updateDeckUI(deckA, node->sourceName(),
                node->sourceDescriptor(),
                slider, playBtn, selLabel, timeLabel);
        };

        if (sameStream(dBase, dOv, curBase, curOv)) {
            // Identical sources (possibly after a swap): update placement only.
            applyBase();
            applyOverlayPlacements();
            refreshUI();
        } else if (dBase == curBase && !curBase.isEmpty() && isPermutation(dOv, curOv)) {
            // Overlay reorder with an unchanged primary: permute in place, no re-decode.
            std::vector<int> perm(dOv.size(), 0);
            QVector<bool> used(curOv.size(), false);
            for (int i = 0; i < dOv.size(); ++i)
                for (int j = 0; j < curOv.size(); ++j)
                    if (!used[j] && curOv[j] == dOv[i]) { perm[i] = j; used[j] = true; break; }
            out->reorderChain(deckA, perm);
            applyBase();
            applyOverlayPlacements();
            refreshUI();
            curOv = dOv;
        } else {
            // Source set changed → full (re)load of primary + overlay chain.
            m_deckController->assignNodeToDeck(node, base.inputNodeId, deckA,
                                               slider, playBtn, selLabel, timeLabel);
            applyBase();
            ResolvedStream overlay;
            overlay.canvasWidth = stream.canvasWidth;
            overlay.canvasHeight = stream.canvasHeight;
            for (int i = 1; i < stream.layers.size(); ++i)
                overlay.layers.push_back(stream.layers[i]);
            auto chain = SourceFactory::buildStream(overlay, m_clipNodeEditor);
            if (deckA) out->setNodeChainA(std::move(chain));
            else       out->setNodeChainB(std::move(chain));
            m_obsIntegration->onClipTriggered(node->sourceDescriptor());
            curBase = dBase; curOv = dOv;
        }
    };

    pushDeck(true, streamA, dBaseA, dOvA,
             ui->aProgressSlider, ui->aDeckPlayBtn, ui->aSelectedLabel, ui->aTimeLabel);
    pushDeck(false, streamB, dBaseB, dOvB,
             ui->bProgressSlider, ui->bDeckPlayBtn, ui->bSelectedLabel, ui->bTimeLabel);

    m_outputHub->setActiveDeckNodes(m_deckController->activeNodeA(), m_deckController->activeNodeB());
}

void MainWindow::onNodeRemoveRequested(NodeId nodeId) {
    m_hotkeyManager->releaseHotkeyForNode(nodeId);
    m_clipNodeEditor->removeNode(nodeId);
    auto *out = m_outputWindow->videoWidget();
    if (m_deckController->activeNodeA() == nodeId) { m_deckController->setActiveNodeA(0); out->clearDeckA(); m_deckBaseA.clear(); m_deckOverlaysA.clear(); }
    if (m_deckController->activeNodeB() == nodeId) { m_deckController->setActiveNodeB(0); out->clearDeckB(); m_deckBaseB.clear(); m_deckOverlaysB.clear(); }
    if (!m_deckController->activeNodeA()) m_deckController->stopDeckAudio(true);
    if (!m_deckController->activeNodeB()) m_deckController->stopDeckAudio(false);
    if (m_clipNodeEditor->allNodes().isEmpty())
        m_stackWidget->setCurrentWidget(m_emptyPlaceholder);
}

// ── Crossfader / deck play ────────────────────────────────────────────────────

void MainWindow::onCrossfaderMoved(int value) {
    m_outputWindow->videoWidget()->setCrossfade(value / 100.f);
    // Crossfading only adjusts deck volumes — do NOT re-seek the audio, or each
    // slider step would flush the decoder and produce a stutter/zipper artifact.
    if (m_deckController->activeNodeA())
        m_deckController->applyAudioControllerToDeck(true,  m_deckController->activeNodeA(), false);
    if (m_deckController->activeNodeB())
        m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB(), false);
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
    const double speed = value * 0.05;
    ui->speedLabelALabel->setText(tr("Speed: %1x").arg(speed, 0, 'f', 2));
    m_deckController->setDeckSpeed(true, speed);
}

void MainWindow::onBDeckSpeedChanged(int value) {
    const double speed = value * 0.05;
    ui->speedLabelBLabel->setText(tr("Speed: %1x").arg(speed, 0, 'f', 2));
    m_deckController->setDeckSpeed(false, speed);
}

void MainWindow::syncPanicButtons(QPushButton *activeBtn) {
    for (QPushButton *btn : {ui->panicBlackoutBtn, ui->panicPauseBtn, ui->panicStayTunedBtn}) {
        if (btn == activeBtn) continue;
        btn->blockSignals(true);
        btn->setChecked(false);
        btn->blockSignals(false);
    }
}

void MainWindow::clearPanicState() {
    auto *out = m_outputWindow->videoWidget();
    out->setPanicOverlay(VideoWidget::PanicOverlay::None);
    out->setOutputFrozen(false);
    if (m_deckController->activeNodeA())
        m_deckController->applyAudioControllerToDeck(true,  m_deckController->activeNodeA());
    if (m_deckController->activeNodeB())
        m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB());
}

void MainWindow::applyPanicFromButtons() {
    auto *out = m_outputWindow->videoWidget();

    switch (MainWindowUtils::panicStateForButtons(
        ui->panicBlackoutBtn->isChecked(),
        ui->panicStayTunedBtn->isChecked(),
        ui->panicPauseBtn->isChecked())) {
    case MainWindowUtils::PanicState::Blackout:
        out->setOutputFrozen(false);
        out->setPanicOverlay(VideoWidget::PanicOverlay::Blackout);
        m_deckController->stopDeckAudio(true);
        m_deckController->stopDeckAudio(false);
        return;
    case MainWindowUtils::PanicState::StayTuned:
        out->setOutputFrozen(false);
        out->setPanicOverlay(VideoWidget::PanicOverlay::StayTuned);
        m_deckController->stopDeckAudio(true);
        m_deckController->stopDeckAudio(false);
        return;
    case MainWindowUtils::PanicState::Freeze:
        out->setPanicOverlay(VideoWidget::PanicOverlay::None);
        out->setOutputFrozen(true);
        m_deckController->stopDeckAudio(true);
        m_deckController->stopDeckAudio(false);
        return;
    case MainWindowUtils::PanicState::None:
        break;
    }

    clearPanicState();
}

void MainWindow::onPanicBlackoutClicked(bool checked) {
    if (checked) syncPanicButtons(ui->panicBlackoutBtn);
    applyPanicFromButtons();
}

void MainWindow::onPanicPauseClicked(bool checked) {
    if (checked) syncPanicButtons(ui->panicPauseBtn);
    applyPanicFromButtons();
}

void MainWindow::onPanicStayTunedClicked(bool checked) {
    if (checked) syncPanicButtons(ui->panicStayTunedBtn);
    applyPanicFromButtons();
}

// ── Timer update (preview labels + progress sliders) ─────────────────────────

void MainWindow::onTimerUpdate() {
    if (m_shuttingDown || !m_outputWindow || !ui)
        return;

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
        if (MainWindowUtils::isBackwardJump(timeA, m_deckController->lastTimeA())) {
            NodeId id = m_deckController->activeNodeA();
            if (id)
                if (auto *node = m_clipNodeEditor->nodeAt(id))
                    m_deckController->updateDeckAudio(true, id, node, timeA, true);
        }
        m_deckController->setLastTimeA(timeA);
    }
    MaterialSymbols::setPlayPause(ui->aDeckPlayBtn, out->isPlayingA(), 22);

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

        if (MainWindowUtils::isBackwardJump(timeB, m_deckController->lastTimeB())) {
            NodeId id = m_deckController->activeNodeB();
            if (id)
                if (auto *node = m_clipNodeEditor->nodeAt(id))
                    m_deckController->updateDeckAudio(false, id, node, timeB, true);
        }
        m_deckController->setLastTimeB(timeB);
    }
    MaterialSymbols::setPlayPause(ui->bDeckPlayBtn, out->isPlayingB(), 22);

    refreshPreviewPixmaps();

    m_outputHub->setActiveDeckNodes(m_deckController->activeNodeA(), m_deckController->activeNodeB());
    if (m_outputHub->isRecording())
        updateRecordingUi(m_outputHub->longestActiveRecordingMs());
}

// ── Drag & drop ───────────────────────────────────────────────────────────────

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasUrls()) return;

    QStringList filePaths;
    for (const QUrl &url : mimeData->urls()) {
        const QString p = url.toLocalFile();
        QFileInfo fi(p);
        if (fi.isDir())
            m_assetLibrary->addFolder(p);
        else
            filePaths << p;
    }
    if (!filePaths.isEmpty())
        m_assetLibrary->addFiles(filePaths);

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
        QMenuBar {
            background-color: #1c1d1f;
            color: #E0E0E0;
            border-bottom: 1px solid #151618;
            padding: 2px 0;
        }
        QMenuBar::item {
            background: transparent;
            padding: 6px 12px;
            border-radius: 4px;
            margin: 2px 2px;
        }
        QMenuBar::item:selected {
            background-color: #2a2c30;
            color: #FFFFFF;
        }
        QMenuBar::item:pressed {
            background-color: #2a5c66;
            color: #FFFFFF;
        }
        QMenu {
            background-color: #1e1f22;
            color: #E0E0E0;
            border: 1px solid #33363b;
            border-radius: 8px;
            padding: 4px;
        }
        QMenu::item {
            padding: 7px 32px 7px 16px;
            border-radius: 4px;
            margin: 1px 2px;
        }
        QMenu::item:selected {
            background-color: #2a5c66;
            color: #FFFFFF;
        }
        QMenu::item:disabled {
            color: #666666;
        }
        QMenu::separator {
            height: 1px;
            background: #33363b;
            margin: 4px 8px;
        }
        QComboBox {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #2a2c30,stop:1 #1e1f22);
            color: #E0E0E0;
            border-top: 1px solid #33363b; border-left: 1px solid #33363b;
            border-bottom: 1px solid #151618; border-right: 1px solid #151618;
            border-radius: 6px;
            padding: 4px 8px;
            min-height: 22px;
        }
        QComboBox:hover {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #2e3136,stop:1 #222326);
            border-top: 1px solid #3a3d43; border-left: 1px solid #3a3d43;
        }
        QComboBox:focus, QComboBox:on {
            border: 1px solid #2a8fa0;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 22px;
            border-left: 1px solid #151618;
            border-top-right-radius: 6px;
            border-bottom-right-radius: 6px;
            background-color: #1e1f22;
        }
        QComboBox::drop-down:hover {
            background-color: #2a2c30;
        }
        QComboBox::down-arrow {
            width: 0; height: 0;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #888888;
        }
        QComboBox::down-arrow:hover {
            border-top-color: #E0E0E0;
        }
        QComboBox QAbstractItemView {
            background-color: #1e1f22;
            color: #E0E0E0;
            border: 1px solid #33363b;
            border-radius: 6px;
            padding: 4px;
            selection-background-color: #2a5c66;
            selection-color: #FFFFFF;
            outline: none;
        }
        QComboBox QAbstractItemView::item {
            padding: 6px 12px;
            min-height: 24px;
            border-radius: 4px;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #2a2c30;
            color: #FFFFFF;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #2a5c66;
            color: #FFFFFF;
        }
        QCheckBox {
            spacing: 6px;
            color: #E0E0E0;
        }
        QCheckBox::indicator {
            width: 16px; height: 16px;
            border: 1px solid #33363b;
            border-radius: 3px;
            background-color: #1e1f22;
        }
        QCheckBox::indicator:hover {
            border: 1px solid #2a8fa0;
            background-color: #2a2c30;
        }
        QCheckBox::indicator:checked {
            background-color: #2a5c66;
            border: 1px solid #2a8fa0;
        }
        QLineEdit, QTextEdit {
            background-color: #1e1f22;
            color: #E0E0E0;
            border: 1px solid #33363b;
            border-radius: 6px;
            padding: 4px 8px;
            selection-background-color: #2a5c66;
        }
        QLineEdit:hover, QTextEdit:hover {
            border: 1px solid #3a3d43;
            background-color: #222326;
        }
        QLineEdit:focus, QTextEdit:focus {
            border: 1px solid #2a8fa0;
            background-color: #242528;
        }
        QPushButton#accentButton:hover, QPushButton[text*="Load"]:hover,
        QPushButton[text*="Play"]:hover, QPushButton[text*="Fullscreen"]:hover {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #4a7f8c,stop:1 #2a5c66);
            color: #FFFFFF;
        }
        QPushButton#panicBlackoutBtn, QPushButton#panicPauseBtn, QPushButton#panicStayTunedBtn {
            font-size: 10px;
            font-weight: bold;
            padding: 6px 4px;
            min-height: 26px;
            border-radius: 4px;
            color: #ffc8bc;
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #3a2422,stop:1 #2a1816);
            border-top: 1px solid #6a3830;
            border-left: 1px solid #6a3830;
            border-bottom: 1px solid #1a0e0c;
            border-right: 1px solid #1a0e0c;
        }
        QPushButton#panicBlackoutBtn:hover, QPushButton#panicPauseBtn:hover, QPushButton#panicStayTunedBtn:hover {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #4a2c28,stop:1 #3a201c);
            color: #ffe8e0;
        }
        QPushButton#panicBlackoutBtn:checked, QPushButton#panicPauseBtn:checked, QPushButton#panicStayTunedBtn:checked {
            background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8b2820,stop:1 #5a1814);
            color: #FFFFFF;
            border-top: 1px solid #c04030;
            border-left: 1px solid #c04030;
            border-bottom: 1px solid #3a100c;
            border-right: 1px solid #3a100c;
        }
        QSplitter::handle {
            background-color: #2a2c30;
        }
        QSplitter::handle:hover {
            background-color: #2a8fa0;
        }
        QSplitter::handle:horizontal {
            width: 5px;
        }
        QSplitter::handle:vertical {
            height: 5px;
        }
    )");
}

// ── Session save / load ───────────────────────────────────────────────────────

void MainWindow::onSaveSessionClicked() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Session", QString(), "CutWire Prism Session (*.sxs);;All Files (*)");
    if (path.isEmpty()) return;
    path = MainWindowUtils::ensureExtension(path, QStringLiteral(".sxs"));

    if (!m_sessionManager->writeSessionFile(currentSessionJson(path), path)) {
        QMessageBox::warning(this, "Save Session",
                             QString("Cannot write to file:\n%1").arg(path));
    }
}

void MainWindow::onLoadSessionClicked() {
    QString path = QFileDialog::getOpenFileName(
        this, "Load Session", QString(), "CutWire Prism Session (*.sxs);;All Files (*)");
    if (path.isEmpty()) return;
    loadFromFile(path, true);
}

void MainWindow::onExportProjectClicked() {
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Project"), QString(),
        tr("CutWire Prism Project (*.switch);;All Files (*)"));
    if (path.isEmpty()) return;
    path = MainWindowUtils::ensureExtension(path, ProjectPackager::kExtension);

    const ProjectPackager::Report report = ProjectPackager::exportPackage(
        currentSessionJson(), m_clipNodeEditor->allNodes(), path);

    if (!report.success) {
        QMessageBox::warning(this, tr("Export Project"),
                             report.error.isEmpty() ? tr("Export failed.") : report.error);
        return;
    }

    QString message = tr("Exported %1 asset(s) to:\n%2").arg(report.assetCount).arg(path);
    if (!report.warnings.isEmpty())
        message += tr("\n\nNotes:\n%1").arg(report.warnings.join('\n'));
    QMessageBox::information(this, tr("Export Project"), message);
}

void MainWindow::onImportProjectClicked() {
    const QString packagePath = QFileDialog::getOpenFileName(
        this, tr("Import Project"), QString(),
        tr("CutWire Prism Project (*.switch);;All Files (*)"));
    if (packagePath.isEmpty()) return;

    const QFileInfo packageInfo(packagePath);
    const QString defaultExtractDir =
        packageInfo.absolutePath() + QDir::separator() + packageInfo.completeBaseName();
    const QString extractDir = QFileDialog::getExistingDirectory(
        this, tr("Extract Project To"), defaultExtractDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (extractDir.isEmpty()) return;

    const ProjectPackager::ImportResult result =
        ProjectPackager::importPackage(packagePath, extractDir, true);
    if (!result.success) {
        QMessageBox::warning(this, tr("Import Project"),
                             result.error.isEmpty() ? tr("Import failed.") : result.error);
        return;
    }

    loadFromFile(result.sessionPath, true);

    if (!result.warnings.isEmpty()) {
        QMessageBox::information(this, tr("Import Project"),
                                 tr("Project imported with notes:\n%1")
                                     .arg(result.warnings.join('\n')));
    }
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
    m_assetLibrary->rebuild();
    m_stackWidget->setCurrentWidget(m_clipNodeEditor);

    // Restore hotkeys.
    m_hotkeyManager->restoreHotkeys(m_sessionManager->restoredHotkeys());

    // Restore crossfader + transition settings.
    ui->crossfaderSlider->setValue(m_sessionManager->restoredCrossfader());
    ui->transitionCombo->setCurrentIndex(m_sessionManager->restoredTransitionMode());
    m_transitionCtrl->setTransitionDuration(m_sessionManager->restoredTransitionDuration());

    // Re-assign active decks.
    const NodeId savedA = m_sessionManager->restoredActiveNodeA();
    const NodeId savedB = m_sessionManager->restoredActiveNodeB();
    if (savedA) onNodeAButtonClicked(savedA);
    if (savedB) onNodeBButtonClicked(savedB);
}

void MainWindow::onFreezeFrameCapture() {
    auto *out = m_outputWindow->videoWidget();
    const QList<FrameCaptureHelper::LayerRef> layers =
        FrameCaptureHelper::enumerateLayers(out, m_clipNodeEditor, m_deckController);

    if (layers.isEmpty()) {
        QMessageBox::information(this, tr("Freeze Frame Capture"),
                                 tr("No layers are available to capture."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Freeze Frame Capture"));
    dlg.setMinimumWidth(460);

    auto *layerCombo = new QComboBox(&dlg);
    for (const auto &layer : layers)
        layerCombo->addItem(layer.label);

    auto *saveCheck = new QCheckBox(tr("Save PNG to Pictures/Prism/Captures"), &dlg);
    saveCheck->setChecked(true);
    auto *addCheck = new QCheckBox(tr("Add captured frame as a new image element"), &dlg);
    addCheck->setChecked(false);
    auto *holdCheck = new QCheckBox(tr("Hold selected layer as a still (freeze in place)"), &dlg);
    holdCheck->setChecked(false);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(tr("Capture current frame from:"), &dlg));
    layout->addWidget(layerCombo);
    layout->addWidget(saveCheck);
    layout->addWidget(addCheck);
    layout->addWidget(holdCheck);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const int idx = layerCombo->currentIndex();
    if (idx < 0 || idx >= layers.size())
        return;

    const FrameCaptureHelper::LayerRef layer = layers.at(idx);
    const QImage frame = FrameCaptureHelper::captureLayer(out, layer);
    if (frame.isNull()) {
        QMessageBox::warning(this, tr("Freeze Frame Capture"),
                             tr("Could not capture a frame from \"%1\".\n\n"
                                "Make sure the layer is active and has video.")
                                 .arg(layer.label));
        return;
    }

    QString savedPath;
    if (saveCheck->isChecked() || addCheck->isChecked()) {
        savedPath = FrameCaptureHelper::savePng(frame, layer.label);
        if (savedPath.isEmpty()) {
            QMessageBox::warning(this, tr("Freeze Frame Capture"),
                                 tr("Captured the frame but could not save it to disk."));
            if (!holdCheck->isChecked())
                return;
        }
    }

    bool held = false;
    if (holdCheck->isChecked()) {
        if (layer.kind == FrameCaptureHelper::LayerKind::Program) {
            QMessageBox::information(this, tr("Freeze Frame Capture"),
                                     tr("Program output cannot be held as a still layer. "
                                        "Choose a deck or overlay layer, or use PAUSE to freeze the full output."));
        } else {
            const int chainIndex = (layer.kind == FrameCaptureHelper::LayerKind::DeckChain)
                                 ? layer.chainIndex : -1;
            out->holdLayerAsStill(layer.deckA, chainIndex, frame);
            held = true;
        }
    }

    if (addCheck->isChecked() && !savedPath.isEmpty()) {
        SourceDescriptor desc;
        desc.kind        = SourceDescriptor::Kind::Image;
        desc.path        = savedPath;
        desc.displayName = tr("Capture %1").arg(QFileInfo(savedPath).completeBaseName());
        addElementNode(desc, QPixmap::fromImage(
            frame.scaled(110, 65, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }

    QString summary = tr("Captured \"%1\".").arg(layer.label);
    if (!savedPath.isEmpty())
        summary += QLatin1Char('\n') + tr("Saved: %1").arg(savedPath);
    if (held)
        summary += QLatin1Char('\n') + tr("Layer is now held as a still.");
    if (addCheck->isChecked() && !savedPath.isEmpty())
        summary += QLatin1Char('\n') + tr("Added as a new image element.");

    QMessageBox::information(this, tr("Freeze Frame Capture"), summary);
}

void MainWindow::onConnectObs() {
    m_obsIntegration->showConnectDialog(this);
}

void MainWindow::onEditHotkeys() {
    HotkeyEditorDialog dlg(m_hotkeyManager, m_clipNodeEditor, this);
    dlg.exec();
}

void MainWindow::onLinkClipObsScene() {
    const QVector<ClipNodeModel *> nodes = m_clipNodeEditor->allNodes();
    QStringList labels;
    QList<NodeId> ids;
    for (ClipNodeModel *model : nodes) {
        if (!model || !model->hasSource()) continue;
        labels << model->sourceName();
        ids << model->nodeId();
    }
    if (labels.isEmpty()) {
        QMessageBox::information(this, tr("OBS Scene Link"),
                                 tr("Add a clip with a source first."));
        return;
    }

    bool ok = false;
    const QString picked = QInputDialog::getItem(this, tr("Select Clip"),
                                                 tr("Clip to link:"), labels, 0, false, &ok);
    if (!ok) return;

    const int idx = labels.indexOf(picked);
    if (idx < 0 || idx >= ids.size()) return;

    auto *node = m_clipNodeEditor->nodeAt(ids[idx]);
    if (!node) return;

    const auto result = m_obsIntegration->promptLinkClipObsScene(
        this, node->sourceName(), node->sourceDescriptor().obsSceneName);
    if (!result.has_value()) return;

    node->setObsSceneName(*result);
}

void MainWindow::rebuildObsScenesMenu(const QStringList &scenes) {
    m_obsScenesMenu->clear();
    if (scenes.isEmpty()) {
        m_obsScenesMenu->addAction(tr("(Not connected)"))->setEnabled(false);
        return;
    }

    auto *refresh = m_obsScenesMenu->addAction(tr("Refresh Scene List"));
    connect(refresh, &QAction::triggered, this, [this]() {
        m_obsIntegration->refreshScenes();
    });
    m_obsScenesMenu->addSeparator();

    for (const QString &scene : scenes) {
        auto *action = m_obsScenesMenu->addAction(scene);
        connect(action, &QAction::triggered, this, [this, scene]() {
            m_obsIntegration->switchProgramScene(scene);
        });
    }
}

void MainWindow::onStartRemoteControl() {
    if (!m_serverDialog) {
        m_serverDialog = new RemoteServerDialog(m_remoteServer, this);
        m_serverDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_serverDialog, &QObject::destroyed, this, [this]() {
            m_serverDialog = nullptr;
        });
    }
    m_serverDialog->show();
    m_serverDialog->raise();
    m_serverDialog->activateWindow();
}

void MainWindow::selectNodeA(NodeId nodeId) {
    onNodeAButtonClicked(nodeId);
}

void MainWindow::selectNodeB(NodeId nodeId) {
    onNodeBButtonClicked(nodeId);
}

void MainWindow::togglePlayA() {
    onADeckPlayClicked();
}

void MainWindow::togglePlayB() {
    onBDeckPlayClicked();
}

bool MainWindow::isPlayingA() const {
    return m_outputWindow && m_outputWindow->videoWidget() && m_outputWindow->videoWidget()->isPlayingA();
}

bool MainWindow::isPlayingB() const {
    return m_outputWindow && m_outputWindow->videoWidget() && m_outputWindow->videoWidget()->isPlayingB();
}

NodeId MainWindow::activeNodeA() const {
    return m_deckController ? m_deckController->activeNodeA() : 0;
}

NodeId MainWindow::activeNodeB() const {
    return m_deckController ? m_deckController->activeNodeB() : 0;
}

void MainWindow::setupRecordingStatusBar() {
    auto *bar = statusBar();
    bar->setSizeGripEnabled(false);

    m_recStatusLabel = new QLabel(tr("● REC"), this);
    QFont recFont = m_recStatusLabel->font();
    recFont.setBold(true);
    m_recStatusLabel->setFont(recFont);
    m_recStatusLabel->setStyleSheet(QStringLiteral("color: #e04545;"));
    m_recStatusLabel->hide();

    m_recTimeLabel = new QLabel(this);
    m_recTimeLabel->hide();

    m_recTracksLabel = new QLabel(this);
    m_recTracksLabel->hide();

    m_recPathLabel = new QLabel(this);
    m_recPathLabel->setStyleSheet(QStringLiteral("color: #888;"));
    m_recPathLabel->hide();

    bar->addPermanentWidget(m_recStatusLabel);
    bar->addPermanentWidget(m_recTimeLabel);
    bar->addPermanentWidget(m_recTracksLabel, 1);
    bar->addPermanentWidget(m_recPathLabel);
}

void MainWindow::updateRecordingUi(qint64 elapsedMs) {
    if (m_shuttingDown)
        return;

    const bool recording = m_outputHub && m_outputHub->isRecording();
    if (m_recStatusLabel) m_recStatusLabel->setVisible(recording);
    if (m_recTimeLabel)   m_recTimeLabel->setVisible(recording);
    if (m_recTracksLabel) m_recTracksLabel->setVisible(recording);
    if (m_recPathLabel)   m_recPathLabel->setVisible(recording);

    if (!recording) {
        setWindowTitle(m_baseWindowTitle);
        if (m_outputWindow)
            m_outputWindow->setRecordingActive(false);
        return;
    }

    if (elapsedMs < 0)
        elapsedMs = m_outputHub->longestActiveRecordingMs();

    const QString elapsed = MainWindowUtils::formatRecordingElapsed(elapsedMs);
    const QString tracks  = m_outputHub->activeRecordingTrackLabels().join(QStringLiteral(" · "));

    if (m_recTimeLabel)
        m_recTimeLabel->setText(elapsed);
    if (m_recTracksLabel)
        m_recTracksLabel->setText(tracks);
    if (m_recPathLabel)
        m_recPathLabel->setText(m_outputHub->outputDir());

    setWindowTitle(m_baseWindowTitle + QStringLiteral("  [REC %1]").arg(elapsed));
    if (m_outputWindow)
        m_outputWindow->setRecordingActive(true);
}

void MainWindow::onRecordingPanel() {
    showRecordingPanel();
}

void MainWindow::showRecordingPanel() {
    if (!m_recordingPanel) {
        m_recordingPanel = new RecordingSettingsDialog(m_outputHub, m_clipNodeEditor, this);
        m_recordingPanel->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_recordingPanel, &QObject::destroyed, this, [this]() {
            m_recordingPanel = nullptr;
        });
    }
    m_recordingPanel->show();
    m_recordingPanel->raise();
    m_recordingPanel->activateWindow();
    m_recordingPanel->syncFromHub();
}
