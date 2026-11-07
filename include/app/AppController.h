#pragma once

#include <QUrl>
#include "core/Deck.h"
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

#include "app/BpmClock.h"
#include "app/show/Preflight.h"
#include "core/graph/ClipGraph.h"
#include "core/graph/ClipGraphModels.h"
#include "core/media/AudioLevelMeter.h"
#include "core/project/ClipManager.h"
#include "core/render/Compositor.h"
#include "core/scripting/ScriptOutput.h"
#include "core/sources/SourceDescriptor.h"

class DeckController;
class GraphRuntime;
class HotkeyManager;
class ObsIntegration;
class OscServer;
class OutputHub;
class QAbstractItemModel;
class ShowManager;
class QJSEngine;
class QQmlEngine;
class QQuickWindow;
class QTimer;
class RemoteControlServer;
class SessionManager;
class TransitionController;
class Scene;
struct VenueProfile;

namespace prism::mcp { class McpServer; }

/// The application's backend coordinator and the single object the QML shell drives. It owns the
/// Compositor, the ShowManager (scenes, each with its graph, QML controller and runtime), DeckController, SessionManager,
/// TransitionController, OutputHub, hotkeys, OBS, and the remote/OSC/MCP servers; it ports the
/// non-widget logic of the old MainWindow (deck routing, transitions, panic, sessions, recording
/// hooks, output windows, MCP/remote helper API).
///
/// Dialogs are never shown from here. Anything that used to open a modal dialog is
/// a signal; QML answers through confirmAnswered() / filePicked().
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // ── Graph / compositor ────────────────────────────────────────────────────
    /// The graph controller of the scene being edited.
    Q_PROPERTY(ClipGraphController *graph READ graphController NOTIFY editedGraphChanged)
    Q_PROPERTY(Compositor *compositor READ compositor CONSTANT)

    // ── Show / scenes ───────────────────────────────────────────────────────────
    Q_PROPERTY(QAbstractItemModel *scenes READ sceneModel CONSTANT)
    /// Project globals {name, value, min, max}; expressions read them as globals.<name>.
    Q_PROPERTY(QAbstractItemModel *globals READ globalsModel CONSTANT)
    Q_PROPERTY(QString liveSceneId READ liveSceneId NOTIFY showChanged)
    /// Node of the edited scene shown in the staging preview instead of the Staging deck (0 = none).
    /// Preview only: never reaches the decks, Take or any output.
    Q_PROPERTY(quint64 previewNodeId READ previewNodeId NOTIFY previewNodeChanged)
    Q_PROPERTY(QString previewNodeName READ previewNodeName NOTIFY previewNodeChanged)
    /// image://nodethumb URL of the latest probe frame; empty until one exists.
    Q_PROPERTY(QString previewImageUrl READ previewImageUrl NOTIFY previewFrameChanged)
    Q_PROPERTY(QString stagingSceneId READ stagingSceneId NOTIFY showChanged)
    Q_PROPERTY(QString editedSceneId READ editedSceneId NOTIFY showChanged)
    /// False after exitScene(): the node editor then asks for a scene to be entered.
    Q_PROPERTY(bool inScene READ inScene NOTIFY showChanged)
    /// The scene the scene list has selected (what the inspector shows when no node is selected);
    /// the edited scene until the list picks one.
    Q_PROPERTY(QString selectedSceneId READ selectedSceneId WRITE setSelectedSceneId NOTIFY selectedSceneChanged)
    Q_PROPERTY(bool canShowUndo READ canShowUndo NOTIFY showUndoChanged)
    Q_PROPERTY(bool canShowRedo READ canShowRedo NOTIFY showUndoChanged)
    /// True while the edited scene is the Live scene (edits go on air).
    Q_PROPERTY(bool editingLive READ editingLive NOTIFY showChanged)

    // ── Decks (clock properties refresh every 100 ms via deckClockChanged) ────
    Q_PROPERTY(quint64 activeNodeLive READ activeNodeLive NOTIFY decksChanged)
    Q_PROPERTY(quint64 activeNodeStaging READ activeNodeStaging NOTIFY decksChanged)
    Q_PROPERTY(QString deckNameLive READ deckNameLive NOTIFY decksChanged)
    Q_PROPERTY(QString deckNameStaging READ deckNameStaging NOTIFY decksChanged)
    /// The deck has a seekable timeline (what the old progress slider and play button
    /// visibility tracked).
    Q_PROPERTY(bool deckHasTransportLive READ deckHasTransportLive NOTIFY decksChanged)
    Q_PROPERTY(bool deckHasTransportStaging READ deckHasTransportStaging NOTIFY decksChanged)
    Q_PROPERTY(bool deckPlayingLive READ isPlayingLive NOTIFY deckClockChanged)
    Q_PROPERTY(bool deckPlayingStaging READ isPlayingStaging NOTIFY deckClockChanged)
    Q_PROPERTY(double deckTimeLive READ deckTimeLive NOTIFY deckClockChanged)
    Q_PROPERTY(double deckTimeStaging READ deckTimeStaging NOTIFY deckClockChanged)
    Q_PROPERTY(double deckDurationLive READ deckDurationLive NOTIFY deckClockChanged)
    Q_PROPERTY(double deckDurationStaging READ deckDurationStaging NOTIFY deckClockChanged)

    /// The deck's source has a decoded first frame.
    Q_PROPERTY(bool deckReadyLive READ deckReadyLive NOTIFY deckClockChanged)
    Q_PROPERTY(bool deckReadyStaging READ deckReadyStaging NOTIFY deckClockChanged)

    // ── Transitions ───────────────────────────────────────────────────────────
    /// 0..100 transition progress from Live toward Staging; reaching 100 swaps the Live and Staging contents and resets to 0.
    Q_PROPERTY(int faderValue READ faderValue WRITE setFaderValue NOTIFY faderValueChanged)
    /// 0 = Live dominates, 1 = Staging dominates (whichever deck currently has the larger alpha).
    Q_PROPERTY(int onAirDeck READ onAirDeck NOTIFY faderValueChanged)
    Q_PROPERTY(int transitionMode READ transitionMode WRITE setTransitionMode NOTIFY transitionSettingsChanged)
    Q_PROPERTY(QStringList transitionModeNames READ transitionModeNames CONSTANT)
    /// Seconds.
    Q_PROPERTY(double transitionDuration READ transitionDuration WRITE setTransitionDuration NOTIFY transitionSettingsChanged)
    /// An AUTO sweep is animating the fader.
    Q_PROPERTY(bool transitionRunning READ transitionRunning NOTIFY transitionRunningChanged)

    // ── Panic ─────────────────────────────────────────────────────────────────
    /// "none" | "blackout" | "freeze" | "stay_tuned".
    Q_PROPERTY(QString panicMode READ panicMode WRITE setPanicMode NOTIFY panicModeChanged)

    // ── Session ───────────────────────────────────────────────────────────────
    /// Empty for an unsaved session.
    Q_PROPERTY(QString sessionPath READ sessionPath NOTIFY sessionPathChanged)
    Q_PROPERTY(bool sessionDirty READ sessionDirty NOTIFY sessionDirtyChanged)
    Q_PROPERTY(QStringList recentSessions READ recentSessions NOTIFY recentSessionsChanged)

    // ── Recording ─────────────────────────────────────────────────────────────
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(qint64 recordingElapsedMs READ recordingElapsedMs NOTIFY recordingProgress)
    Q_PROPERTY(QStringList recordingTracks READ recordingTracks NOTIFY recordingChanged)
    Q_PROPERTY(QString outputDir READ outputDir WRITE setOutputDir NOTIFY outputDirChanged)

    // ── Outputs ───────────────────────────────────────────────────────────────
    /// Rows {id, label, available, enabled, detail}; id is "window", "ndi" or
    /// "virtualCamera". Toggle with setOutputEnabled(id, on).
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY outputsChanged)
    /// Peak level (0..1) per audio node id (as a string), refreshed about 25 times a second;
    /// absent ids are silent.
    Q_PROPERTY(QVariantMap audioLevels READ audioLevels NOTIFY audioLevelsChanged)
    Q_PROPERTY(int programWidth READ programWidth NOTIFY programResolutionChanged)
    Q_PROPERTY(int programHeight READ programHeight NOTIFY programResolutionChanged)
    /// 0 Stretch, 1 Fit, 2 Crop (Compositor::OutputScaleMode).
    Q_PROPERTY(int outputScaleMode READ outputScaleModeInt WRITE setOutputScaleModeInt NOTIFY outputScaleModeChanged)
    Q_PROPERTY(bool outputStayOnTop READ outputStayOnTop WRITE setOutputStayOnTop NOTIFY outputsChanged)

    // ── OBS / MCP ─────────────────────────────────────────────────────────────
    Q_PROPERTY(bool obsAvailable READ obsAvailable CONSTANT)
    Q_PROPERTY(bool obsConnected READ obsConnected NOTIFY obsChanged)
    Q_PROPERTY(QStringList obsScenes READ obsScenes NOTIFY obsChanged)
    Q_PROPERTY(bool mcpEnabled READ mcpEnabled WRITE setMcpEnabled NOTIFY mcpEnabledChanged)
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY bpmChanged)

