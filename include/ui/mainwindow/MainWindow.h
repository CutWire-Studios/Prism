#pragma once

#include <QMainWindow>
#include <QMenu>
#include <QVector>
#include <QMap>
#include <memory>
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/output/OutputWindow.h"
#include "ui/hotkeys/HotkeyManager.h"
#include "ui/session/SessionManager.h"
#include "ui/common/AssetLibrary.h"
#include <QJsonObject>
#include "ui/transitions/TransitionController.h"
#include "ui/mainwindow/DeckController.h"
#include "ui/output/OutputHub.h"
#include "ui/obs/ObsIntegration.h"
#include "core/project/ClipManager.h"
#include "core/project/ProjectPackager.h"
#include "core/sources/SourceDescriptor.h"
#include "core/sources/MediaSource.h"

namespace Ui { class MainWindow; }
class RemoteControlServer;
class RemoteServerDialog;
class RecordingSettingsDialog;
class McpAccessDialog;
class OutputHub;
class DeckController;
class SessionManager;
class QSlider;
class QLabel;
class QPushButton;

namespace prism::mcp { class McpServer; }

/// The application shell. Owns the decks, node editor, asset library, program
/// output and the controllers (deck/transition/session/hotkeys/OBS/output hub),
/// and wires them together. The top-level coordinator rather than a feature unit.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // ── Remote / MCP API helpers ─────────────────────────────────────────────
    void selectNodeA(NodeId nodeId);
    void selectNodeB(NodeId nodeId);
    void togglePlayA();
    void togglePlayB();
    bool isPlayingA() const;
    bool isPlayingB() const;
    NodeId activeNodeA() const;
    NodeId activeNodeB() const;
    ClipNodeEditor* clipNodeEditor() const { return m_clipNodeEditor; }

    OutputWindow *outputWindow() const { return m_outputWindow; }
    OutputHub *outputHub() const { return m_outputHub; }
    TransitionController *transitionController() const { return m_transitionCtrl; }
    DeckController *deckController() const { return m_deckController; }
    SessionManager *sessionManager() const { return m_sessionManager; }

    void playDeck(bool deckA, bool play);
    void seekDeck(bool deckA, double seconds);
    void setDeckSpeedValue(bool deckA, double speed);
    void setFaderValue(int value);
    int faderValue() const;
    QString mcpPanicMode() const;
    bool mcpSetPanic(const QString &mode);
    ClipNodeModel *addSourceFromDescriptor(const SourceDescriptor &desc, const QPixmap &thumb);
    bool mcpUpdateSource(NodeId id, const SourceDescriptor &desc, const QPixmap &thumb);
    bool mcpRenameClip(NodeId id, const QString &name);
    bool mcpRemoveClip(NodeId id);
    bool mcpSaveSession(const QString &path);
    bool mcpLoadSession(const QString &path);
    bool mcpStartRecording(const QString &dir, QString *error);
    QJsonObject mcpInspect(bool includeClips, bool detail, int sinceRevision,
                           bool includeGraph = false) const;
    QJsonObject mcpCaptureFrame(bool full);
    int mcpRevision() const { return m_mcpEditRevision; }
    void mcpBumpRevision();

    bool mcpEnabled() const;
    void setMcpEnabled(bool on);
    prism::mcp::McpServer *mcpServer() const { return m_mcp; }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    // ── Media / folder actions ────────────────────────────────────────────────
    void onLoadFolderClicked();
    void onAddFolderClicked();
    void onAddFilesClicked();
    void onAddVideoUrlClicked();
    void onClearAllClicked();

    // ── Node editor signals ───────────────────────────────────────────────────
    void onNodeAButtonClicked(NodeId nodeId);
    void onNodeBButtonClicked(NodeId nodeId);
    void onNodeRemoveRequested(NodeId nodeId);

    // ── Session persistence ───────────────────────────────────────────────────
    void onSaveSessionClicked();
    void onLoadSessionClicked();
    void onExportProjectClicked();
    void onImportProjectClicked();
    void onSessionLoaded();

    void onFreezeFrameCapture();
    void onSetOutputResolution();
    void onAboutPrism();

    void onConnectObs();
    void onLinkClipObsScene();
    void onEditHotkeys();
    void onStartRemoteControl();
    void onAgentAccess();
    void rebuildObsScenesMenu(const QStringList &scenes);

    // ── Deck controls ─────────────────────────────────────────────────────────
    void onCrossfaderMoved(int value);
    void onADeckPlayClicked();
    void onBDeckPlayClicked();
    void onADeckSpeedChanged(int value);
    void onBDeckSpeedChanged(int value);
    void onTimerUpdate();

    void onPanicBlackoutClicked(bool checked);
    void onPanicPauseClicked(bool checked);
    void onPanicStayTunedClicked(bool checked);

