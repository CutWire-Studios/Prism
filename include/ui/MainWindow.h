#pragma once

#include <QMainWindow>
#include <QVector>
#include <QMap>
#include <memory>
#include "ui/ClipNodeEditor.h"
#include "ui/OutputWindow.h"
#include "ui/HotkeyManager.h"
#include "ui/SessionManager.h"
#include "ui/TransitionController.h"
#include "ui/DeckController.h"
#include "ui/OutputHub.h"
#include "ui/ObsIntegration.h"
#include "core/ClipManager.h"
#include "core/SourceDescriptor.h"
#include "core/MediaSource.h"

namespace Ui { class MainWindow; }

class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

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
    void onAddPhotosClicked();
    void onClearAllClicked();

    // ── Node editor signals ───────────────────────────────────────────────────
    void onNodeAButtonClicked(NodeId nodeId);
    void onNodeBButtonClicked(NodeId nodeId);
    void onNodeRemoveRequested(NodeId nodeId);

    // ── Session persistence ───────────────────────────────────────────────────
    void onSaveSessionClicked();
    void onLoadSessionClicked();
    void onSessionLoaded();

    // ── Add Element handlers ──────────────────────────────────────────────────
    void onAddElementSlideshow();
    void onAddElementCamera();
    void onAddElementScreen();
    void onAddElementWindow();
    void onAddElementCanvas();
    void onAddElementShader();
    void onAddElementDynamicInterface();
    void onAddElementNdi();

    void onConnectObs();
    void onLinkClipObsScene();
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

    // ── Extracted controllers ─────────────────────────────────────────────────
    HotkeyManager      *m_hotkeyManager      = nullptr;
    SessionManager     *m_sessionManager     = nullptr;
    TransitionController *m_transitionCtrl   = nullptr;
    DeckController     *m_deckController     = nullptr;
    OutputHub          *m_outputHub          = nullptr;
    ObsIntegration     *m_obsIntegration     = nullptr;
    class QMenu        *m_obsScenesMenu      = nullptr;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void setupConnections();
    void applyTheme();

    void loadFromFile(const QString &path, bool showErrors);
    void addElementNode(const SourceDescriptor &desc, const QPixmap &thumb);
    void appendClipsToEditor(const QStringList &clipPaths);

    void buildEmptyPlaceholder();
    void syncPanicButtons(QPushButton *activeBtn);
    void applyPanicFromButtons();
    void clearPanicState();
};
