#include "ui/mainwindow/MainWindow.h"
#include "ui_MainWindow.h"
#include <functional>
#include "ui/canvas/VideoWidget.h"
#include "ui/output/MirrorOutputWindow.h"
#include "ui/mainwindow/SourcePrompt.h"
#include "ui/mainwindow/SourceFactory.h"
#include "core/media/ThumbnailExtractor.h"
#include "core/media/MediaFormats.h"
#include "core/media/MediaProbe.h"
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
#include "ui/mcp/McpAccessDialog.h"
#include "mcp/McpServer.h"
#include "mcp/McpJson.h"
#include "ui/mainwindow/MainWindowUtils.h"
#include "ui/common/Icons.h"
#include "ui/common/Theme.h"
#include "ui/output/FrameCaptureHelper.h"
#include "ui/recording/RecordingSettingsDialog.h"
#include "core/project/ClipManager.h"
#include "ui/common/AssetLibrary.h"
#include "ui/common/ThumbHelper.h"
#include "ui/common/PreferencesDialog.h"
#include "core/media/VideoDecoder.h"
#include <QActionGroup>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QApplication>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDir>
#include <QShortcut>
#include <QKeySequence>
#include <QFileDialog>
#include <QStandardPaths>
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
#include <QCloseEvent>
#include <QStatusBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QImage>
#include <QSet>
#include <QDesktopServices>
#include <QUrl>
#include <algorithm>
#include <numeric>

#include "ui/mainwindow/PrismSplashScreen.h"

