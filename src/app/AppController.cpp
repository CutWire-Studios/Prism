#include <QCameraDevice>
#include "app/AppController.h"

#include "app/bin/MediaThumbnailProvider.h"
#include "app/common/ThumbHelper.h"
#include "app/graph/GraphRuntime.h"
#include "app/mainwindow/DeckController.h"
#include "app/mainwindow/SourceFactory.h"
#include "app/output/FrameCaptureHelper.h"
#include "app/output/OutputHub.h"
#include "app/session/SessionManager.h"
#include "app/transitions/TransitionController.h"
#include "core/graph/ProcessEffects.h"
#include "core/media/MediaFormats.h"
#include "core/media/MediaProbe.h"
#include "core/media/VideoDecoder.h"
#include "core/project/ProjectPackager.h"
#include "mcp/McpJson.h"
#include "mcp/McpServer.h"
#include "core/platform/ErrorReporter.h"
#include "app/hotkeys/HotkeyManager.h"
#include "app/common/MainWindowUtils.h"
#include "app/obs/ObsIntegration.h"
#include "app/obs/ObsWebSocketClient.h"
#include "app/recording/RecordingOptions.h"
#include "app/remote/OscServer.h"
#include "app/remote/RemoteControlServer.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJSEngine>
#include <QMediaDevices>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstring>
#include <functional>

AppController *AppController::s_instance = nullptr;

namespace {

constexpr int kMaxRecentSessions = 10;

QString sessionFilter()
{
    return QStringLiteral("CutWire Prism Session (*%1);;All Files (*)")
        .arg(QString::fromUtf8(SessionManager::kSessionExtension));
}

QString projectFilter()
{
    return QStringLiteral("CutWire Prism Project (*%1);;All Files (*)")
        .arg(QString::fromUtf8(ProjectPackager::kExtension));
}

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
    case K::Ndi:       return QStringLiteral("ndi");
    case K::WebRtc:    return QStringLiteral("webrtc");
    case K::Text:      return QStringLiteral("text");
    case K::Shape:     return QStringLiteral("shape");
    case K::AudioFile: return QStringLiteral("audio");
    case K::SvgTemplate: return QStringLiteral("svg_template");
    default: break;
    }
    return QStringLiteral("unknown");
}

QString normalizePanic(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String("none") || m.isEmpty())
        return QStringLiteral("none");
    if (m == QLatin1String("blackout"))
        return m;
    if (m == QLatin1String("freeze") || m == QLatin1String("pause"))
        return QStringLiteral("freeze");
    if (m == QLatin1String("stay_tuned") || m == QLatin1String("staytuned"))
        return QStringLiteral("stay_tuned");
    return {};
}

} // namespace

AppController *AppController::instance()
{
    return s_instance;
}

AppController *AppController::create(QQmlEngine *engine, QJSEngine *)
{
    if (!s_instance)
        return nullptr;
    if (engine && !s_instance->m_engine)
        s_instance->setEngine(engine);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    ErrorReporter::setHandler([this](const QString &text) { emit errorOccurred(text); });

    m_compositor = std::make_unique<Compositor>();
    m_graph = new ClipGraph(this);
    m_graph->setOutputCanvasSize(Compositor::programWidth(), Compositor::programHeight());
    m_graphController = new ClipGraphController(m_graph, this);
    m_runtime = new GraphRuntime(m_graph, this);
    connect(m_runtime, &GraphRuntime::thumbnailChanged, this, &AppController::nodeThumbnailChanged);

    const RecordingOptions recOpts = RecordingOptions::loadSaved();
    m_outputHub = new OutputHub(this);
    m_outputHub->setProgramSource(m_compositor.get());
    m_outputHub->setOutputDir(recOpts.effectiveOutputDir());
    m_compositor->addDeckPreviewConsumer();

    m_obsIntegration = new ObsIntegration(this);
    m_deckController = new DeckController(m_compositor.get(), m_graph, m_runtime, m_outputHub, this);
    m_hotkeyManager = new HotkeyManager(m_graph, this);
    m_sessionManager = new SessionManager(m_graph, m_compositor.get(), &m_clipManager, this);
    m_transitionCtrl = new TransitionController(m_compositor.get(), this);
    m_remoteServer = new RemoteControlServer(this, this);
    m_oscServer = new OscServer(this, this);
    m_mcp = new prism::mcp::McpServer(this, this);

    m_pushTimer = new QTimer(this);
    m_pushTimer->setSingleShot(true);
    m_pushTimer->setInterval(0);
    connect(m_pushTimer, &QTimer::timeout, this, &AppController::pushDecks);

    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &AppController::onTimerTick);

    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &AppController::performAutosave);

    connectSignals();
}

AppController::~AppController()
{
    shutdown(false);
    delete m_mcp;
    delete m_oscServer;
    delete m_remoteServer;
    delete m_hotkeyManager;
    delete m_transitionCtrl;
    delete m_sessionManager;
    delete m_deckController;
    delete m_outputHub;
    delete m_runtime;
    delete m_graphController;
    delete m_graph;
    m_compositor.reset();
    if (s_instance == this) {
        ErrorReporter::setHandler({});
        s_instance = nullptr;
    }
}

