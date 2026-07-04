#pragma once

#include <QObject>
#include <memory>
#include <unordered_map>
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/canvas/VideoWidget.h"
#include "ui/output/OutputWindow.h"
#include "ui/output/OutputHub.h"
#include "core/media/AudioPlayer.h"
#include "core/media/AudioInputCapture.h"

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
                            OutputHub     *outputHub,
                            QObject       *parent = nullptr);

    // ── Deck assignment ──────────────────────────────────────────────────────
    /// Assign a node to a deck and start playback.
    void assignNodeToDeck(ClipNodeModel *node, NodeId nodeId, bool deckA,
                          QSlider *progressSlider, QPushButton *playBtn,
                          QLabel *selectedLabel, QLabel *timeLabel,
                          bool removeBackground = false);

    /// Assign a ready-made source to whichever deck the crossfader is on.
    void assignSourceToActiveDeck(std::unique_ptr<MediaSource> src,
                                  const QString &name,
                                  QSlider *crossfader);

    // ── Audio ────────────────────────────────────────────────────────────────
    void stopDeckAudio(bool deckA);
    /// Exchange the A/B audio players + node ids (pairs with VideoWidget::swapDeckContents).
    void swapDeckAudio();
    void releaseAllDeckAudio();
    void updateDeckAudio(bool deckA, NodeId clipId, const ClipNodeModel *node,
                         double currentTimeHint = -1.0, bool forceSeek = false);
    void applyAudioControllerToDeck(bool deckA, NodeId clipId, bool forceSeek = true);
    void refreshShaderAudioForActiveDecks();
    void refreshTextDataForActiveDecks();
    void syncMasterAudioInputs();
    void releaseAllMasterAudioInputs();

    // ── UI state ─────────────────────────────────────────────────────────────
    void updateDeckUI(bool deckA, const QString &name, const SourceDescriptor &desc,
                      QSlider *progressSlider, QPushButton *playBtn,
                      QLabel *selectedLabel, QLabel *timeLabel);

    // ── Playback speed (per deck, applied to video pacing and audio) ────────
    void   setDeckSpeed(bool deckA, double speed);
    double deckSpeed(bool deckA) const { return deckA ? m_speedA : m_speedB; }

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
    void wireAudioPlayerTap(bool deckA, NodeId clipId);

    OutputWindow   *m_outputWindow;
    ClipNodeEditor *m_editor;
    OutputHub      *m_outputHub;

    NodeId m_aClipNodeId = 0;
    NodeId m_bClipNodeId = 0;
    double m_lastTimeA   = 0.0;
    double m_lastTimeB   = 0.0;
    double m_speedA      = 1.0;
    double m_speedB      = 1.0;

    std::unique_ptr<AudioPlayer> m_audioPlayerA;
    std::unique_ptr<AudioPlayer> m_audioPlayerB;
    std::unordered_map<NodeId, std::unique_ptr<AudioInputCapture>> m_inputCaptures;
};