// ── Constructor ───────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setAcceptDrops(true);
    m_baseWindowTitle = windowTitle();

    Icons::setActionIcon(ui->actionLoadFolder, Icons::Names::Description);
    Icons::setActionIcon(ui->actionAddFolder, Icons::Names::FolderOpen);
    Icons::setActionIcon(ui->actionSaveSession, Icons::Names::Save);
    Icons::setActionIcon(ui->actionLoadSession, Icons::Names::FolderOpen);
    Icons::setActionIcon(ui->actionExportProject, Icons::Names::Inventory);
    Icons::setActionIcon(ui->actionImportProject, Icons::Names::Download);
    Icons::setActionIcon(ui->actionClearAll, Icons::Names::Delete);
    Icons::setActionIcon(ui->actionAgentAccess, Icons::Names::Sensors);
    ui->aDeckPlayBtn->setProperty("primary", true);
    ui->bDeckPlayBtn->setProperty("primary", true);
    Icons::setPlayPause(ui->aDeckPlayBtn, false, 22);
    Icons::setPlayPause(ui->bDeckPlayBtn, false, 22);

    // Deck transport controls stay hidden until a clip that supports them is
    // assigned (pushDecks / assignNodeToDeck toggle them per capability).
    for (QWidget *w : std::initializer_list<QWidget *>{
             ui->aProgressSlider, ui->aDeckPlayBtn, ui->aDeckSpeedSlider, ui->speedLabelALabel,
             ui->bProgressSlider, ui->bDeckPlayBtn, ui->bDeckSpeedSlider, ui->speedLabelBLabel})
        w->setVisible(false);

    const RecordingOptions recOpts = RecordingSettingsDialog::loadSavedOptions();

    m_outputWindow = new OutputWindow();
    // QOpenGLWidget needs a shown native window before textures can be uploaded.
    // Keep the monitor off-screen until the user opens it from the Output node.
    m_outputWindow->move(-30000, -30000);
    m_outputWindow->show();
    QCoreApplication::processEvents();

    setupPreviewSplitters();
    setupRecordingStatusBar();

    m_outputHub = new OutputHub(this);
    m_outputHub->setProgramSource(m_outputWindow->videoWidget());
    m_outputHub->setOutputDir(recOpts.effectiveOutputDir());
    m_outputWindow->videoWidget()->addDeckPreviewConsumer();

    m_obsIntegration = new ObsIntegration(this);
    m_obsScenesMenu = new QMenu(tr("OBS Scenes"), this);
    ui->menuTools->insertMenu(ui->actionConnectObs, m_obsScenesMenu);
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

    // ── Asset library + node editor (left sidebar + canvas) ───────────────────
    m_assetLibrary = new AssetLibrary(&clipManager);
    m_assetLibrary->setMinimumWidth(160);

    m_clipNodeEditor = new ClipNodeEditor();
    m_clipNodeEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_editorSplitter = new QSplitter(Qt::Horizontal, ui->gridWidget);
    m_editorSplitter->addWidget(m_assetLibrary);
    m_editorSplitter->addWidget(m_clipNodeEditor);
    m_editorSplitter->setStretchFactor(0, 0);
    m_editorSplitter->setStretchFactor(1, 1);
    m_editorSplitter->setSizes({220, 800});
    ui->gridLayout->addWidget(m_editorSplitter, 0, 0, 1, 1);

    connect(m_assetLibrary, &AssetLibrary::addAsClipRequested,
            this, [this](const QString &path, const QPixmap &thumb) {
        m_clipNodeEditor->addClipNode(path, thumb);
    });

    // ── Controllers ───────────────────────────────────────────────────────────
    m_deckController = new DeckController(m_outputWindow, m_clipNodeEditor, m_outputHub, this);

    m_hotkeyManager = new HotkeyManager(this, m_clipNodeEditor, this);

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

    m_mcp = new prism::mcp::McpServer(this, this);
    connect(m_mcp, &prism::mcp::McpServer::runningChanged, this, [this]() {
        if (m_mcpStatusLabel)
            m_mcpStatusLabel->setVisible(m_mcp && m_mcp->running());
    });

    setupConnections();
    setupAddMenu(ui->menuAddElement);

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
    m_assetLibrary = nullptr;
    m_editorSplitter = nullptr;
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
    if (m_mcp)
        m_mcp->stop();

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
        m_hotkeyManager->hotkeysJson(),
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

    // Output menu
    connect(ui->actionSetOutputResolution, &QAction::triggered, this, &MainWindow::onSetOutputResolution);
    connect(ui->actionShowOutput, &QAction::triggered, this, &MainWindow::showOutputWindow);
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
    {
        auto *prefs = new QAction(tr("Playback && Hardware…"), this);
        ui->menuTools->insertAction(ui->actionEditHotkeys, prefs);
        connect(prefs, &QAction::triggered, this, [this] {
            PreferencesDialog dlg(this);
            dlg.exec();
        });
    }
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
    ui->actionStayOnTop->setChecked(false);

    auto *appearanceGroup = new QActionGroup(this);
    const std::pair<QAction *, Theme::Mode> appearanceModes[] = {
        {ui->actionAppearanceSystem, Theme::Mode::System},
        {ui->actionAppearanceLight, Theme::Mode::Light},
        {ui->actionAppearanceDark, Theme::Mode::Dark},
    };
    for (const auto &[action, mode] : appearanceModes) {
        appearanceGroup->addAction(action);
        action->setChecked(Theme::instance().mode() == mode);
        connect(action, &QAction::triggered, this, [mode] { Theme::instance().setMode(mode); });
    }

    connect(ui->actionStartRemoteControl, &QAction::triggered, this, &MainWindow::onStartRemoteControl);
    connect(ui->actionAgentAccess, &QAction::triggered, this, &MainWindow::onAgentAccess);

    // Help menu
    connect(ui->actionAboutPrism, &QAction::triggered, this, &MainWindow::onAboutPrism);
    connect(ui->actionPrismDocs, &QAction::triggered, this, [this]() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://docs.cutwire.org/prism")));
    });
    connect(ui->actionReportBugs, &QAction::triggered, this, [this]() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/CutWire-Studios/Prism/issues")));
    });
    connect(ui->actionAboutCutWire, &QAction::triggered, this, [this]() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://cutwire.org")));
    });

    // ClipNodeEditor signals
    connect(m_clipNodeEditor, &ClipNodeEditor::outputWindowRequested,
            this, &MainWindow::showOutputWindow);
    connect(m_clipNodeEditor, &ClipNodeEditor::clipChainChanged,
            this, &MainWindow::pushDecks);
    connect(m_clipNodeEditor, &ClipNodeEditor::addInputNodeRequested,
            this, [this]() {
        QMenu menu(this);
        setupSourceMenu(&menu);
        menu.exec(QCursor::pos());
    });
    // Hotkeys follow the A/B switcher inputs, which change with the wiring.
    connect(m_clipNodeEditor, &ClipNodeEditor::clipChainChanged,
            m_hotkeyManager, &HotkeyManager::syncWithGraph);
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
        tr("Media Files (%1)").arg(MediaFormats::globPattern(true, true, true)));
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
        tr("Media Files (%1)").arg(MediaFormats::globPattern(true, true, true)));
    if (files.isEmpty()) return;
    if (!m_assetLibrary->addFiles(files))
        return;
    for (const QString &path : files) {
        if (ClipManager::isMediaPath(path))
            m_clipNodeEditor->addClipNode(path, ThumbnailExtractor::extract(path, 110, 65));
    }
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

    // Validate the URL before adding the node
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool canOpen = hasStream(MediaProbe::probe(url), StreamInfo::Type::Video);
    QApplication::restoreOverrideCursor();

    if (!canOpen) {
        QMessageBox::critical(this, tr("Invalid Video URL"),
            tr("Could not open or play the video from URL:\n%1\n\n"
               "Please check the URL and ensure the video stream is accessible and valid.").arg(url));
        return;
    }

    // Create the Clip Node
    QPixmap thumb = ThumbHelper::makeIconThumb(Icons::Names::Link);
    ClipNodeModel *node = m_clipNodeEditor->addClipNode(url, thumb);
    if (node) {
        node->setDisplayName(name);
    }
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
}