QString AppController::localPath(const QUrl &url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

void AppController::setEngine(QQmlEngine *engine)
{
    m_engine = engine;
    m_outputHub->setQmlEngine(engine);
    // ── Media library ──
    engine->addImageProvider(QStringLiteral("mediabin"), new MediaThumbnailProvider);
}

// ── Media library ──
void AppController::notifyLibraryChanged()
{
    markDirty();
}

void AppController::connectSignals()
{
    connect(m_graph, &ClipGraph::changed, this, &AppController::onGraphChanged);
    connect(m_graph, &ClipGraph::deckAChanged, this, [this]() { m_pushTimer->start(); });
    connect(m_graph, &ClipGraph::deckBChanged, this, [this]() { m_pushTimer->start(); });
    connect(m_graph, &ClipGraph::graphReset, this, [this]() { m_pushTimer->start(); });
    connect(m_graph, &ClipGraph::nodeChanged, this, &AppController::onNodeChanged);
    connect(m_graph, &ClipGraph::changed, m_hotkeyManager, &HotkeyManager::syncWithGraph);

    connect(m_sessionManager, &SessionManager::sessionLoaded, this, &AppController::onSessionLoaded);
    connect(m_sessionManager, &SessionManager::loadFailed, this, &AppController::errorOccurred);
    connect(m_sessionManager, &SessionManager::assetsMissing, this,
            [this](int count, const QString &notes) {
        emit infoMessage(tr("Missing assets"), tr("%1 asset(s) could not be found.\n%2").arg(count).arg(notes));
    });

    connect(m_transitionCtrl, &TransitionController::faderValueChanged, this, [this]() {
        if (m_deckController->activeNodeA())
            m_deckController->applyAudioControllerToDeck(true, m_deckController->activeNodeA(), false);
        if (m_deckController->activeNodeB())
            m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB(), false);
        emit faderValueChanged();
    });
    connect(m_transitionCtrl, &TransitionController::modeChanged, this, &AppController::transitionSettingsChanged);
    connect(m_transitionCtrl, &TransitionController::durationChanged, this, &AppController::transitionSettingsChanged);
    connect(m_transitionCtrl, &TransitionController::runningChanged, this, &AppController::transitionRunningChanged);

    connect(m_outputHub, &OutputHub::recordingStateChanged, this, [this]() {
        emit recordingChanged();
    });
    connect(m_outputHub, &OutputHub::recordingProgress, this, &AppController::recordingProgress);
    connect(m_outputHub, &OutputHub::recordingError, this, [this](const QString &msg) {
        if (!msg.isEmpty())
            emit errorOccurred(msg);
    });
    connect(m_outputHub, &OutputHub::ndiOutputEnabledChanged, this, &AppController::outputsChanged);
    connect(m_outputHub, &OutputHub::virtualCameraEnabledChanged, this, &AppController::outputsChanged);

    connect(m_obsIntegration, &ObsIntegration::connectedChanged, this, [this](bool on) {
        if (!on)
            m_obsScenes.clear();
        emit obsChanged();
    });
    connect(m_obsIntegration, &ObsIntegration::sceneListUpdated, this, [this](const QStringList &scenes) {
        m_obsScenes = scenes;
        emit obsChanged();
    });

    connect(m_mcp, &prism::mcp::McpServer::runningChanged, this, &AppController::mcpEnabledChanged);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void AppController::start()
{
    if (m_started)
        return;
    m_started = true;

    applyOscSettings();

    QTimer::singleShot(0, []() {
        [[maybe_unused]] auto _ = QMediaDevices::videoInputs();
    });

    m_updateTimer->start(100);

    const SessionManager::RecoveryInfo recovery = SessionManager::checkRecovery();
    if (recovery.uncleanShutdown) {
        emit recoveryRequested(recovery.autosavePath, recovery.backupPaths);
    } else if (QFile::exists(SessionManager::autosavePath())) {
        loadSessionFile(SessionManager::autosavePath());
    }
    m_autosaveTimer->start(SessionManager::kDefaultAutosaveIntervalMs);
    SessionManager::markRunning();
    refreshOutputsList();
}

void AppController::shutdown(bool saveAndMarkClean)
{
    if (m_shuttingDown)
        return;
    if (saveAndMarkClean)
        performAutosave();
    m_shuttingDown = true;

    m_updateTimer->stop();
    m_autosaveTimer->stop();
    m_pushTimer->stop();

    m_remoteServer->stopServer();
    m_oscServer->stop();
    m_mcp->stop();
    m_transitionCtrl->shutdown();

    m_deckController->setActiveNodeA(0);
    m_deckController->setActiveNodeB(0);
    m_deckController->releaseAllDeckAudio();
    m_deckController->releaseAllMasterAudioInputs();
    for (int i = 0; i < 2; ++i) {
        m_deckBase[i].clear();
        m_deckOverlays[i].clear();
    }

    disconnect(m_outputHub, nullptr, this, nullptr);
    m_outputHub->stopAllRecording();
    m_outputHub->setNdiOutputEnabled(false);
    m_outputHub->setVirtualCameraEnabled(false);
    m_outputHub->closeMirrorOutputs();
    m_outputHub->setProgramSource(nullptr);

    m_compositor->removeDeckPreviewConsumer();
    m_compositor->shutdownPlayback();
    m_obsIntegration->disconnectFromObs();

    if (saveAndMarkClean)
        SessionManager::markCleanExit();
}

// ── Property getters ──────────────────────────────────────────────────────────

quint64 AppController::activeNodeA() const { return m_deckController ? m_deckController->activeNodeA() : 0; }
quint64 AppController::activeNodeB() const { return m_deckController ? m_deckController->activeNodeB() : 0; }
double AppController::deckSpeedA() const { return m_deckController->deckSpeed(true); }
double AppController::deckSpeedB() const { return m_deckController->deckSpeed(false); }
bool AppController::isPlayingA() const { return m_compositor && m_compositor->isPlayingA(); }
bool AppController::isPlayingB() const { return m_compositor && m_compositor->isPlayingB(); }
double AppController::deckTimeA() const { return m_compositor->getCurrentTimeA(); }
double AppController::deckTimeB() const { return m_compositor->getCurrentTimeB(); }
double AppController::deckDurationA() const { return m_compositor->getDurationA(); }
double AppController::deckDurationB() const { return m_compositor->getDurationB(); }

int AppController::faderValue() const { return m_transitionCtrl->faderValue(); }

int AppController::onAirDeck() const
{
    const auto alphas = m_compositor->computeDeckAlphas();
    return alphas.second > alphas.first ? 1 : 0;
}

int AppController::transitionMode() const { return m_transitionCtrl->currentModeIndex(); }
QStringList AppController::transitionModeNames() const { return m_transitionCtrl->transitionModeNames(); }
double AppController::transitionDuration() const { return m_transitionCtrl->currentDurationSecs(); }
bool AppController::transitionRunning() const { return m_transitionCtrl->isRunning(); }

QString AppController::panicMode() const { return m_panic; }

QStringList AppController::recentSessions() const
{
    return QSettings().value(QStringLiteral("session/recent")).toStringList();
}

bool AppController::isRecording() const { return m_outputHub && m_outputHub->isRecording(); }
qint64 AppController::recordingElapsedMs() const { return m_outputHub ? m_outputHub->longestActiveRecordingMs() : 0; }
QStringList AppController::recordingTracks() const
{
    return m_outputHub ? m_outputHub->activeRecordingTrackLabels() : QStringList{};
}
QString AppController::outputDir() const { return m_outputHub ? m_outputHub->outputDir() : QString(); }

QVariantList AppController::outputs() const
{
    QVariantList list;
    bool windowOpen = false;
    for (const auto &w : m_outputHub->mirrorOutputs())
        windowOpen = windowOpen || (w && w->isVisible());
    list.append(QVariantMap{{"id", "window"}, {"label", tr("Program output window")},
                            {"available", true}, {"enabled", windowOpen}, {"detail", QString()}});
    list.append(QVariantMap{{"id", "ndi"}, {"label", tr("NDI output")},
                            {"available", m_outputHub->ndiAvailable()},
                            {"enabled", m_outputHub->ndiOutputEnabled()},
                            {"detail", m_outputHub->ndiStreamName()}});
    list.append(QVariantMap{{"id", "virtualCamera"}, {"label", tr("Virtual camera")},
                            {"available", m_outputHub->virtualCameraAvailable()},
                            {"enabled", m_outputHub->virtualCameraEnabled()},
                            {"detail", m_outputHub->virtualCameraDevicePath()}});
    return list;
}

int AppController::programWidth() const { return Compositor::programWidth(); }
int AppController::programHeight() const { return Compositor::programHeight(); }
int AppController::outputScaleModeInt() const { return int(Compositor::outputScaleMode()); }
Compositor::OutputScaleMode AppController::outputScaleMode() const { return Compositor::outputScaleMode(); }

bool AppController::obsAvailable() const { return ObsWebSocketClient::isAvailable(); }
bool AppController::obsConnected() const { return m_obsIntegration->isConnected(); }
bool AppController::mcpEnabled() const { return m_mcp && m_mcp->running(); }

// ── Property setters ──────────────────────────────────────────────────────────

void AppController::setFaderValue(int value) { m_transitionCtrl->setFaderValue(qBound(0, value, 100)); }
void AppController::setTransitionMode(int index) { m_transitionCtrl->setTransitionModeIndex(index); }
void AppController::setTransitionDuration(double secs) { m_transitionCtrl->setTransitionDuration(secs); }

void AppController::setPanicMode(const QString &mode)
{
    const QString normalized = normalizePanic(mode);
    if (normalized.isEmpty() || normalized == m_panic)
        return;
    applyPanic(normalized);
}

void AppController::applyPanic(const QString &mode)
{
    m_panic = mode;
    if (mode == QLatin1String("blackout") || mode == QLatin1String("stay_tuned")) {
        m_compositor->setOutputFrozen(false);
        m_compositor->setPanicOverlay(mode == QLatin1String("blackout")
                                          ? Compositor::PanicOverlay::Blackout
                                          : Compositor::PanicOverlay::StayTuned);
        m_deckController->stopDeckAudio(true);
        m_deckController->stopDeckAudio(false);
    } else if (mode == QLatin1String("freeze")) {
        m_compositor->setPanicOverlay(Compositor::PanicOverlay::None);
        m_compositor->setOutputFrozen(true);
        m_deckController->stopDeckAudio(true);
        m_deckController->stopDeckAudio(false);
    } else {
        m_compositor->setPanicOverlay(Compositor::PanicOverlay::None);
        m_compositor->setOutputFrozen(false);
        if (m_deckController->activeNodeA())
            m_deckController->applyAudioControllerToDeck(true, m_deckController->activeNodeA());
        if (m_deckController->activeNodeB())
            m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB());
    }
    emit panicModeChanged();
}