public:
    // No default argument: a default-constructible type makes QML build its own instance instead of calling create().
    explicit AppController(QObject *parent);
    ~AppController() override;

    /// The process-wide instance (null before construction).
    static AppController *instance();
    /// QML_SINGLETON factory: hands back the existing instance (CppOwnership).
    static AppController *create(QQmlEngine *engine, QJSEngine *jsEngine);

    /// Engine used to instantiate output windows. main.cpp calls this once the engine exists.
    void setEngine(QQmlEngine *engine);
    QQmlEngine *engine() const { return m_engine; }

    // ── Owned objects ─────────────────────────────────────────────────────────
    Compositor *compositor() const { return m_compositor.get(); }
    ShowManager *showManager() const { return m_show; }
    /// Graph, controller and runtime of the scene being edited.
    ClipGraph *clipGraph() const;
    ClipGraphController *graphController() const;
    GraphRuntime *graphRuntime() const;
    DeckController *deckController() const { return m_deckController; }
    SessionManager *sessionManager() const { return m_sessionManager; }
    TransitionController *transitionController() const { return m_transitionCtrl; }
    OutputHub *outputHub() const { return m_outputHub; }
    HotkeyManager *hotkeyManager() const { return m_hotkeyManager; }
    ObsIntegration *obsIntegration() const { return m_obsIntegration; }
    RemoteControlServer *remoteServer() const { return m_remoteServer; }
    OscServer *oscServer() const { return m_oscServer; }
    prism::mcp::McpServer *mcpServer() const { return m_mcp; }
    ClipManager *clipManager() { return &m_clipManager; }
    // ── Media library ──
    void notifyLibraryChanged();

    // ── Property getters ──────────────────────────────────────────────────────
    quint64 activeNodeLive() const;
    quint64 activeNodeStaging() const;
    QString deckNameLive() const { return m_deckUi[0].name; }
    QString deckNameStaging() const { return m_deckUi[1].name; }
    bool deckHasTransportLive() const { return m_deckUi[0].hasTransport; }
    bool deckHasTransportStaging() const { return m_deckUi[1].hasTransport; }
    bool isPlayingLive() const;
    bool isPlayingStaging() const;
    double deckTimeLive() const;
    double deckTimeStaging() const;
    double deckDurationLive() const;
    double deckDurationStaging() const;

    int faderValue() const;
    bool deckReadyLive() const;
    bool deckReadyStaging() const;
    int onAirDeck() const;
    int transitionMode() const;
    QStringList transitionModeNames() const;
    double transitionDuration() const;
    bool transitionRunning() const;
    QAbstractItemModel *sceneModel() const;
    QAbstractItemModel *globalsModel() const;
    QString liveSceneId() const;
    QString stagingSceneId() const;
    QString editedSceneId() const;
    bool inScene() const;
    QString selectedSceneId() const;
    void setSelectedSceneId(const QString &id);
    bool canShowUndo() const;
    bool canShowRedo() const;
    bool editingLive() const;

    QString panicMode() const;

    QString sessionPath() const { return m_sessionPath; }
    bool sessionDirty() const { return m_dirty; }
    QStringList recentSessions() const;

    bool isRecording() const;
    qint64 recordingElapsedMs() const;
    QStringList recordingTracks() const;
    QString outputDir() const;

    QVariantList outputs() const;
    int programWidth() const;
    int programHeight() const;
    int outputScaleModeInt() const;
    bool outputStayOnTop() const { return m_stayOnTop; }

    bool obsAvailable() const;
    bool obsConnected() const;
    QStringList obsScenes() const { return m_obsScenes; }
    bool mcpEnabled() const;

    // ── Setters (property writes) ─────────────────────────────────────────────
    void setFaderValue(int value);
    void setTransitionMode(int index);
    void setTransitionDuration(double secs);
    void setPanicMode(const QString &mode);
    void setOutputDir(const QString &dir);
    void setOutputScaleModeInt(int mode);
    void setOutputStayOnTop(bool on);
    void setMcpEnabled(bool on);

    // ── Decks / transitions (QML) ─────────────────────────────────────────────
    /// Puts a scene on Staging. False for an unknown scene, the Live scene, or while a take is running.
    Q_INVOKABLE bool stageScene(const QString &id);
    /// Stages @p id and starts the take; false when it could not be staged. During a running take it
    /// retargets instead: the on-air blend becomes a still "from" and the transition restarts toward @p id.
    Q_INVOKABLE bool stageAndTake(const QString &id);
    /// Stages the scene @p delta places after (or before, when negative) the Live scene.
    Q_INVOKABLE bool stageRelative(int delta);
    Q_INVOKABLE bool moveScene(const QString &id, int toIndex);
    /// removeScene after a confirmation when the scene is Staging; the Live scene cannot be deleted.
    Q_INVOKABLE void deleteScene(const QString &id);
    Q_INVOKABLE void undoShow();
    Q_INVOKABLE void redoShow();
    /// Display text of the key bound to a scene ("" when unbound).
    Q_INVOKABLE QString sceneHotkey(const QString &id) const;
    /// Binds @p chord (portable text, "" clears); returns an error message or "".
    Q_INVOKABLE QString setSceneHotkey(const QString &id, const QString &chord);
    /// Missing media and devices across all scenes: [{kind, sceneId, scene, source, detail}].
    Q_INVOKABLE QVariantList preflight() const;
    /// Returns the new global's name, or an empty string when @p name is invalid, taken or a built-in.
    Q_INVOKABLE QString addGlobal(const QString &name = {});
    Q_INVOKABLE bool removeGlobal(const QString &name);
    Q_INVOKABLE bool renameGlobal(const QString &name, const QString &newName);
    Q_INVOKABLE bool setGlobalValue(const QString &name, double value);
    Q_INVOKABLE bool setGlobalRange(const QString &name, double min, double max);
    /// Adds a scene after the edited one and returns its id.
    Q_INVOKABLE QString addScene(const QString &name = {});
    Q_INVOKABLE QString duplicateScene(const QString &id);
    Q_INVOKABLE bool removeScene(const QString &id);
    Q_INVOKABLE void enterScene(const QString &id);
    /// Enters the scene @p delta places after (or before) the edited one, wrapping around.
    Q_INVOKABLE void enterSceneRelative(int delta);
    Q_INVOKABLE void exitScene();
    /// {id, name, color, notes, transition:{mode,durationMs}|null, endAction, autoAdvanceMs, advanceOnMediaEnd}.
    Q_INVOKABLE QVariantMap sceneInfo(const QString &id) const;
    /// True when a Scene node in the edited scene pointing at @p targetId would make that scene contain itself.
    Q_INVOKABLE bool sceneWouldCycle(const QString &targetId) const;
    /// Every scene as {id, name, available}; available is false for cyclic targets (the scene picker greys them out).
    Q_INVOKABLE QVariantList sceneChoices() const;
    /// Applies the keys present in @p props (same names as sceneInfo; transition null clears the override).
    Q_INVOKABLE void setSceneProps(const QString &id, const QVariantMap &props);
    /// The @p deck arguments below are int(Deck): 0 = Live, 1 = Staging.
    Q_INVOKABLE void togglePlay(int deck);
    Q_INVOKABLE void playDeck(int deck, bool play);
    Q_INVOKABLE void seekDeck(int deck, double seconds);
    /// 0..1 of the deck's duration; also re-seeks the deck audio (the old progress-slider release).
    Q_INVOKABLE void seekDeckFraction(int deck, double fraction);
    Q_INVOKABLE void cut();
    /// Animates Live -> Staging with the configured transition, then the Staging content becomes Live.
    Q_INVOKABLE void take();
    /// mode: "blackout" | "freeze" | "stay_tuned"; toggles it off when already active.
    Q_INVOKABLE void togglePanic(const QString &mode);
    Q_INVOKABLE void setProgramResolution(int width, int height);
    Q_INVOKABLE void clearGraph();  // asks confirmRequested("clearAll", ...) first

    // ── Sources / media (QML) ─────────────────────────────────────────────────
    /// Adds files to the media library and creates a clip node per media file (MainWindow::onAddFilesClicked).
    /// x/y: graph position of the first node (NaN = the position remembered by requestAddFiles /
    /// requestAddSource, else automatic). The new nodes become the selection.
    Q_INVOKABLE void importFiles(const QStringList &paths, qreal x = qQNaN(), qreal y = qQNaN());
    /// Adds files to the media library only, without creating nodes.
    Q_INVOKABLE void addToLibrary(const QStringList &paths);
    Q_INVOKABLE QString localPath(const QUrl &url) const;
    Q_INVOKABLE void importFolder(const QString &path);
    /// x/y: graph position for the node(s) the picker leads to (NaN = automatic).
    Q_INVOKABLE void requestAddFiles(qreal x = qQNaN(), qreal y = qQNaN());  // filePickRequested("addFiles", ...)
    Q_INVOKABLE void requestAddFolder();   // filePickRequested("addFolder", ...)
    /// Probes the URL first; emits errorOccurred on failure. Returns the new node id or 0.
    Q_INVOKABLE quint64 addVideoUrl(const QString &url, const QString &displayName = {});
    /// descriptor is a SourceDescriptor as a JSON-style map (same keys the .psm "source" object uses).
    Q_INVOKABLE quint64 addSourceFromMap(const QVariantMap &descriptor, const QPointF &pos = QPointF(qQNaN(), qQNaN()));
    Q_INVOKABLE void removeNode(quint64 nodeId);
    Q_INVOKABLE void renameNode(quint64 nodeId, const QString &name);
    /// The physical output device this venue plays the logical audio port on ("" = system default).
    Q_INVOKABLE QString audioDeviceFor(const QString &logical) const;
    Q_INVOKABLE void setAudioDevice(const QString &logical, const QString &physical);
    Q_INVOKABLE void clearAudioDevice(const QString &logical);
    Q_INVOKABLE void linkNodeObsScene(quint64 nodeId, const QString &sceneName);
    double bpm() const { return m_bpmClock.bpm(); }
    void setBpm(double bpm) { m_bpmClock.setBpm(bpm); }
    BpmClock *bpmClock() { return &m_bpmClock; }
    Q_INVOKABLE void tap() { m_bpmClock.tap(); }
    Q_INVOKABLE void nudge(int dir) { m_bpmClock.nudge(dir); }
    /// OSC learn: the next non-/prism/ OSC message within 10 s emits oscLearned(address); otherwise
    /// oscLearnTimedOut(). Needs OSC enabled in the service settings.
    Q_INVOKABLE void startOscLearn();
    Q_INVOKABLE void cancelOscLearn();
    /// Received OSC control addresses as [{address, value}], newest first.
    QVariantList oscControls() const;
    /// Live output of a Modulator node of the edited scene.
    Q_INVOKABLE double modulatorValue(quint64 nodeId) const;
    /// "Run now" on a Script node; false when the node is not a running Script node.
    Q_INVOKABLE bool runScript(quint64 nodeId);
    /// Error of the Script node's last run (empty when it succeeded) and its print/prism.log output.
    Q_INVOKABLE QString scriptError(quint64 nodeId) const;
    Q_INVOKABLE QStringList scriptLog(quint64 nodeId) const;
    /// Error of node @p nodeId's expression on param @p name ("" when fine).
    Q_INVOKABLE QString expressionError(quint64 nodeId, const QString &name) const;
    /// 0 while the node has no thumbnail; otherwise a cache-buster for image://nodethumb/<id>?v=<rev>.
    Q_INVOKABLE int nodeThumbnailRevision(quint64 nodeId) const;
    /// Shows node @p nodeId of the edited scene in the staging preview; picking the same node again clears it.
    Q_INVOKABLE void previewNode(quint64 nodeId);
    Q_INVOKABLE void clearNodePreview();
    quint64 previewNodeId() const { return m_previewNodeId; }
    QString previewNodeName() const;
    QString previewImageUrl() const;
    /// Node ids currently on screen in the node editor (empty when it is hidden); only these get live thumbnails.
    Q_INVOKABLE void setVisibleNodeIds(const QVariantList &ids);
    /// Code, output and run hook of the script feeding a Text/Shader node's data input (empty when unconnected).
    ScriptBinding scriptBindingFor(NodeId dataNodeId) const;

    // ── Session (QML) ─────────────────────────────────────────────────────────
    Q_INVOKABLE void newSession();          // confirms via confirmRequested("newSession", ...) when dirty
    Q_INVOKABLE void openSession(const QString &path);
    Q_INVOKABLE void saveSession();         // saves to sessionPath, or asks filePickRequested("saveSession", ...)
    Q_INVOKABLE void saveSessionAs(const QString &path);
    Q_INVOKABLE void requestOpenSession();  // filePickRequested("openSession", ...)
    Q_INVOKABLE void requestSaveSessionAs();// filePickRequested("saveSession", ...)
    Q_INVOKABLE void requestExportProject();
    Q_INVOKABLE void requestImportProject();
    Q_INVOKABLE void exportProject(const QString &packagePath);
    Q_INVOKABLE void importProject(const QString &packagePath, const QString &extractDir);
    Q_INVOKABLE void recoverFrom(const QString &sessionFile);  // answer to recoveryRequested
    Q_INVOKABLE void clearRecentSessions();

    // ── Outputs / recording (QML) ─────────────────────────────────────────────
    Q_INVOKABLE void showOutputWindow();
    Q_INVOKABLE void hideOutputWindow();
    Q_INVOKABLE void addPreviewOutput();
    Q_INVOKABLE bool setOutputEnabled(const QString &id, bool on);
    Q_INVOKABLE bool startRecording();   // false + filePickRequested("outputDir") when no folder is set
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void dropMarker(const QString &label);
    /// Freeze-frame capture: layers are {label, index}; captureFrame returns {ok, path, held, error}.
    Q_INVOKABLE QVariantList captureLayers() const;
    Q_INVOKABLE QVariantMap captureFrame(int layerIndex, bool savePng, bool addAsElement, bool holdAsStill);

    // ── App-level dialogs: each emits appDialogRequested, AppDialogs.qml shows it ──
    Q_INVOKABLE void openHotkeyEditor();
    Q_INVOKABLE void openPreflight();
    Q_INVOKABLE void openRecordingSettings();
    Q_INVOKABLE void openRemoteServerDialog();
    Q_INVOKABLE void openOscSettings();
    Q_INVOKABLE void openAgentAccess();
    Q_INVOKABLE void connectObs();
    /// kind: "url" | "webrtc" open an app dialog; "slideshow" | "camera" | "screen" | "window" |
    /// "canvas" | "ndi" are added with defaults by AppDialogs; "shader" | "svgTemplate" | "text" | "shape" emit sourceEditorRequested.
    /// x/y: graph position for the new node (NaN = automatic); kept until a source is added.
    /// Forgets the canvas position remembered for a source dialog that was cancelled.
    Q_INVOKABLE void cancelPendingSourcePos();
    Q_INVOKABLE void requestAddSource(const QString &kind, qreal x = qQNaN(), qreal y = qQNaN());
    /// {exists, name, savedAt} of a session / autosave file, for the recovery dialog.
    Q_INVOKABLE QVariantMap describeSessionFile(const QString &path) const;
    Q_INVOKABLE void switchObsScene(const QString &scene);
    Q_INVOKABLE void refreshObsScenes();

    // ── Dialog answers from QML ───────────────────────────────────────────────
    /// id is the one given in confirmRequested ("clearAll", "newSession", "openSession:<path>", ...).
    Q_INVOKABLE void confirmAnswered(const QString &id, bool accepted);
    /// Empty paths = cancelled. id is the one given in filePickRequested.
    Q_INVOKABLE void filePicked(const QString &id, const QStringList &paths);

    /// C++ side of confirmRequested: shows the prompt and calls @p answer with the user's choice
    /// (false on cancel) once QML answers.
    void requestConfirm(const QString &title, const QString &text, std::function<void(bool)> answer);

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    /// Stops servers, timers, recording and the decks. Idempotent. Also autosaves and marks a clean exit
    /// when @p saveAndMarkClean is true (the normal quit path).
    void shutdown(bool saveAndMarkClean = true);
    /// Startup: starts timers/servers, restores the autosave or raises recoveryRequested.
    void start();

    // ── C++ remote / MCP / OSC helper API (kept from MainWindow under the same names) ──
    QJsonObject mcpInspect(bool includeClips, bool detail, int sinceRevision, bool includeGraph = false) const;
    QJsonObject mcpCaptureFrame(bool full);
    QString mcpPanicMode() const { return panicMode(); }
    bool mcpSetPanic(const QString &mode);
    NodeId addSourceFromDescriptor(const SourceDescriptor &desc, const QImage &thumb);
    bool mcpUpdateSource(NodeId id, const SourceDescriptor &desc, const QImage &thumb);
    bool mcpRenameClip(NodeId id, const QString &name);
    bool mcpRemoveClip(NodeId id);
    bool mcpSaveSession(const QString &path);
    bool mcpLoadSession(const QString &path);
    bool mcpStartRecording(const QString &dir, QString *error);
    int mcpRevision() const { return m_mcpEditRevision; }
    void mcpBumpRevision() { ++m_mcpEditRevision; }
    Compositor::OutputScaleMode outputScaleMode() const;
    void setOutputScaleMode(Compositor::OutputScaleMode mode);
    /// Re-reads the osc/* settings and (re)starts or stops the OSC server.
    void applyOscSettings();

    /// Rebuilds the compositor decks from the Live and Staging scenes.
    void pushDecks();
    /// Applies resolution, scaling and outputs; @p persist stores it as this machine's default.
    void applyVenue(const VenueProfile &venue, bool persist);
    const VenueProfile &venue() const;
    QVariantMap audioLevels() const { return m_audioLevels; }
    QJsonObject currentSessionJson(const QString &sessionFilePath = {}) const;