// ── Element management ────────────────────────────────────────────────────────

void MainWindow::addElementNode(const SourceDescriptor &desc, const QPixmap &thumb) {
    m_clipNodeEditor->addSourceNode(desc, thumb);
    if ((desc.kind == SourceDescriptor::Kind::VideoFile
         || desc.kind == SourceDescriptor::Kind::Image)
        && !desc.path.isEmpty() && QFileInfo(desc.path).isFile()) {
        m_assetLibrary->addFiles({desc.path});
    }
}

void MainWindow::addSourceOfKind(SourceDescriptor::Kind kind) {
    SourceDescriptor desc;
    QPixmap thumb;
    if (SourcePrompt::prompt(kind, this, desc, thumb))
        addElementNode(desc, thumb);
}

void MainWindow::setupSourceMenu(QMenu *menu) {
    if (!menu) return;
    SourcePrompt::buildMenu(menu,
                            [this]() { onAddFilesClicked(); },
                            [this]() { onAddVideoUrlClicked(); },
                            [this](SourceDescriptor::Kind kind) { addSourceOfKind(kind); },
                            [this]() {
                                if (m_clipNodeEditor)
                                    m_clipNodeEditor->addMicInputAtCursor();
                            },
                            [this]() {
                                if (m_clipNodeEditor)
                                    m_clipNodeEditor->addAudioCaptureAtCursor();
                            },
                            NdiSource::isAvailable(),
#ifdef PRISM_HAVE_WEBRTC
                            WebRtcSource::isAvailable());
#else
                            false);
#endif
}