void AppController::togglePanic(const QString &mode)
{
    const QString normalized = normalizePanic(mode);
    if (normalized.isEmpty())
        return;
    setPanicMode(normalized == m_panic ? QStringLiteral("none") : normalized);
}

bool AppController::mcpSetPanic(const QString &mode)
{
    const QString normalized = normalizePanic(mode);
    if (normalized.isEmpty())
        return false;
    setPanicMode(normalized);
    return true;
}

void AppController::setOutputDir(const QString &dir)
{
    if (dir.isEmpty() || dir == m_outputHub->outputDir())
        return;
    RecordingOptions::saveOutputDir(dir);
    m_outputHub->setOutputDir(dir);
    emit outputDirChanged();
}

void AppController::setOutputScaleModeInt(int mode)
{
    setOutputScaleMode(static_cast<Compositor::OutputScaleMode>(qBound(0, mode, 2)));
}

void AppController::setOutputScaleMode(Compositor::OutputScaleMode mode)
{
    m_compositor->setOutputScaleMode(mode);
    emit outputScaleModeChanged();
}

void AppController::setOutputStayOnTop(bool on)
{
    if (m_stayOnTop == on)
        return;
    m_stayOnTop = on;
    for (const auto &w : m_outputHub->mirrorOutputs()) {
        if (w)
            w->setFlag(Qt::WindowStaysOnTopHint, on);
    }
    emit outputsChanged();
}

void AppController::setMcpEnabled(bool on)
{
    if (on)
        m_mcp->start();
    else
        m_mcp->stop();
}

void AppController::setProgramResolution(int width, int height)
{
    if (width == Compositor::programWidth() && height == Compositor::programHeight())
        return;
    // Recorders keep the size they started with; a change would stretch every later frame.
    if (m_outputHub->isRecording())
        return;
    m_compositor->setProgramResolution(width, height);
    m_graph->setOutputCanvasSize(width, height);
    emit programResolutionChanged();
}

// ── Decks ─────────────────────────────────────────────────────────────────────

void AppController::selectNodeA(quint64 nodeId)
{
    if (nodeId)
        m_graph->assignInputToDeck(nodeId, true);
    else
        pushDecks();
}

void AppController::selectNodeB(quint64 nodeId)
{
    if (nodeId)
        m_graph->assignInputToDeck(nodeId, false);
    else
        pushDecks();
}

void AppController::togglePlayA() { playDeck(true, !m_compositor->isPlayingA()); }
void AppController::togglePlayB() { playDeck(false, !m_compositor->isPlayingB()); }

void AppController::playDeck(bool deckA, bool play)
{
    if (deckA) {
        if (play) m_compositor->playA(); else m_compositor->pauseA();
        m_deckController->applyAudioControllerToDeck(true, m_deckController->activeNodeA());
    } else {
        if (play) m_compositor->playB(); else m_compositor->pauseB();
        m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB());
    }
}

void AppController::seekDeck(bool deckA, double seconds)
{
    if (deckA)
        m_compositor->seekA(seconds);
    else
        m_compositor->seekB(seconds);
}

void AppController::seekDeckFraction(bool deckA, double fraction)
{
    const double dur = deckA ? m_compositor->getDurationA() : m_compositor->getDurationB();
    if (dur <= 0)
        return;
    const double t = qBound(0.0, fraction, 1.0) * dur;
    seekDeck(deckA, t);
    const NodeId id = deckA ? m_deckController->activeNodeA() : m_deckController->activeNodeB();
    if (id)
        m_deckController->updateDeckAudio(deckA, id, t, true);
}

void AppController::setDeckSpeedValue(bool deckA, double speed)
{
    const double clamped = qBound(0.25, speed, 4.0);
    m_deckController->setDeckSpeed(deckA, clamped);
    emit decksChanged();
}

void AppController::cut() { m_transitionCtrl->cutTransition(); }
void AppController::autoTransition() { m_transitionCtrl->autoTransition(); }

void AppController::clearGraph()
{
    emit confirmRequested(QStringLiteral("clearAll"), tr("Clear All"),
                          tr("Remove all clips and nodes from the grid? This cannot be undone."));
}

void AppController::onTimerTick()
{
    if (m_shuttingDown)
        return;

    if (const quint64 fallbacks = VideoDecoder::hardwareFallbackCount(); fallbacks != m_seenHwFallbacks) {
        m_seenHwFallbacks = fallbacks;
        emit statusMessage(tr("Hardware decoding failed for a clip, playing it in software: %1")
                               .arg(VideoDecoder::lastHardwareFailure()), 10000);
    }

    for (int i = 0; i < 2; ++i) {
        const bool deckA = i == 0;
        const double dur = deckA ? m_compositor->getDurationA() : m_compositor->getDurationB();
        const double t = deckA ? m_compositor->getCurrentTimeA() : m_compositor->getCurrentTimeB();
        if (dur > 0) {
            const double last = deckA ? m_deckController->lastTimeA() : m_deckController->lastTimeB();
            if (MainWindowUtils::isBackwardJump(t, last)) {
                const NodeId id = deckA ? m_deckController->activeNodeA() : m_deckController->activeNodeB();
                if (id)
                    m_deckController->updateDeckAudio(deckA, id, t, true);
            }
            if (deckA) m_deckController->setLastTimeA(t); else m_deckController->setLastTimeB(t);
        }
    }

    m_deckController->refreshShaderAudioForActiveDecks();
    m_outputHub->setActiveDeckNodes(m_deckController->activeNodeA(), m_deckController->activeNodeB());
    emit deckClockChanged();
    if (m_outputHub->isRecording())
        emit recordingProgress(m_outputHub->longestActiveRecordingMs());
}

// ── Graph -> compositor ───────────────────────────────────────────────────────

void AppController::markDirty()
{
    if (m_dirty)
        return;
    m_dirty = true;
    emit sessionDirtyChanged();
}

void AppController::onGraphChanged()
{
    if (m_shuttingDown)
        return;
    markDirty();
    m_pushTimer->start();
    if (m_deckController->activeNodeA())
        m_deckController->applyAudioControllerToDeck(true, m_deckController->activeNodeA(), false);
    if (m_deckController->activeNodeB())
        m_deckController->applyAudioControllerToDeck(false, m_deckController->activeNodeB(), false);
    m_deckController->refreshShaderAudioForActiveDecks();
    m_deckController->refreshTextDataForActiveDecks();
    m_deckController->syncMasterAudioInputs();
    m_deckController->refreshAudioEffects();
}

