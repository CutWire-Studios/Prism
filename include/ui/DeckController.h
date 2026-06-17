#pragma once

#include <QObject>
#include <memory>
#include "ui/ClipNodeEditor.h"
#include "ui/VideoWidget.h"
#include "ui/OutputWindow.h"
#include "core/AudioPlayer.h"

class QSlider;
class QPushButton;
class QLabel;

/// Owns A-deck and B-deck state: source assignment, audio players, UI sync.
/// Extracted from MainWindow to give it a single clear responsibility.
class DeckController : public QObject {
    Q_OBJECT
public:
    explicit DeckController(OutputWindow  *outputWindow,
                            ClipNodeEditor *editor,
                            QObject       *parent = nullptr);

    // ── Deck assignment ──────────────────────────────────────────────────────
    /// Assign a node to a deck and start playback.
    void assignNodeToDeck(ClipNodeModel *node, NodeId nodeId, bool deckA,
                          QSlider *progressSlider, QPushButton *playBtn,
                          QLabel *selectedLabel, QLabel *timeLabel);

    /// Assign a ready-made source to whichever deck the crossfader is on.
    void assignSourceToActiveDeck(std::unique_ptr<MediaSource> src,
                                  const QString &name,
                                  QSlider *crossfader);

    // ── Audio ────────────────────────────────────────────────────────────────
    void stopDeckAudio(bool deckA);
    void updateDeckAudio(bool deckA, NodeId clipId, const ClipNodeModel *node,
                         double currentTimeHint = -1.0, bool forceSeek = false);
    void applyAudioControllerToDeck(bool deckA, NodeId clipId);
    void refreshShaderAudioForActiveDecks();

    // ── UI state ─────────────────────────────────────────────────────────────
    void updateDeckUI(bool deckA, const QString &name, bool hasTimeline,
                      QSlider *progressSlider, QPushButton *playBtn,
                      QLabel *selectedLabel, QLabel *timeLabel);

    // ── Active node IDs ──────────────────────────────────────────────────────
    NodeId activeNodeA() const { return m_aClipNodeId; }
    NodeId activeNodeB() const { return m_bClipNodeId; }
    void   setActiveNodeA(NodeId id) { m_aClipNodeId = id; }
    void   setActiveNodeB(NodeId id) { m_bClipNodeId = id; }

    // ── Time tracking (for audio loop-detection) ─────────────────────────────
    double lastTimeA() const { return m_lastTimeA; }
    double lastTimeB() const { return m_lastTimeB; }
    void   setLastTimeA(double t) { m_lastTimeA = t; }
    void   setLastTimeB(double t) { m_lastTimeB = t; }

    static QString formatTimeShort(double secs);

private:
    OutputWindow   *m_outputWindow;
    ClipNodeEditor *m_editor;

    NodeId m_aClipNodeId = 0;
    NodeId m_bClipNodeId = 0;
    double m_lastTimeA   = 0.0;
    double m_lastTimeB   = 0.0;

    std::unique_ptr<AudioPlayer> m_audioPlayerA;
    std::unique_ptr<AudioPlayer> m_audioPlayerB;
};