void MainWindow::setupAddMenu(QMenu *menu) {
    if (!menu) return;
    setupSourceMenu(menu);
    if (!m_clipNodeEditor) return;
    menu->addSeparator();
    m_clipNodeEditor->populateAddNodeMenu(menu);
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
    // Identity key for deck-source reuse. Recursive: a flattened Layer composite
    // hashes its sub-layers (content + placement) and canvas so the deck rebuilds
    // when the group changes but not every frame when it is unchanged.
    std::function<QString(const ResolvedLayer &)> layerKey =
        [&](const ResolvedLayer &l) -> QString {
        if (l.composite) {
            QString k = QStringLiteral("L%1:%2x%3{")
                .arg(l.layerNodeId).arg(l.composite->canvasWidth).arg(l.composite->canvasHeight);
            for (const ResolvedLayer &sl : l.composite->layers) {
                k += layerKey(sl)
                   + QStringLiteral("@%1,%2,%3,%4,%5;")
                       .arg(sl.baseX).arg(sl.baseY).arg(sl.baseW).arg(sl.baseH)
                       .arg((sl.visible ? 1 : 0));
            }
            return k + QStringLiteral("}") + ProcessEffects::sourceEffectsKey(l.sourceEffects);
        }
        ClipNodeModel *n = m_clipNodeEditor->nodeAt(l.inputNodeId);
        if (!n) return QStringLiteral("%1:?").arg(l.inputNodeId);
        const SourceDescriptor &d = n->sourceDescriptor();
        return QStringLiteral("%1:%2:%3:%4:%5:%6:%7:%8")
            .arg(l.inputNodeId).arg((int)d.kind).arg(d.path)
            .arg(n->startTime()).arg(n->endTime()).arg(d.captureId)
            .arg(ProcessEffects::sourceEffectsKey(l.sourceEffects))
            .arg(qHash(d.contentKey()));
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

        // Flattened Layer group as the deck primary: no single input node, so it
        // takes a dedicated path. The composite delegates its timeline to its
        // bottom clip, so scrub/speed/play/audio bind to that clip's node.
        if (base.composite) {
            const NodeId primaryId = base.composite->layers.isEmpty()
                ? 0 : base.composite->layers.first().inputNodeId;
            ClipNodeModel *primaryNode = m_clipNodeEditor->nodeAt(primaryId);
            const bool speedControl =
                primaryNode && primaryNode->sourceDescriptor().hasSpeedControl();
            speedSlider->setVisible(speedControl);
            speedLabel->setVisible(speedControl);
            auto applyCompositeBase = [&]() {
                if (deckA) {
                    out->setBaseA(base.baseX, base.baseY, base.baseW, base.baseH);
                    out->setCropA(0.f, 0.f, 1.f, 1.f);
                    out->setFlipA(false, false);
                    out->setCanvasSizeA(stream.canvasWidth, stream.canvasHeight);
                } else {
                    out->setBaseB(base.baseX, base.baseY, base.baseW, base.baseH);
                    out->setCropB(0.f, 0.f, 1.f, 1.f);
                    out->setFlipB(false, false);
                    out->setCanvasSizeB(stream.canvasWidth, stream.canvasHeight);
                }
            };
            if (sameStream(dBase, dOv, curBase, curOv)) {
                applyCompositeBase();
            } else {
                auto src = SourceFactory::buildLayerSource(base, m_clipNodeEditor);
                if (!src) {
                    if (deckA) { out->clearDeckA(); m_deckController->setActiveNodeA(0); }
                    else       { out->clearDeckB(); m_deckController->setActiveNodeB(0); }
                    curBase.clear(); curOv.clear();
                    return;
                }
                m_deckController->assignCompositeToDeck(std::move(src), primaryId, primaryNode,
                    deckA, slider, playBtn, selLabel, timeLabel);
                applyCompositeBase();
                if (deckA) out->setNodeChainA({});
                else       out->setNodeChainB({});
                curBase = dBase; curOv = dOv;
            }
            return;
        }

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
                                               slider, playBtn, selLabel, timeLabel,
                                               base.sourceEffects);
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
    m_clipNodeEditor->removeNode(nodeId);
    auto *out = m_outputWindow->videoWidget();
    if (m_deckController->activeNodeA() == nodeId) { m_deckController->setActiveNodeA(0); out->clearDeckA(); m_deckBaseA.clear(); m_deckOverlaysA.clear(); }
    if (m_deckController->activeNodeB() == nodeId) { m_deckController->setActiveNodeB(0); out->clearDeckB(); m_deckBaseB.clear(); m_deckOverlaysB.clear(); }
    if (!m_deckController->activeNodeA()) m_deckController->stopDeckAudio(true);
    if (!m_deckController->activeNodeB()) m_deckController->stopDeckAudio(false);
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

    // A clip whose hardware decode failed has already switched itself to software; say so once.
    if (const quint64 fallbacks = VideoDecoder::hardwareFallbackCount();
        fallbacks != m_seenHwFallbacks) {
        m_seenHwFallbacks = fallbacks;
        statusBar()->showMessage(
            tr("Hardware decoding failed for a clip, playing it in software: %1")
                .arg(VideoDecoder::lastHardwareFailure()), 10000);
    }

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
    Icons::setPlayPause(ui->aDeckPlayBtn, out->isPlayingA(), 22);

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
    Icons::setPlayPause(ui->bDeckPlayBtn, out->isPlayingB(), 22);

    // Keep shader audio analysis phase-locked to the live deck clocks.
    m_deckController->refreshShaderAudioForActiveDecks();
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

// ── Session save / load ───────────────────────────────────────────────────────

void MainWindow::onSaveSessionClicked() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Session", QString(),
        QStringLiteral("CutWire Prism Session (*%1);;All Files (*)").arg(SessionManager::kSessionExtension));
    if (path.isEmpty()) return;
    path = MainWindowUtils::ensureExtension(path, QString::fromUtf8(SessionManager::kSessionExtension));

    if (!m_sessionManager->writeSessionFile(currentSessionJson(path), path)) {
        QMessageBox::warning(this, "Save Session",
                             QString("Cannot write to file:\n%1").arg(path));
    }
}

void MainWindow::onLoadSessionClicked() {
    QString path = QFileDialog::getOpenFileName(
        this, "Load Session", QString(),
        QStringLiteral("CutWire Prism Session (*%1);;All Files (*)").arg(SessionManager::kSessionExtension));
    if (path.isEmpty()) return;
    loadFromFile(path, true);
}

void MainWindow::onExportProjectClicked() {
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Project"), QString(),
        tr("CutWire Prism Project (*%1);;All Files (*)").arg(ProjectPackager::kExtension));
    if (path.isEmpty()) return;
    path = MainWindowUtils::ensureExtension(path, QString::fromUtf8(ProjectPackager::kExtension));

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
        tr("CutWire Prism Project (*%1);;All Files (*)").arg(ProjectPackager::kExtension));
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

    m_sessionManager->loadFromFile(path, showErrors);
}