void AppController::onNodeChanged(NodeId id)
{
    if (m_shuttingDown || !id)
        return;
    const bool isA = id == m_deckController->activeNodeA();
    const bool isB = id == m_deckController->activeNodeB();
    if (!isA && !isB)
        return;
    const bool repeat = m_graph->clipRepeat(id);
    const ClipSettings s = m_graph->clipSettings(id);
    if (isA) {
        m_compositor->setRepeatA(repeat);
        m_compositor->setTrimPointsA(s.startTime, s.endTime);
    }
    if (isB) {
        m_compositor->setRepeatB(repeat);
        m_compositor->setTrimPointsB(s.startTime, s.endTime);
    }
}

void AppController::pushDecks()
{
    if (m_shuttingDown || m_pushing)
        return;
    m_pushing = true;

    Compositor *out = m_compositor.get();
    const bool single = m_graph->outputIsSingleStream();
    out->setSingleStreamMode(single);
    if (single != m_singleStream) {
        m_singleStream = single;
        emit singleStreamChanged();
    }

    using Layer = ClipGraph::ResolvedLayer;
    using Stream = ClipGraph::ResolvedStream;

    std::function<QString(const Layer &)> layerKey = [&](const Layer &l) -> QString {
        if (l.composite) {
            QString k = QStringLiteral("L%1:%2x%3{")
                .arg(l.layerNodeId).arg(l.composite->canvasWidth).arg(l.composite->canvasHeight);
            for (const Layer &sl : l.composite->layers) {
                k += layerKey(sl)
                   + QStringLiteral("@%1,%2,%3,%4,%5;")
                       .arg(sl.baseX).arg(sl.baseY).arg(sl.baseW).arg(sl.baseH)
                       .arg(sl.visible ? 1 : 0);
            }
            return k + QStringLiteral("}") + ProcessEffects::sourceEffectsKey(l.sourceEffects);
        }
        if (!m_graph->hasNode(l.inputNodeId))
            return QStringLiteral("%1:?").arg(l.inputNodeId);
        const SourceDescriptor d = m_graph->sourceDescriptor(l.inputNodeId);
        const ClipSettings cs = m_graph->clipSettings(l.inputNodeId);
        return QStringLiteral("%1:%2:%3:%4:%5:%6:%7:%8")
            .arg(l.inputNodeId).arg(int(d.kind)).arg(d.path)
            .arg(cs.startTime).arg(cs.endTime).arg(d.captureId)
            .arg(ProcessEffects::sourceEffectsKey(l.sourceEffects))
            .arg(qHash(d.contentKey()));
    };
    auto keysOf = [&](const Stream &s, QString &base, QStringList &ov) {
        base.clear(); ov.clear();
        if (s.layers.isEmpty()) return;
        base = layerKey(s.layers.first());
        for (int i = 1; i < s.layers.size(); ++i) ov << layerKey(s.layers[i]);
    };
    auto sameStream = [](const QString &b1, const QStringList &o1, const QString &b2, const QStringList &o2) {
        return b1 == b2 && o1 == o2;
    };
    auto isPermutation = [](QStringList a, QStringList b) {
        a.sort(); b.sort(); return a == b;
    };

    const NodeId prodA = single ? m_graph->outputSingleProducer() : m_graph->deckAInput();
    const NodeId prodB = single ? 0 : m_graph->deckBInput();
    Stream streamA = prodA ? m_graph->evaluateVideoInput(prodA) : Stream{};
    Stream streamB = prodB ? m_graph->evaluateVideoInput(prodB) : Stream{};

    QString dBaseA, dBaseB;
    QStringList dOvA, dOvB;
    keysOf(streamA, dBaseA, dOvA);
    keysOf(streamB, dBaseB, dOvB);

    const bool aWantsCurB = !dBaseA.isEmpty() && sameStream(dBaseA, dOvA, m_deckBase[1], m_deckOverlays[1]);
    const bool bWantsCurA = !dBaseB.isEmpty() && sameStream(dBaseB, dOvB, m_deckBase[0], m_deckOverlays[0]);
    const bool aUnchanged = sameStream(dBaseA, dOvA, m_deckBase[0], m_deckOverlays[0]);
    const bool bUnchanged = sameStream(dBaseB, dOvB, m_deckBase[1], m_deckOverlays[1]);
    if (!(aUnchanged && bUnchanged) && (aWantsCurB || bWantsCurA)) {
        out->swapDeckContents();
        m_deckController->swapDeckAudio();
        std::swap(m_deckBase[0], m_deckBase[1]);
        std::swap(m_deckOverlays[0], m_deckOverlays[1]);
        std::swap(m_deckUi[0], m_deckUi[1]);
    }

    auto clearDeck = [&](bool deckA) {
        if (deckA) { out->clearDeckA(); m_deckController->setActiveNodeA(0); }
        else       { out->clearDeckB(); m_deckController->setActiveNodeB(0); }
    };

    auto pushDeck = [&](bool deckA, const Stream &stream, const QString &dBase, const QStringList &dOv) {
        const int di = deckA ? 0 : 1;
        QString &curBase = m_deckBase[di];
        QStringList &curOv = m_deckOverlays[di];
        DeckUi &ui = m_deckUi[di];

        if (stream.layers.isEmpty()) {
            clearDeck(deckA);
            m_deckController->stopDeckAudio(deckA);
            ui = DeckUi{};
            curBase.clear(); curOv.clear();
            return;
        }
        const Layer base = stream.layers.first();

        auto applyBaseFor = [&](const Layer &b, bool composite) {
            const float cx = composite ? 0.f : b.cropX, cy = composite ? 0.f : b.cropY;
            const float cw = composite ? 1.f : b.cropW, ch = composite ? 1.f : b.cropH;
            const bool fh = composite ? false : b.flipH, fv = composite ? false : b.flipV;
            if (deckA) {
                out->setBaseA(b.baseX, b.baseY, b.baseW, b.baseH);
                out->setCropA(cx, cy, cw, ch);
                out->setFlipA(fh, fv);
                out->setCanvasSizeA(stream.canvasWidth, stream.canvasHeight);
            } else {
                out->setBaseB(b.baseX, b.baseY, b.baseW, b.baseH);
                out->setCropB(cx, cy, cw, ch);
                out->setFlipB(fh, fv);
                out->setCanvasSizeB(stream.canvasWidth, stream.canvasHeight);
            }
        };

        if (base.composite) {
            const NodeId primaryId = base.composite->layers.isEmpty()
                ? 0 : base.composite->layers.first().inputNodeId;
            if (primaryId && m_graph->hasNode(primaryId)) {
                const SourceDescriptor pd = m_graph->sourceDescriptor(primaryId);
                const auto st = DeckController::deckUiState(pd, m_graph->sourceName(primaryId));
                ui.name = st.name; ui.hasTransport = st.hasTransport; ui.hasSpeed = st.hasSpeed;
            } else {
                ui = DeckUi{};
            }
            if (sameStream(dBase, dOv, curBase, curOv)) {
                applyBaseFor(base, true);
            } else {
                auto src = SourceFactory::buildLayerSource(base, m_graph, m_runtime);
                if (!src) {
                    clearDeck(deckA);
                    curBase.clear(); curOv.clear();
                    return;
                }
                m_deckController->assignCompositeToDeck(std::move(src), primaryId, deckA);
                applyBaseFor(base, true);
                if (deckA) out->setNodeChainA({});
                else       out->setNodeChainB({});
                curBase = dBase; curOv = dOv;
            }
            return;
        }

        if (!m_graph->hasNode(base.inputNodeId))
            return;
        const SourceDescriptor desc = m_graph->sourceDescriptor(base.inputNodeId);
        const QString name = m_graph->sourceName(base.inputNodeId);
        const auto st = DeckController::deckUiState(desc, name);
        ui.name = st.name; ui.hasTransport = st.hasTransport; ui.hasSpeed = st.hasSpeed;

        auto applyOverlayPlacements = [&]() {
            for (int i = 1; i < stream.layers.size(); ++i) {
                const Layer &l = stream.layers[i];
                out->setChainPlacement(deckA, i - 1, l.cropX, l.cropY, l.cropW, l.cropH,
                                       l.flipH, l.flipV, l.baseX, l.baseY, l.baseW, l.baseH, l.visible);
            }
        };

        if (sameStream(dBase, dOv, curBase, curOv)) {
            applyBaseFor(base, false);
            applyOverlayPlacements();
        } else if (dBase == curBase && !curBase.isEmpty() && isPermutation(dOv, curOv)) {
            std::vector<int> perm(dOv.size(), 0);
            QVector<bool> used(curOv.size(), false);
            for (int i = 0; i < dOv.size(); ++i)
                for (int j = 0; j < curOv.size(); ++j)
                    if (!used[j] && curOv[j] == dOv[i]) { perm[i] = j; used[j] = true; break; }
            out->reorderChain(deckA, perm);
            applyBaseFor(base, false);
            applyOverlayPlacements();
            curOv = dOv;
        } else {
            m_deckController->assignNodeToDeck(base.inputNodeId, deckA, base.sourceEffects);
            applyBaseFor(base, false);
            Stream overlay;
            overlay.canvasWidth = stream.canvasWidth;
            overlay.canvasHeight = stream.canvasHeight;
            for (int i = 1; i < stream.layers.size(); ++i)
                overlay.layers.push_back(stream.layers[i]);
            auto chain = SourceFactory::buildStream(overlay, m_graph, m_runtime);
            if (deckA) out->setNodeChainA(std::move(chain));
            else       out->setNodeChainB(std::move(chain));
            m_obsIntegration->onClipTriggered(desc);
            curBase = dBase; curOv = dOv;
        }
    };

    pushDeck(true, streamA, dBaseA, dOvA);
    pushDeck(false, streamB, dBaseB, dOvB);

    m_outputHub->setActiveDeckNodes(m_deckController->activeNodeA(), m_deckController->activeNodeB());
    m_pushing = false;
    emit decksChanged();
}

