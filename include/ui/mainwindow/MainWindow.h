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

class QPushButton;

/// The application shell. Owns the decks, node editor, asset library, program
/// output and the controllers (deck/transition/session/hotkeys/OBS/output hub),
/// and wires them together. The top-level coordinator rather than a feature unit.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // ── Remote API helpers ────────────────────────────────────────────────────
    void selectNodeA(NodeId nodeId);
    void selectNodeB(NodeId nodeId);
    void togglePlayA();
    void togglePlayB();
    bool isPlayingA() const;
    bool isPlayingB() const;
    NodeId activeNodeA() const;
    NodeId activeNodeB() const;
    ClipNodeEditor* clipNodeEditor() const { return m_clipNodeEditor; }

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

    void onConnectObs();
    void onLinkClipObsScene();
    void onEditHotkeys();
    void onStartRemoteControl();
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

    class QStackedWidget *m_stackWidget       = nullptr;
    class QSplitter      *m_editorSplitter    = nullptr;
    AssetLibrary         *m_assetLibrary      = nullptr;
    ClipNodeEditor       *m_clipNodeEditor    = nullptr;
    QWidget              *m_emptyPlaceholder  = nullptr;

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

    QLabel          *m_recStatusLabel  = nullptr;
    QLabel          *m_recTimeLabel    = nullptr;
    QLabel          *m_recTracksLabel  = nullptr;
    QLabel          *m_recPathLabel    = nullptr;
    QString          m_baseWindowTitle;
    bool             m_shuttingDown = false;

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
    void setupAddElementMenu(QMenu *menu);
    void setupRecordingStatusBar();
    void updateRecordingUi(qint64 elapsedMs = -1);
    void shutdownLivePipeline();
    void onRecordingPanel();
    void showRecordingPanel();

    void buildEmptyPlaceholder();
    void syncPanicButtons(QPushButton *activeBtn);
    void applyPanicFromButtons();
    void clearPanicState();
};