void MainWindow::onSessionLoaded() {
    m_assetLibrary->rebuild();

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

    auto *saveCheck = new QCheckBox(tr("Save PNG to the Captures folder"), &dlg);
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
        const QString baseDir = ensureOutputDir();
        if (baseDir.isEmpty()) {
            if (!holdCheck->isChecked())
                return;
        } else {
            savedPath = FrameCaptureHelper::savePng(frame, layer.label, baseDir);
            if (savedPath.isEmpty()) {
                QMessageBox::warning(this, tr("Freeze Frame Capture"),
                                     tr("Captured the frame but could not save it to disk."));
                if (!holdCheck->isChecked())
                    return;
            }
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

void MainWindow::onSetOutputResolution() {
    static const QList<QSize> kPresets = {
        {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160},
    };

    auto *videoWidget = m_outputWindow->videoWidget();
    const QSize current = videoWidget->programFrameSize();

    QStringList items;
    for (const QSize &s : kPresets)
        items << tr("%1 x %2").arg(s.width()).arg(s.height());
    items << tr("Custom…");

    int currentIndex = kPresets.indexOf(current);
    if (currentIndex < 0) currentIndex = items.size() - 1;

    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, tr("Set Output Resolution"), tr("Program output resolution:"),
        items, currentIndex, false, &ok);
    if (!ok) return;

    QSize target;
    const int presetIndex = items.indexOf(choice);
    if (presetIndex >= 0 && presetIndex < kPresets.size()) {
        target = kPresets[presetIndex];
    } else {
        const int width = QInputDialog::getInt(
            this, tr("Set Output Resolution"), tr("Width:"),
            current.width(), 160, 7680, 2, &ok);
        if (!ok) return;
        const int height = QInputDialog::getInt(
            this, tr("Set Output Resolution"), tr("Height:"),
            current.height(), 90, 4320, 2, &ok);
        if (!ok) return;
        target = QSize(width, height);
    }

    if (target == current) return;
    videoWidget->setProgramResolution(target.width(), target.height());
}

void MainWindow::onAboutPrism() {
    auto *about = new PrismSplashScreen();
    about->setAttribute(Qt::WA_DeleteOnClose);
    about->setProgress(100, tr("CutWire Prism"));
    about->show();
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

void MainWindow::onAgentAccess() {
    if (!m_mcpDialog) {
        m_mcpDialog = new McpAccessDialog(this, this);
        m_mcpDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_mcpDialog, &QObject::destroyed, this, [this]() {
            m_mcpDialog = nullptr;
        });
    }
    m_mcpDialog->show();
    m_mcpDialog->raise();
    m_mcpDialog->activateWindow();
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

bool MainWindow::mcpEnabled() const {
    return m_mcp && m_mcp->running();
}

void MainWindow::setMcpEnabled(bool on)
{
    if (!m_mcp)
        return;
    if (on)
        m_mcp->start();
    else
        m_mcp->stop();
}

void MainWindow::mcpBumpRevision() {
    ++m_mcpEditRevision;
}

void MainWindow::playDeck(bool deckA, bool play)
{
    if (!m_outputWindow)
        return;
    auto *out = m_outputWindow->videoWidget();
    if (deckA) {
        if (play) out->playA(); else out->pauseA();
        if (m_deckController)
            m_deckController->applyAudioControllerToDeck(true, m_deckController->activeNodeA());
    } else {
        if (play) out->playB(); else out->pauseB();
        if (m_deckController)
            m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB());
    }
}

void MainWindow::seekDeck(bool deckA, double seconds)
{
    if (!m_outputWindow)
        return;
    auto *out = m_outputWindow->videoWidget();
    if (deckA)
        out->seekA(seconds);
    else
        out->seekB(seconds);
}

void MainWindow::setDeckSpeedValue(bool deckA, double speed)
{
    const int sliderVal = qBound(5, qRound(speed / 0.05), 80);
    if (deckA)
        ui->aDeckSpeedSlider->setValue(sliderVal);
    else
        ui->bDeckSpeedSlider->setValue(sliderVal);
}

void MainWindow::setFaderValue(int value)
{
    ui->crossfaderSlider->setValue(qBound(0, value, 100));
}

int MainWindow::faderValue() const
{
    return ui->crossfaderSlider->value();
}

QString MainWindow::mcpPanicMode() const
{
    if (!m_outputWindow)
        return QStringLiteral("none");
    auto *out = m_outputWindow->videoWidget();
    if (out->isOutputFrozen())
        return QStringLiteral("freeze");
    switch (out->panicOverlay()) {
    case VideoWidget::PanicOverlay::Blackout:
        return QStringLiteral("blackout");
    case VideoWidget::PanicOverlay::StayTuned:
        return QStringLiteral("stay_tuned");
    case VideoWidget::PanicOverlay::None:
        break;
    }
    return QStringLiteral("none");
}

bool MainWindow::mcpSetPanic(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String("none")) {
        ui->panicBlackoutBtn->setChecked(false);
        ui->panicPauseBtn->setChecked(false);
        ui->panicStayTunedBtn->setChecked(false);
        applyPanicFromButtons();
        return true;
    }
    if (m == QLatin1String("blackout")) {
        syncPanicButtons(ui->panicBlackoutBtn);
        applyPanicFromButtons();
        return true;
    }
    if (m == QLatin1String("freeze") || m == QLatin1String("pause")) {
        syncPanicButtons(ui->panicPauseBtn);
        applyPanicFromButtons();
        return true;
    }
    if (m == QLatin1String("stay_tuned") || m == QLatin1String("staytuned")) {
        syncPanicButtons(ui->panicStayTunedBtn);
        applyPanicFromButtons();
        return true;
    }
    return false;
}