// ── Sources / media ───────────────────────────────────────────────────────────

NodeId AppController::addSourceFromDescriptor(const SourceDescriptor &desc, const QImage &thumb)
{
    const NodeId id = m_runtime->addSourceNode(desc, thumb);
    if (id && (desc.kind == SourceDescriptor::Kind::VideoFile || desc.kind == SourceDescriptor::Kind::Image)
        && !desc.path.isEmpty() && QFileInfo(desc.path).isFile()) {
        m_clipManager.addFiles({desc.path});
    }
    mcpBumpRevision();
    return id;
}

void AppController::importFiles(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    m_clipManager.addFiles(paths);
    for (const QString &path : paths) {
        if (ClipManager::isMediaPath(path))
            m_runtime->addClipNode(path);
    }
    mcpBumpRevision();
}

void AppController::importFolder(const QString &path)
{
    if (path.isEmpty())
        return;
    m_clipManager.addFolder(path);
}

void AppController::requestAddFiles()
{
    emit filePickRequested(QStringLiteral("addFiles"), tr("Add Media Files"), QStringLiteral("openMany"),
                           {tr("Media Files (%1)").arg(MediaFormats::globPattern(true, true, true))}, QString());
}

void AppController::requestAddFolder()
{
    emit filePickRequested(QStringLiteral("addFolder"), tr("Add Media Folder"), QStringLiteral("folder"),
                           {}, QString());
}

quint64 AppController::addVideoUrl(const QString &url, const QString &displayName)
{
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return 0;
    QString name = displayName.trimmed();
    if (name.isEmpty()) {
        name = QUrl(trimmed).fileName();
        if (name.isEmpty())
            name = tr("Network Video");
    }
    if (!hasStream(MediaProbe::probe(trimmed), StreamInfo::Type::Video)) {
        emit errorOccurred(tr("Could not open or play the video from URL:\n%1\n\n"
                              "Please check the URL and ensure the video stream is accessible and valid.")
                               .arg(trimmed));
        return 0;
    }
    const NodeId id = m_runtime->addClipNode(trimmed, ThumbHelper::makeIconThumb(ThumbHelper::Names::Link));
    if (id)
        renameNode(id, name);
    mcpBumpRevision();
    return id;
}

quint64 AppController::addSourceFromMap(const QVariantMap &descriptor, const QPointF &pos)
{
    mcpBumpRevision();
    return m_graph->addNode(ClipGraph::NodeType::Input, pos, {{"source", descriptor}});
}

void AppController::removeNode(quint64 nodeId)
{
    if (!m_graph->hasNode(nodeId))
        return;
    m_graph->removeNode(nodeId);
    if (m_deckController->activeNodeA() == nodeId) m_deckController->setActiveNodeA(0);
    if (m_deckController->activeNodeB() == nodeId) m_deckController->setActiveNodeB(0);
    m_pushTimer->stop();
    pushDecks();
    mcpBumpRevision();
}

void AppController::renameNode(quint64 nodeId, const QString &name)
{
    SourceDescriptor desc = m_graph->sourceDescriptor(nodeId);
    desc.displayName = name;
    m_graphController->setSourceDescriptor(nodeId, desc);
    mcpBumpRevision();
}

void AppController::linkNodeObsScene(quint64 nodeId, const QString &sceneName)
{
    SourceDescriptor desc = m_graph->sourceDescriptor(nodeId);
    desc.obsSceneName = sceneName;
    m_graphController->setSourceDescriptor(nodeId, desc);
}

int AppController::nodeThumbnailRevision(quint64 nodeId) const { return m_runtime->thumbnailRevision(nodeId); }

bool AppController::runScript(quint64 nodeId) { return m_runtime->runScript(nodeId); }

bool AppController::pulseTrigger(quint64 nodeId) { return m_runtime->pulseTrigger(nodeId); }

ScriptBinding AppController::scriptBindingFor(NodeId dataNodeId) const
{
    return m_runtime->scriptBindingFor(dataNodeId);
}

bool AppController::mcpUpdateSource(NodeId id, const SourceDescriptor &desc, const QImage &thumb)
{
    if (!m_graph->setSourceDescriptor(id, desc))
        return false;
    if (!thumb.isNull())
        m_runtime->setThumbnail(id, thumb);
    pushDecks();
    mcpBumpRevision();
    return true;
}

bool AppController::mcpRenameClip(NodeId id, const QString &name)
{
    if (!m_graph->hasNode(id))
        return false;
    renameNode(id, name);
    return true;
}

bool AppController::mcpRemoveClip(NodeId id)
{
    if (!m_graph->hasNode(id))
        return false;
    removeNode(id);
    return true;
}

// ── Session ───────────────────────────────────────────────────────────────────

QJsonObject AppController::currentSessionJson(const QString &sessionFilePath) const
{
    return m_sessionManager->buildJson(
        faderValue(), m_transitionCtrl->currentModeIndex(), m_transitionCtrl->currentDurationSecs(),
        m_deckController->activeNodeA(), m_deckController->activeNodeB(),
        m_hotkeyManager->hotkeysJson(),
        sessionFilePath.isEmpty() ? SessionManager::autosavePath() : sessionFilePath);
}

void AppController::performAutosave()
{
    m_sessionManager->saveAutosave(currentSessionJson());
}

