#pragma once

#include <QMainWindow>
#include <QVector>
#include <QMap>
#include "ui/ClipNodeEditor.h"
#include "ui/OutputWindow.h"
#include "core/ClipManager.h"
#include "core/SourceDescriptor.h"
#include "core/MediaSource.h"

class QShortcut;
class QComboBox;
class QSlider;
class QPushButton;
class QLabel;

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onLoadFolderClicked();
    void onAddFolderClicked();
    void onAddFilesClicked();
    void onClearAllClicked();
    void onCrossfaderMoved(int value);
    void onADeckPlayClicked();
    void onBDeckPlayClicked();
    void onADeckSpeedChanged(int value);
    void onBDeckSpeedChanged(int value);
    void onTimerUpdate();

    // ── Node editor signals ───────────────────────────────────────────────────
    void onNodeAButtonClicked(NodeId nodeId);
    void onNodeBButtonClicked(NodeId nodeId);
    void onNodeRemoveRequested(NodeId nodeId);

    // ── Add Element handlers ──────────────────────────────────────────────────
    void onAddElementSlideshow();
    void onAddElementCamera();
    void onAddElementScreen();
    void onAddElementWindow();
    void onAddElementCanvas();
    void onAddElementShader();

    // ── Transition mode ───────────────────────────────────────────────────────
    void onTransitionModeChanged(int index);

private:
    Ui::MainWindow  *ui;
    OutputWindow    *outputWindow       = nullptr;

    class QStackedWidget *m_stackWidget = nullptr;
    ClipNodeEditor  *m_clipNodeEditor   = nullptr;
    QWidget        *m_emptyPlaceholder = nullptr;

    bool m_aSliderDragging = false;
    bool m_bSliderDragging = false;

    ClipManager clipManager;
    QTimer     *updateTimer       = nullptr;

    NodeId m_aClipNodeId = 0;
    NodeId m_bClipNodeId = 0;

    // Add an element node to the editor
    void addElementNode(const SourceDescriptor &desc, const QPixmap &thumb);
    void appendClipsToEditor(const QStringList &clipPaths);

    // Assign a clip node to a deck using the clip's SourceDescriptor.
    void assignNodeToDeck(ClipNodeModel *node, NodeId nodeId, bool deckA,
                         VideoWidget *out,
                         QSlider *progressSlider, QPushButton *playBtn,
                         QLabel *selectedLabel, QLabel *timeLabel);

    // Assign a ready-made source to the active deck (based on crossfader).
    void assignSourceToActiveDeck(std::unique_ptr<MediaSource> src,
                                  const QString &name);

    // Update deck UI state after a source is assigned.
    void updateDeckUI(bool deckA, const QString &name, bool hasTimeline);

    void setupConnections();
    void applyTheme();

    // ── Hotkey grid ───────────────────────────────────────────────────────────
    // Auto-assigns keyboard shortcuts (key → Deck A, Shift+key → Deck B) to
    // each node as it is added.  Keys are taken in order from the standard VJ
    // grid row sequence: 1–0, Q–P, A–L, Z–M (36 slots total).
    void assignHotkeyToNode(NodeId nodeId);
    void releaseHotkeyForNode(NodeId nodeId);
    static const QList<Qt::Key> &hotkeySequence();

    struct NodeShortcuts {
        QShortcut *deckA = nullptr;
        QShortcut *deckB = nullptr;
    };
    QMap<NodeId, Qt::Key>       m_nodeHotkeys;
    QMap<Qt::Key,  NodeId>      m_keyToNode;
    QMap<NodeId, NodeShortcuts> m_nodeShortcuts;

    // ── Transition combobox ───────────────────────────────────────────────────
    QComboBox *m_transitionCombo = nullptr;


    static QPixmap makeIconThumb(const QString &glyph, int w = 110, int h = 65);
    static QPixmap makeCanvasThumb(const QString &label,
                                   SourceDescriptor::CanvasFill fill,
                                   const QColor &color = Qt::white,
                                   int w = 110, int h = 65);
    static QPixmap makeShaderThumb(const QString &code, int w = 110, int h = 65);
    static QString formatTimeShort(double secs);
};