ClipNodeModel *MainWindow::addSourceFromDescriptor(const SourceDescriptor &desc, const QPixmap &thumb)
{
    QSet<NodeId> before;
    if (m_clipNodeEditor) {
        for (ClipNodeModel *n : m_clipNodeEditor->allNodes()) {
            if (n)
                before.insert(n->nodeId());
        }
    }
    addElementNode(desc, thumb);
    mcpBumpRevision();
    if (!m_clipNodeEditor)
        return nullptr;
    for (ClipNodeModel *n : m_clipNodeEditor->allNodes()) {
        if (n && n->hasSource() && !before.contains(n->nodeId()))
            return n;
    }
    return nullptr;
}

bool MainWindow::mcpUpdateSource(NodeId id, const SourceDescriptor &desc, const QPixmap &thumb)
{
    ClipNodeModel *node = m_clipNodeEditor ? m_clipNodeEditor->nodeAt(id) : nullptr;
    if (!node)
        return false;
    node->loadSource(desc, thumb);
    pushDecks();
    mcpBumpRevision();
    return true;
}

bool MainWindow::mcpRenameClip(NodeId id, const QString &name)
{
    ClipNodeModel *node = m_clipNodeEditor ? m_clipNodeEditor->nodeAt(id) : nullptr;
    if (!node)
        return false;
    node->setDisplayName(name);
    mcpBumpRevision();
    return true;
}

bool MainWindow::mcpRemoveClip(NodeId id)
{
    if (!m_clipNodeEditor || !m_clipNodeEditor->nodeAt(id))
        return false;
    m_clipNodeEditor->removeNode(id);
    mcpBumpRevision();
    return true;
}

bool MainWindow::mcpSaveSession(const QString &path)
{
    const QString out = MainWindowUtils::ensureExtension(path, QString::fromUtf8(SessionManager::kSessionExtension));
    return m_sessionManager && m_sessionManager->writeSessionFile(currentSessionJson(out), out);
}

bool MainWindow::mcpLoadSession(const QString &path)
{
    loadFromFile(path, false);
    return true;
}

bool MainWindow::mcpStartRecording(const QString &dir, QString *error)
{
    if (!m_outputHub) {
        if (error)
            *error = tr("No output hub");
        return false;
    }
    QString outDir = dir;
    if (outDir.isEmpty()) {
        if (!RecordingSettingsDialog::hasChosenOutputDir()) {
            if (error)
                *error = tr("No save location. Pass dir or set it in the recording panel.");
            return false;
        }
        outDir = RecordingSettingsDialog::loadSavedOptions().effectiveOutputDir();
    } else {
        RecordingSettingsDialog::saveOutputDir(outDir);
    }
    m_outputHub->setOutputDir(outDir);
    if (!m_outputHub->startProgramRecording()) {
        if (error)
            *error = tr("Could not start program recording");
        return false;
    }
    mcpBumpRevision();
    return true;
}

namespace {

QString mcpKindName(SourceDescriptor::Kind kind)
{
    using K = SourceDescriptor::Kind;
    switch (kind) {
    case K::VideoFile: return QStringLiteral("video");
    case K::Image:     return QStringLiteral("image");
    case K::Slideshow: return QStringLiteral("slideshow");
    case K::Camera:    return QStringLiteral("camera");
    case K::Screen:    return QStringLiteral("screen");
    case K::Canvas:    return QStringLiteral("canvas");
    case K::Window:    return QStringLiteral("window");
    case K::Shader:    return QStringLiteral("shader");
    case K::Html:      return QStringLiteral("html");
    case K::Ndi:       return QStringLiteral("ndi");
    case K::WebRtc:    return QStringLiteral("webrtc");
    case K::Text:      return QStringLiteral("text");
    case K::AudioFile: return QStringLiteral("audio");
    }
    return QStringLiteral("unknown");
}

QJsonObject mcpDeckJson(const MainWindow *w, bool deckA)
{
    auto *out = w->outputWindow() ? w->outputWindow()->videoWidget() : nullptr;
    auto *decks = w->deckController();
    const NodeId id = decks ? (deckA ? decks->activeNodeA() : decks->activeNodeB()) : 0;
    ClipNodeModel *node = (id && w->clipNodeEditor()) ? w->clipNodeEditor()->nodeAt(id) : nullptr;
    QJsonObject o{
        {QStringLiteral("clip"), id ? QString::number(id) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("name"), node ? node->sourceName() : QString()},
        {QStringLiteral("playing"), out && (deckA ? out->isPlayingA() : out->isPlayingB())},
        {QStringLiteral("time"), out ? (deckA ? out->getCurrentTimeA() : out->getCurrentTimeB()) : 0.0},
        {QStringLiteral("duration"), out ? (deckA ? out->getDurationA() : out->getDurationB()) : 0.0},
        {QStringLiteral("speed"), decks ? decks->deckSpeed(deckA) : 1.0},
    };
    return o;
}

} // namespace