void AppController::setSessionPath(const QString &path)
{
    if (m_sessionPath == path)
        return;
    m_sessionPath = path;
    emit sessionPathChanged();
}

void AppController::addRecent(const QString &path)
{
    QSettings s;
    QStringList list = s.value(QStringLiteral("session/recent")).toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecentSessions)
        list.removeLast();
    s.setValue(QStringLiteral("session/recent"), list);
    emit recentSessionsChanged();
}

void AppController::clearRecentSessions()
{
    QSettings().remove(QStringLiteral("session/recent"));
    emit recentSessionsChanged();
}

bool AppController::loadSessionFile(const QString &path)
{
    m_deckController->setActiveNodeA(0);
    m_deckController->setActiveNodeB(0);
    m_deckController->stopDeckAudio(true);
    m_deckController->stopDeckAudio(false);
    return m_sessionManager->loadFromFile(path);
}

void AppController::onSessionLoaded()
{
    m_hotkeyManager->restoreHotkeys(m_sessionManager->restoredHotkeys());
    setFaderValue(m_sessionManager->restoredCrossfader());
    m_transitionCtrl->setTransitionModeIndex(m_sessionManager->restoredTransitionMode());
    m_transitionCtrl->setTransitionDuration(m_sessionManager->restoredTransitionDuration());

    const NodeId savedA = m_sessionManager->restoredActiveNodeA();
    const NodeId savedB = m_sessionManager->restoredActiveNodeB();
    if (savedA) selectNodeA(savedA);
    if (savedB) selectNodeB(savedB);

    m_graph->setOutputCanvasSize(Compositor::programWidth(), Compositor::programHeight());

    m_dirty = false;
    emit sessionDirtyChanged();
    emit programResolutionChanged();
    emit outputScaleModeChanged();
    emit sessionLoaded();
}

void AppController::doNewSession()
{
    m_graph->clear();
    m_deckController->setActiveNodeA(0);
    m_deckController->setActiveNodeB(0);
    m_deckController->stopDeckAudio(true);
    m_deckController->stopDeckAudio(false);
    m_compositor->clearDeckA();
    m_compositor->clearDeckB();
    for (int i = 0; i < 2; ++i) {
        m_deckBase[i].clear();
        m_deckOverlays[i].clear();
        m_deckUi[i] = DeckUi{};
    }
    setSessionPath(QString());
    m_dirty = false;
    emit sessionDirtyChanged();
    emit decksChanged();
}

void AppController::newSession()
{
    if (m_dirty)
        emit confirmRequested(QStringLiteral("newSession"), tr("New Session"),
                              tr("Discard unsaved changes and start a new session?"));
    else
        doNewSession();
}

void AppController::doOpenSession(const QString &path)
{
    if (loadSessionFile(path)) {
        setSessionPath(path);
        addRecent(path);
    }
    m_dirty = false;
    emit sessionDirtyChanged();
}

void AppController::openSession(const QString &path)
{
    if (path.isEmpty())
        return;
    if (m_dirty)
        emit confirmRequested(QStringLiteral("openSession:") + path, tr("Open Session"),
                              tr("Discard unsaved changes and open this session?"));
    else
        doOpenSession(path);
}

void AppController::saveSession()
{
    if (m_sessionPath.isEmpty())
        requestSaveSessionAs();
    else
        saveSessionAs(m_sessionPath);
}

void AppController::saveSessionAs(const QString &pathIn)
{
    const QString path = MainWindowUtils::ensureExtension(pathIn, QString::fromUtf8(SessionManager::kSessionExtension));
    if (!m_sessionManager->writeSessionFile(currentSessionJson(path), path)) {
        emit errorOccurred(tr("Cannot write to file:\n%1").arg(path));
        return;
    }
    setSessionPath(path);
    addRecent(path);
    m_dirty = false;
    emit sessionDirtyChanged();
}

void AppController::requestOpenSession()
{
    emit filePickRequested(QStringLiteral("openSession"), tr("Open Session"), QStringLiteral("open"),
                           {sessionFilter()}, QString());
}

void AppController::requestSaveSessionAs()
{
    emit filePickRequested(QStringLiteral("saveSession"), tr("Save Session"), QStringLiteral("save"),
                           {sessionFilter()}, m_sessionPath);
}

void AppController::requestExportProject()
{
    emit filePickRequested(QStringLiteral("exportProject"), tr("Export Project"), QStringLiteral("save"),
                           {projectFilter()}, QString());
}

void AppController::requestImportProject()
{
    emit filePickRequested(QStringLiteral("importProject"), tr("Import Project"), QStringLiteral("open"),
                           {projectFilter()}, QString());
}

void AppController::exportProject(const QString &packagePathIn)
{
    const QString path = MainWindowUtils::ensureExtension(packagePathIn, QString::fromUtf8(ProjectPackager::kExtension));
    const ProjectPackager::Report report = ProjectPackager::exportPackage(currentSessionJson(), *m_graph, path);
    if (!report.success) {
        emit errorOccurred(report.error.isEmpty() ? tr("Export failed.") : report.error);
        return;
    }
    QString message = tr("Exported %1 asset(s) to:\n%2").arg(report.assetCount).arg(path);
    if (!report.warnings.isEmpty())
        message += tr("\n\nNotes:\n%1").arg(report.warnings.join('\n'));
    emit infoMessage(tr("Export Project"), message);
}

void AppController::importProject(const QString &packagePath, const QString &extractDir)
{
    const ProjectPackager::ImportResult result = ProjectPackager::importPackage(packagePath, extractDir, true);
    if (!result.success) {
        emit errorOccurred(result.error.isEmpty() ? tr("Import failed.") : result.error);
        return;
    }
    doOpenSession(result.sessionPath);
    if (!result.warnings.isEmpty())
        emit infoMessage(tr("Import Project"), tr("Project imported with notes:\n%1").arg(result.warnings.join('\n')));
}

void AppController::recoverFrom(const QString &sessionFile)
{
    if (!sessionFile.isEmpty() && QFile::exists(sessionFile))
        loadSessionFile(sessionFile);
}

bool AppController::mcpSaveSession(const QString &path)
{
    const QString out = MainWindowUtils::ensureExtension(path, QString::fromUtf8(SessionManager::kSessionExtension));
    return m_sessionManager->writeSessionFile(currentSessionJson(out), out);
}

bool AppController::mcpLoadSession(const QString &path)
{
    loadSessionFile(path);
    return true;
}

// ── Dialog answers ────────────────────────────────────────────────────────────

void AppController::requestConfirm(const QString &title, const QString &text, std::function<void(bool)> answer)
{
    const QString id = QStringLiteral("callback:") + QString::number(++m_nextConfirmId);
    m_confirmCallbacks.insert(id, std::move(answer));
    emit confirmRequested(id, title, text);
}

void AppController::confirmAnswered(const QString &id, bool accepted)
{
    if (const auto callback = m_confirmCallbacks.take(id)) {
        callback(accepted);
        return;
    }
    if (!accepted)
        return;
    if (id == QLatin1String("clearAll")) {
        m_graph->clear();
        m_deckController->setActiveNodeA(0);
        m_deckController->setActiveNodeB(0);
        m_deckController->stopDeckAudio(true);
        m_deckController->stopDeckAudio(false);
        m_compositor->clearDeckA();
        m_compositor->clearDeckB();
        for (int i = 0; i < 2; ++i) {
            m_deckBase[i].clear();
            m_deckOverlays[i].clear();
            m_deckUi[i] = DeckUi{};
        }
        emit decksChanged();
    } else if (id == QLatin1String("newSession")) {
        doNewSession();
    } else if (id.startsWith(QLatin1String("openSession:"))) {
        doOpenSession(id.mid(int(strlen("openSession:"))));
    }
}