signals:
    /// A Script node's error or log, or a parameter expression error, changed (0 = everything).
    void scriptStatusChanged(quint64 id);
    /// id 0 means all node thumbnails were dropped.
    void nodeThumbnailChanged(quint64 id);
    void previewNodeChanged();
    void previewFrameChanged();
    // Properties
    void decksChanged();
    void deckClockChanged();
    void faderValueChanged();
    void transitionSettingsChanged();
    void transitionRunningChanged();
    void editedGraphChanged();
    void showChanged();
    void showUndoChanged();
    void selectedSceneChanged();
    /// Scene metadata, hotkeys or warnings changed (not graph contents).
    void scenesEdited();
    void panicModeChanged();
    void sessionPathChanged();
    void sessionDirtyChanged();
    void recentSessionsChanged();
    void recordingChanged();
    void recordingProgress(qint64 elapsedMs);
    void outputDirChanged();
    void outputsChanged();
    void programResolutionChanged();
    void audioLevelsChanged();
    void audioDevicesChanged();
    void outputScaleModeChanged();
    void obsChanged();
    void mcpEnabledChanged();
    void bpmChanged();
    void oscLearned(const QString &address);
    void oscLearnTimedOut();

    // Requests for the QML shell
    void confirmRequested(const QString &id, const QString &title, const QString &text);
    /// mode: "open" | "openMany" | "save" | "folder". nameFilters in file dialog syntax
    /// ("Media Files (*.mp4 *.png)").
    void filePickRequested(const QString &id, const QString &title, const QString &mode,
                           const QStringList &nameFilters, const QString &defaultPath);
    void errorOccurred(const QString &text);
    void infoMessage(const QString &title, const QString &text);
    /// Transient status line text (hardware decode fallback, OSC bind failure, ...).
    void statusMessage(const QString &text, int timeoutMs);
    /// Unclean shutdown detected: offer autosave / backups; answer with recoverFrom(path) or ignore to start fresh.
    void recoveryRequested(const QString &autosavePath, const QStringList &backupPaths);
    /// id: "hotkeys" | "preflight" | "recording" | "remote" | "osc" | "agent" | "obs" | "addSource:<kind>".
    void appDialogRequested(const QString &id);
    /// The node editors for these source kinds are not app dialogs; the editor owner handles them.
    void sourceEditorRequested(const QString &kind);
    /// A session finished loading (after the decks, hotkeys, fader were restored).
    void sessionLoaded();