QJsonObject MainWindow::mcpInspect(bool includeClips, bool detail, int sinceRevision,
                                   bool includeGraph) const
{
    using namespace prism::mcp;
    if (sinceRevision >= 0 && sinceRevision == m_mcpEditRevision)
        return ok({{QStringLiteral("unchanged"), true},
                   {QStringLiteral("revision"), m_mcpEditRevision}});

    auto *t = m_transitionCtrl;
    auto *hub = m_outputHub;

    QJsonArray transitionModes;
    if (t) {
        for (const QString &name : t->transitionModeNames())
            transitionModes.append(name);
    }

    QJsonObject body{
        {QStringLiteral("revision"), m_mcpEditRevision},
        {QStringLiteral("fader"), ui->crossfaderSlider->value()},
        {QStringLiteral("panic"), mcpPanicMode()},
        {QStringLiteral("decks"),
         QJsonObject{{QStringLiteral("a"), mcpDeckJson(this, true)},
                     {QStringLiteral("b"), mcpDeckJson(this, false)}}},
        {QStringLiteral("transition"),
         QJsonObject{
             {QStringLiteral("index"), t ? t->currentModeIndex() : 0},
             {QStringLiteral("duration"), t ? t->currentDurationSecs() : 0.0},
             {QStringLiteral("modes"), transitionModes},
         }},
        {QStringLiteral("program"),
         QJsonObject{{QStringLiteral("w"), VideoWidget::programWidth()},
                     {QStringLiteral("h"), VideoWidget::programHeight()}}},
        {QStringLiteral("recording"),
         QJsonObject{
             {QStringLiteral("active"), hub && hub->isRecording()},
             {QStringLiteral("elapsed"), hub ? hub->longestActiveRecordingMs() / 1000.0 : 0.0},
             {QStringLiteral("dir"), hub ? hub->outputDir() : QString()},
         }},
        {QStringLiteral("ndi"),
         QJsonObject{{QStringLiteral("available"), hub && hub->ndiAvailable()},
                     {QStringLiteral("enabled"), hub && hub->ndiOutputEnabled()},
                     {QStringLiteral("name"), hub ? hub->ndiStreamName() : QString()}}},
        {QStringLiteral("virtualCamera"),
         QJsonObject{{QStringLiteral("available"), hub && hub->virtualCameraAvailable()},
                     {QStringLiteral("enabled"), hub && hub->virtualCameraEnabled()}}},
    };

    if (includeClips && m_clipNodeEditor) {
        QJsonArray clips;
        for (ClipNodeModel *node : m_clipNodeEditor->allNodes()) {
            if (!node || !node->hasSource())
                continue;
            const SourceDescriptor &d = node->sourceDescriptor();
            QJsonObject row{
                {QStringLiteral("id"), QString::number(node->nodeId())},
                {QStringLiteral("name"), node->sourceName()},
                {QStringLiteral("kind"), mcpKindName(d.kind)},
                {QStringLiteral("activeA"), node->nodeId() == activeNodeA()},
                {QStringLiteral("activeB"), node->nodeId() == activeNodeB()},
            };
            if (detail) {
                if (!d.path.isEmpty())
                    row.insert(QStringLiteral("path"), d.path);
                if (!d.textTemplate.isEmpty())
                    row.insert(QStringLiteral("text"), d.textTemplate);
                if (!d.htmlContent.isEmpty())
                    row.insert(QStringLiteral("html"), d.htmlContent);
                if (!d.shaderCode.isEmpty())
                    row.insert(QStringLiteral("shader"), true);
                row.insert(QStringLiteral("live"), d.isLiveSource());
                row.insert(QStringLiteral("seekable"), d.isSeekable());
            }
            clips.append(row);
        }
        body.insert(QStringLiteral("clips"), clips);
    } else if (m_clipNodeEditor) {
        int n = 0;
        for (ClipNodeModel *node : m_clipNodeEditor->allNodes()) {
            if (node && node->hasSource())
                ++n;
        }
        body.insert(QStringLiteral("clips"), n);
    }

    if (includeGraph && m_clipNodeEditor)
        body.insert(QStringLiteral("graph"), m_clipNodeEditor->graphSnapshot());

    return ok(body);
}