void AppController::filePicked(const QString &id, const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    const QString first = paths.first();
    if (id == QLatin1String("openSession")) {
        openSession(first);
    } else if (id == QLatin1String("saveSession")) {
        saveSessionAs(first);
    } else if (id == QLatin1String("exportProject")) {
        exportProject(first);
    } else if (id == QLatin1String("importProject")) {
        const QFileInfo info(first);
        emit filePickRequested(QStringLiteral("importExtract:") + first, tr("Extract Project To"),
                               QStringLiteral("folder"), {},
                               info.absolutePath() + QDir::separator() + info.completeBaseName());
    } else if (id.startsWith(QLatin1String("importExtract:"))) {
        importProject(id.mid(int(strlen("importExtract:"))), first);
    } else if (id == QLatin1String("addFiles")) {
        importFiles(paths);
    } else if (id == QLatin1String("addFolder")) {
        importFolder(first);
    } else if (id == QLatin1String("outputDir")) {
        setOutputDir(first);
        const QString follow = m_pendingAfterOutputDir;
        m_pendingAfterOutputDir.clear();
        if (follow == QLatin1String("startRecording"))
            startRecording();
        else if (follow == QLatin1String("recordingSettings"))
            openRecordingSettings();
    }
}

void AppController::refreshOutputsList()
{
    emit outputsChanged();
}

// ── Outputs / recording ───────────────────────────────────────────────────────

void AppController::showOutputWindow()
{
    QQuickWindow *window = nullptr;
    for (const auto &w : m_outputHub->mirrorOutputs()) {
        if (w) { window = w; break; }
    }
    if (!window)
        window = m_outputHub->addMirrorOutput(tr("CutWire Prism - Program Output"));
    if (!window)
        return;
    window->setFlag(Qt::WindowStaysOnTopHint, m_stayOnTop);
    window->show();
    window->raise();
    window->requestActivate();
    pushDecks();
    emit outputsChanged();
}

void AppController::hideOutputWindow()
{
    for (const auto &w : m_outputHub->mirrorOutputs()) {
        if (w)
            w->hide();
    }
    emit outputsChanged();
}

void AppController::addPreviewOutput()
{
    if (QQuickWindow *w = m_outputHub->addMirrorOutput(tr("CutWire Prism - Preview Output"))) {
        w->raise();
        w->requestActivate();
    }
    emit outputsChanged();
}

bool AppController::setOutputEnabled(const QString &id, bool on)
{
    if (id == QLatin1String("window")) {
        if (on) showOutputWindow(); else hideOutputWindow();
        return true;
    }
    if (id == QLatin1String("ndi")) {
        const bool ok = m_outputHub->setNdiOutputEnabled(on);
        if (!ok && on)
            emit errorOccurred(m_outputHub->ndiAvailable()
                                   ? tr("Could not start NDI program output.")
                                   : tr("NDI is not available. Install the NDI SDK and rebuild CutWire Prism."));
        emit outputsChanged();
        return ok;
    }
    if (id == QLatin1String("virtualCamera")) {
        const bool ok = m_outputHub->setVirtualCameraEnabled(on);
        if (!ok && on) {
            const QString dev = m_outputHub->virtualCameraDevicePath();
#ifdef Q_OS_LINUX
            emit errorOccurred(tr("Could not start virtual camera output on %1.\n\n"
                                  "Ensure v4l2loopback is loaded:\n  sudo modprobe v4l2loopback\n\n"
                                  "You may need to specify a device path in settings if the "
                                  "loopback device is not at the default location.")
                                   .arg(dev.isEmpty() ? tr("(unknown device)") : dev));
#else
            emit errorOccurred(tr("Could not start virtual camera output (%1).\n\n"
                                  "Another application may already be using the virtual camera, "
                                  "or softcam.dll may be missing next to prism.exe.")
                                   .arg(dev.isEmpty() ? QStringLiteral("DirectShow Softcam") : dev));
#endif
        }
        emit outputsChanged();
        return ok;
    }
    return false;
}