private:
    Ui::MainWindow *ui;
    OutputWindow   *m_outputWindow   = nullptr;

    class QSplitter      *m_editorSplitter    = nullptr;
    AssetLibrary         *m_assetLibrary      = nullptr;
    ClipNodeEditor       *m_clipNodeEditor    = nullptr;

    bool m_aSliderDragging = false;
    bool m_bSliderDragging = false;

    ClipManager clipManager;
    QTimer     *updateTimer = nullptr;
    QTimer     *m_autosaveTimer = nullptr;

    // ── Extracted controllers ─────────────────────────────────────────────────
    HotkeyManager      *m_hotkeyManager      = nullptr;
    SessionManager     *m_sessionManager     = nullptr;
    TransitionController *m_transitionCtrl   = nullptr;
    DeckController     *m_deckController     = nullptr;
    // Source-identity of each deck's currently-loaded stream (base source key +
    // ordered overlay keys). Lets pushDecks() reuse already-decoded sources —
    // updating placement, reordering, or swapping decks — instead of reloading.
    QString      m_deckBaseA,     m_deckBaseB;
    QStringList  m_deckOverlaysA, m_deckOverlaysB;
    OutputHub          *m_outputHub          = nullptr;
    ObsIntegration     *m_obsIntegration     = nullptr;
    QMenu              *m_obsScenesMenu      = nullptr;
    RemoteControlServer *m_remoteServer      = nullptr;
    RemoteServerDialog  *m_serverDialog      = nullptr;
    RecordingSettingsDialog *m_recordingPanel = nullptr;
    prism::mcp::McpServer *m_mcp             = nullptr;
    McpAccessDialog     *m_mcpDialog         = nullptr;
    QLabel              *m_mcpStatusLabel    = nullptr;
    int                  m_mcpEditRevision   = 0;

    QLabel          *m_recStatusLabel  = nullptr;
    QLabel          *m_recTimeLabel    = nullptr;
    QLabel          *m_recTracksLabel  = nullptr;
    QLabel          *m_recPathLabel    = nullptr;
    QString          m_baseWindowTitle;
    bool             m_shuttingDown = false;
    bool             m_outputWindowUserPlaced = false;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void setupConnections();
    void setupPreviewSplitters();
    void setupDeckPreviewSplitter(bool deckA);
    void refreshPreviewPixmaps();
    void rebuildActiveDeckChains();
    void pushDecks();
    void applyTheme();

    void loadFromFile(const QString &path, bool showErrors);
    void handleStartupRecovery();
    void performAutosave();
    QJsonObject currentSessionJson() const;
    QJsonObject currentSessionJson(const QString &sessionFilePath) const;
    void addElementNode(const SourceDescriptor &desc, const QPixmap &thumb);
    void addSourceOfKind(SourceDescriptor::Kind kind);
    void setupAddMenu(QMenu *menu);
    void setupSourceMenu(QMenu *menu);
    void setupRecordingStatusBar();
    void updateRecordingUi(qint64 elapsedMs = -1);
    void shutdownLivePipeline();
    void onRecordingPanel();
    void showRecordingPanel();
    // Ensures the user has picked a folder for recordings/captures, prompting
    // once if not. Returns the chosen folder, or empty if the user cancelled.
    QString ensureOutputDir();
    void showOutputWindow();

    void syncPanicButtons(QPushButton *activeBtn);
    void applyPanicFromButtons();
    void clearPanicState();
};