QJsonObject MainWindow::mcpCaptureFrame(bool full)
{
    using namespace prism::mcp;
    if (!m_outputWindow)
        return textResult(err("capture_failed", QStringLiteral("No program output")), true);

    auto *out = m_outputWindow->videoWidget();
    QImage frame = out->captureProgramFrame();
    if (frame.isNull())
        frame = out->programFrame();
    if (frame.isNull())
        return textResult(err("capture_failed", QStringLiteral("No program frame yet")), true);

    const QJsonObject meta = ok({
        {QStringLiteral("w"), frame.width()},
        {QStringLiteral("h"), frame.height()},
        {QStringLiteral("full"), full},
    });

    if (full) {
        QString baseDir = m_outputHub ? m_outputHub->outputDir() : QString();
        if (baseDir.isEmpty())
            baseDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        const QString path = FrameCaptureHelper::savePng(frame, QStringLiteral("mcp"), baseDir);
        if (path.isEmpty())
            return textResult(err("capture_failed", QStringLiteral("Could not write PNG")), true);
        QJsonObject withPath = meta;
        withPath.insert(QStringLiteral("path"), path);
        return textResult(withPath);
    }

    QImage jpegFrame = frame;
    const int longEdge = qMax(jpegFrame.width(), jpegFrame.height());
    if (longEdge > 1280) {
        const double scale = 1280.0 / double(longEdge);
        jpegFrame = jpegFrame.scaled(qRound(jpegFrame.width() * scale),
                                     qRound(jpegFrame.height() * scale),
                                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!jpegFrame.save(&buffer, "JPEG", 80))
        return textResult(err("capture_failed", QStringLiteral("Could not encode JPEG")), true);

    QJsonArray content;
    content.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact))},
    });
    content.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("image")},
        {QStringLiteral("mimeType"), QStringLiteral("image/jpeg")},
        {QStringLiteral("data"), QString::fromLatin1(jpeg.toBase64())},
    });
    return {{QStringLiteral("content"), content}, {QStringLiteral("isError"), false}};
}

void MainWindow::setupRecordingStatusBar() {
    auto *bar = statusBar();
    bar->setSizeGripEnabled(false);

    m_recStatusLabel = new QLabel(tr("● REC"), this);
    QFont recFont = m_recStatusLabel->font();
    recFont.setBold(true);
    m_recStatusLabel->setFont(recFont);
    m_recStatusLabel->setProperty("role", "danger");
    m_recStatusLabel->hide();

    m_recTimeLabel = new QLabel(this);
    m_recTimeLabel->hide();

    m_recTracksLabel = new QLabel(this);
    m_recTracksLabel->hide();

    m_recPathLabel = new QLabel(this);
    m_recPathLabel->setProperty("role", "secondary");
    m_recPathLabel->hide();

    bar->addPermanentWidget(m_recStatusLabel);
    bar->addPermanentWidget(m_recTimeLabel);
    bar->addPermanentWidget(m_recTracksLabel, 1);
    bar->addPermanentWidget(m_recPathLabel);

    m_mcpStatusLabel = new QLabel(tr("Agent access"), this);
    m_mcpStatusLabel->setProperty("role", "warning");
    m_mcpStatusLabel->setToolTip(tr("Localhost MCP is listening. Turn it off in Run → Agent Access when you are done."));
    m_mcpStatusLabel->hide();
    bar->addPermanentWidget(m_mcpStatusLabel);
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

QString MainWindow::ensureOutputDir() {
    if (!RecordingSettingsDialog::hasChosenOutputDir()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Save Location Not Set"));
        box.setText(tr("No save location is set for recordings and captures."));
        box.setInformativeText(tr("Choose a folder before you can record or capture."));
        QPushButton *setBtn = box.addButton(tr("Set Location…"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != setBtn)
            return {};

        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Choose a folder to save recordings and captures"),
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
        if (dir.isEmpty())
            return {};
        RecordingSettingsDialog::saveOutputDir(dir);
    }
    return RecordingSettingsDialog::loadSavedOptions().effectiveOutputDir();
}

void MainWindow::showRecordingPanel() {
    if (ensureOutputDir().isEmpty())
        return;

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

void MainWindow::showOutputWindow() {
    if (!m_outputWindow)
        return;
    if (!m_outputWindowUserPlaced) {
        const auto screens = QGuiApplication::screens();
        if (screens.size() >= 2) {
            QScreen *secondary = screens.at(1);
            m_outputWindow->setScreen(secondary);
            m_outputWindow->setGeometry(secondary->geometry());
        } else {
            m_outputWindow->move(100, 100);
            m_outputWindow->resize(800, 600);
        }
        m_outputWindowUserPlaced = true;
    }
    m_outputWindow->show();
    m_outputWindow->raise();
    m_outputWindow->activateWindow();
    if (VideoWidget *vw = m_outputWindow->videoWidget())
        vw->update();
    pushDecks();
}