bool AppController::ensureOutputDir(const QString &followUpId)
{
    if (RecordingOptions::hasChosenOutputDir())
        return true;
    m_pendingAfterOutputDir = followUpId;
    emit filePickRequested(QStringLiteral("outputDir"), tr("Choose a folder to save recordings and captures"),
                           QStringLiteral("folder"), {},
                           QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    return false;
}

bool AppController::startRecording()
{
    if (!ensureOutputDir(QStringLiteral("startRecording")))
        return false;
    m_outputHub->setOutputDir(RecordingOptions::loadSaved().effectiveOutputDir());
    return m_outputHub->startProgramRecording();
}

void AppController::stopRecording()
{
    m_outputHub->stopAllRecording();
}

void AppController::dropMarker(const QString &label)
{
    if (m_outputHub->isRecording() && !label.isEmpty())
        m_outputHub->addRecordingMarker(label);
}

bool AppController::mcpStartRecording(const QString &dir, QString *error)
{
    QString outDir = dir;
    if (outDir.isEmpty()) {
        if (!RecordingOptions::hasChosenOutputDir()) {
            if (error)
                *error = tr("No save location. Pass dir or set it in the recording panel.");
            return false;
        }
        outDir = RecordingOptions::loadSaved().effectiveOutputDir();
    } else {
        RecordingOptions::saveOutputDir(outDir);
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

QVariantList AppController::captureLayers() const
{
    QVariantList list;
    const auto layers = FrameCaptureHelper::enumerateLayers(m_compositor.get(), m_graph, m_deckController);
    for (int i = 0; i < layers.size(); ++i)
        list.append(QVariantMap{{"label", layers[i].label}, {"index", i}});
    return list;
}

QVariantMap AppController::captureFrame(int layerIndex, bool savePng, bool addAsElement, bool holdAsStill)
{
    const auto layers = FrameCaptureHelper::enumerateLayers(m_compositor.get(), m_graph, m_deckController);
    if (layerIndex < 0 || layerIndex >= layers.size())
        return {{"ok", false}, {"error", tr("No layers are available to capture.")}};

    const auto layer = layers.at(layerIndex);
    const QImage frame = FrameCaptureHelper::captureLayer(m_compositor.get(), layer);
    if (frame.isNull())
        return {{"ok", false}, {"error", tr("Could not capture a frame from \"%1\".").arg(layer.label)}};

    QString savedPath;
    if (savePng || addAsElement) {
        if (!ensureOutputDir(QString()))
            return {{"ok", false}, {"error", tr("No save location set.")}};
        savedPath = FrameCaptureHelper::savePng(
            frame, layer.label, RecordingOptions::loadSaved().effectiveOutputDir());
        if (savedPath.isEmpty() && !holdAsStill)
            return {{"ok", false}, {"error", tr("Captured the frame but could not save it to disk.")}};
    }

    bool held = false;
    if (holdAsStill && layer.kind != FrameCaptureHelper::LayerKind::Program) {
        const int chainIndex = layer.kind == FrameCaptureHelper::LayerKind::DeckChain ? layer.chainIndex : -1;
        m_compositor->holdLayerAsStill(layer.deckA, chainIndex, frame);
        held = true;
    }

    if (addAsElement && !savedPath.isEmpty()) {
        SourceDescriptor desc;
        desc.kind = SourceDescriptor::Kind::Image;
        desc.path = savedPath;
        desc.displayName = tr("Capture %1").arg(QFileInfo(savedPath).completeBaseName());
        addSourceFromDescriptor(desc, frame.scaled(110, 65, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    return {{"ok", true}, {"path", savedPath}, {"held", held}, {"error", QString()}};
}

// ── App dialogs (shown by AppDialogs.qml) ─────────────────────────────────────

void AppController::openHotkeyEditor() { emit appDialogRequested(QStringLiteral("hotkeys")); }

void AppController::openRecordingSettings()
{
    if (!ensureOutputDir(QStringLiteral("recordingSettings")))
        return;
    emit appDialogRequested(QStringLiteral("recording"));
}

void AppController::openRemoteServerDialog() { emit appDialogRequested(QStringLiteral("remote")); }

void AppController::openOscSettings() { emit appDialogRequested(QStringLiteral("osc")); }

void AppController::openAgentAccess() { emit appDialogRequested(QStringLiteral("agent")); }

void AppController::connectObs() { emit appDialogRequested(QStringLiteral("obs")); }

void AppController::requestAddSource(const QString &kind)
{
    static const QStringList editorKinds = {QStringLiteral("shader"), QStringLiteral("svgTemplate"),
                                            QStringLiteral("text"), QStringLiteral("shape")};
    if (editorKinds.contains(kind))
        emit sourceEditorRequested(kind);
    else
        emit appDialogRequested(QStringLiteral("addSource:") + kind);
}

QVariantMap AppController::describeSessionFile(const QString &path) const
{
    QVariantMap out{{QStringLiteral("exists"), QFile::exists(path)},
                    {QStringLiteral("name"), QFileInfo(path).fileName()},
                    {QStringLiteral("savedAt"), QString()}};
    QFile file(path);
    QDateTime when;
    if (file.open(QIODevice::ReadOnly)) {
        const QString savedAt = QJsonDocument::fromJson(file.readAll()).object()
                                    .value(QStringLiteral("savedAt")).toString();
        when = QDateTime::fromString(savedAt, Qt::ISODate);
    }
    if (!when.isValid())
        when = QFileInfo(path).lastModified();
    out[QStringLiteral("savedAt")] = when.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    return out;
}

void AppController::applyOscSettings()
{
    if (!m_oscServer->applySettings())
        emit statusMessage(tr("OSC: could not listen on UDP port %1").arg(m_oscServer->port()), 8000);
}

void AppController::switchObsScene(const QString &scene) { m_obsIntegration->switchProgramScene(scene); }
void AppController::refreshObsScenes() { m_obsIntegration->refreshScenes(); }

// ── MCP inspect / capture ─────────────────────────────────────────────────────

QJsonObject AppController::mcpInspect(bool includeClips, bool detail, int sinceRevision, bool includeGraph) const
{
    using namespace prism::mcp;
    if (sinceRevision >= 0 && sinceRevision == m_mcpEditRevision)
        return ok({{QStringLiteral("unchanged"), true}, {QStringLiteral("revision"), m_mcpEditRevision}});

    auto deckJson = [this](bool deckA) {
        const NodeId id = deckA ? activeNodeA() : activeNodeB();
        return QJsonObject{
            {QStringLiteral("clip"), id ? QString::number(id) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("name"), id ? m_graph->sourceName(id) : QString()},
            {QStringLiteral("playing"), deckA ? isPlayingA() : isPlayingB()},
            {QStringLiteral("time"), deckA ? deckTimeA() : deckTimeB()},
            {QStringLiteral("duration"), deckA ? deckDurationA() : deckDurationB()},
            {QStringLiteral("speed"), m_deckController->deckSpeed(deckA)},
        };
    };

    QJsonArray transitionModes;
    for (const QString &name : m_transitionCtrl->transitionModeNames())
        transitionModes.append(name);

    auto *hub = m_outputHub;
    QJsonObject body{
        {QStringLiteral("revision"), m_mcpEditRevision},
        {QStringLiteral("fader"), faderValue()},
        {QStringLiteral("panic"), mcpPanicMode()},
        {QStringLiteral("decks"), QJsonObject{{QStringLiteral("a"), deckJson(true)},
                                               {QStringLiteral("b"), deckJson(false)}}},
        {QStringLiteral("transition"), QJsonObject{
            {QStringLiteral("index"), m_transitionCtrl->currentModeIndex()},
            {QStringLiteral("duration"), m_transitionCtrl->currentDurationSecs()},
            {QStringLiteral("modes"), transitionModes},
        }},
        {QStringLiteral("program"), QJsonObject{{QStringLiteral("w"), Compositor::programWidth()},
                                                 {QStringLiteral("h"), Compositor::programHeight()}}},
        {QStringLiteral("recording"), QJsonObject{
            {QStringLiteral("active"), hub->isRecording()},
            {QStringLiteral("elapsed"), hub->longestActiveRecordingMs() / 1000.0},
            {QStringLiteral("dir"), hub->outputDir()},
        }},
        {QStringLiteral("ndi"), QJsonObject{{QStringLiteral("available"), hub->ndiAvailable()},
                                             {QStringLiteral("enabled"), hub->ndiOutputEnabled()},
                                             {QStringLiteral("name"), hub->ndiStreamName()}}},
        {QStringLiteral("virtualCamera"), QJsonObject{{QStringLiteral("available"), hub->virtualCameraAvailable()},
                                                       {QStringLiteral("enabled"), hub->virtualCameraEnabled()}}},
    };

    const QVector<NodeId> inputs = m_graph->nodeIdsOfType(ClipGraph::NodeType::Input);
    if (includeClips) {
        QJsonArray clips;
        for (NodeId id : inputs) {
            const SourceDescriptor d = m_graph->sourceDescriptor(id);
            QJsonObject row{
                {QStringLiteral("id"), QString::number(id)},
                {QStringLiteral("name"), m_graph->sourceName(id)},
                {QStringLiteral("kind"), mcpKindName(d.kind)},
                {QStringLiteral("activeA"), id == activeNodeA()},
                {QStringLiteral("activeB"), id == activeNodeB()},
            };
            if (detail) {
                if (!d.path.isEmpty())
                    row.insert(QStringLiteral("path"), d.path);
                if (!d.textTemplate.isEmpty())
                    row.insert(QStringLiteral("text"), d.textTemplate);
                if (!d.svgTemplateId.isEmpty())
                    row.insert(QStringLiteral("template"), d.svgTemplateId);
                if (!d.shaderCode.isEmpty())
                    row.insert(QStringLiteral("shader"), true);
                row.insert(QStringLiteral("live"), d.isLiveSource());
                row.insert(QStringLiteral("seekable"), d.isSeekable());
            }
            clips.append(row);
        }
        body.insert(QStringLiteral("clips"), clips);
    } else {
        body.insert(QStringLiteral("clips"), int(inputs.size()));
    }

    if (includeGraph)
        body.insert(QStringLiteral("graph"), m_graph->snapshot());

    return ok(body);
}

QJsonObject AppController::mcpCaptureFrame(bool full)
{
    using namespace prism::mcp;
    QImage frame = m_compositor->captureProgramFrame();
    if (frame.isNull())
        frame = m_compositor->programFrame();
    if (frame.isNull())
        return textResult(err("capture_failed", QStringLiteral("No program frame yet")), true);

    const QJsonObject meta = ok({
        {QStringLiteral("w"), frame.width()},
        {QStringLiteral("h"), frame.height()},
        {QStringLiteral("full"), full},
    });

    if (full) {
        QString baseDir = m_outputHub->outputDir();
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
        jpegFrame = jpegFrame.scaled(qRound(jpegFrame.width() * scale), qRound(jpegFrame.height() * scale),
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
