#pragma once

#include <QMainWindow>
#include <QMenu>
#include <QVector>
#include <QMap>
#include <memory>
#include "ui/ClipNodeEditor.h"
#include "ui/OutputWindow.h"
#include "ui/HotkeyManager.h"
#include "ui/SessionManager.h"
#include <QJsonObject>
#include "ui/TransitionController.h"
#include "ui/DeckController.h"
#include "ui/OutputHub.h"
#include "ui/ObsIntegration.h"
#include "core/ClipManager.h"
#include "core/ProjectPackager.h"
#include "core/SourceDescriptor.h"
#include "core/MediaSource.h"

namespace Ui { class MainWindow; }
class RemoteControlServer;
class RemoteServerDialog;
class RecordingSettingsDialog;

class QPushButton;

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

    // ── Helpers ───────────────────────────────────────────────────────────────
    void setupConnections();
    void setupPreviewSplitters();
    void setupDeckPreviewSplitter(bool deckA);
    void refreshPreviewPixmaps();
    void applyTheme();

    void loadFromFile(const QString &path, bool showErrors);
    void handleStartupRecovery();
    void performAutosave();
    QJsonObject currentSessionJson() const;
    QJsonObject currentSessionJson(const QString &sessionFilePath) const;
    void addElementNode(const SourceDescriptor &desc, const QPixmap &thumb);
    void addSourceOfKind(SourceDescriptor::Kind kind);
    void appendClipsToEditor(const QStringList &clipPaths);
    void setupAddElementMenu(QMenu *menu);
    void setupRecordingStatusBar();
    void updateRecordingUi(qint64 elapsedMs = -1);
    void onRecordingPanel();
    void showRecordingPanel();

    void buildEmptyPlaceholder();
    void syncPanicButtons(QPushButton *activeBtn);
    void applyPanicFromButtons();
    void clearPanicState();
};