private:
    /// Exchanges the Live/Staging audio players and per-deck UI/stream state (pairs with Compositor::swapDeckContents).
    void swapDeckState();
    /// Stage-and-take of @p id while a take is running: freezes the blend on air as the new "from" and restarts toward @p id.
    bool retargetTake(const QString &id);
    void onTakeCommitted();

    struct DeckUi {
        QString name;
        bool hasTransport = false;
    };

    void connectSignals();
    void onTimerTick();
    void pollAudioLevels();
    void refreshDeckAudio();
    void applyPanic(const QString &mode);
    void onSessionLoaded();
    bool loadSessionFile(const QString &path);
    void markDirty();
    void setSessionPath(const QString &path);
    void addRecent(const QString &path);
    void performAutosave();
    bool ensureOutputDir(const QString &followUpId);
    void doOpenSession(const QString &path);
    void doNewSession();
    void onNodeChanged(Scene *scene, NodeId id);
    void onSceneGraphChanged(Scene *scene);
    void attachScene(Scene *scene);
    void applyTransition(int mode, int durationMs);
    void onLiveEnded();
    void applyLiveEndAction();
    void blackLive();
    void scheduleThumbnail(Deck deck);
    void captureThumbnail(Deck deck, const QString &sceneId);
    QString thumbnailDir() const;
    QList<PreflightIssue> runPreflight(bool deep) const;
    void refreshSceneWarnings();
    void refreshOutputsList();

    static AppController *s_instance;

    QQmlEngine *m_engine = nullptr;
    std::unique_ptr<Compositor> m_compositor;
    ShowManager *m_show = nullptr;
    std::unique_ptr<VenueProfile> m_venue;
    QString m_lastEditedId;
    QPointer<GraphRuntime> m_thumbRuntime;
    QPointer<GraphRuntime> m_previewRuntime;
    quint64 m_previewNodeId = 0;
    DeckController *m_deckController = nullptr;
    SessionManager *m_sessionManager = nullptr;
    TransitionController *m_transitionCtrl = nullptr;
    OutputHub *m_outputHub = nullptr;
    HotkeyManager *m_hotkeyManager = nullptr;
    ObsIntegration *m_obsIntegration = nullptr;
    RemoteControlServer *m_remoteServer = nullptr;
    OscServer *m_oscServer = nullptr;
    prism::mcp::McpServer *m_mcp = nullptr;
    BpmClock m_bpmClock;
    ClipManager m_clipManager;

    QTimer *m_updateTimer = nullptr;
    QTimer *m_meterTimer = nullptr;
    AudioMeterBank m_meterBank;
    QVariantMap m_audioLevels;
    QElapsedTimer m_meterClock;
    QTimer *m_autosaveTimer = nullptr;
    QTimer *m_pushTimer = nullptr;   // coalesces graph changes into one pushDecks()
    QTimer *m_warnTimer = nullptr;   // debounces the per-scene missing media/device scan
    QHash<QString, QString> m_sceneWarnings;
    QString m_selectedSceneId;

    // Source identity of each deck's loaded stream (see pushDecks()).
    QString m_deckBase[2];
    QStringList m_deckOverlays[2];
    QString m_deckBaseParams[2];
    QStringList m_deckOverlayParams[2];
    DeckUi m_deckUi[2];

    QString m_panic = QStringLiteral("none");
    bool m_liveBlack = false;
    bool m_livePlayingPrev = false;
    bool m_applyingTransition = false;
    bool m_shuttingDown = false;
    bool m_started = false;
    bool m_dirty = false;
    bool m_stayOnTop = false;
    bool m_pushing = false;
    bool m_pushDeferred = false;
    quint64 m_seenHwFallbacks = 0;
    double m_lastTime[2] = {0.0, 0.0};
    QString m_sessionPath;
    QStringList m_obsScenes;
    QString m_pendingAfterOutputDir;
    QPointF m_pendingSourcePos{qQNaN(), qQNaN()};
    int m_mcpEditRevision = 0;
    QHash<QString, std::function<void(bool)>> m_confirmCallbacks;
    int m_nextConfirmId = 0;
};
