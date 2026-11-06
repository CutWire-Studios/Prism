#pragma once

#include <QObject>
#include <memory>
#include <unordered_map>
#include "app/graph/GraphRuntime.h"
#include "core/graph/ClipGraph.h"
#include "core/render/Compositor.h"
#include "app/output/OutputHub.h"
#include "core/media/AudioPlayer.h"
#include "core/media/AudioInputCapture.h"
#include "core/media/AudioLoopbackCapture.h"

/// Owns A-deck and B-deck state: source assignment and audio players.
class DeckController : public QObject {
    Q_OBJECT
public:
    explicit DeckController(Compositor *compositor,
                            ClipGraph  *graph,
                            GraphRuntime *runtime,
                            OutputHub  *outputHub,
                            QObject    *parent = nullptr);

    // ── Deck assignment ──────────────────────────────────────────────────────
    /// Assign a node to a deck and start playback.
    void assignNodeToDeck(NodeId nodeId, bool deckA,
                          const QVector<SourceEffectRef> &sourceEffects = {});

    /// Assign a ready-made source to whichever deck the crossfader is on.
    void assignSourceToActiveDeck(std::unique_ptr<MediaSource> src, const QString &name);

    /// Assign a pre-built flattened Layer-group (composite) source to a deck. The
    /// composite delegates its timeline to its bottom (primary) clip, so the deck's
    /// scrub/speed/play/audio bind to @p primaryNodeId. When it is 0 or not an Input node
    /// (bottom is not a plain clip), the deck runs LIVE with no scrub.
    void assignCompositeToDeck(std::unique_ptr<MediaSource> src, NodeId primaryNodeId, bool deckA);

    // ── Audio ────────────────────────────────────────────────────────────────
    void stopDeckAudio(bool deckA);
    /// Exchange the A/B audio players + node ids (pairs with Compositor::swapDeckContents).
    void swapDeckAudio();
    void releaseAllDeckAudio();
    void updateDeckAudio(bool deckA, NodeId clipId,
                         double currentTimeHint = -1.0, bool forceSeek = false);
    void applyAudioControllerToDeck(bool deckA, NodeId clipId, bool forceSeek = true);
    void refreshShaderAudioForActiveDecks();
    void refreshTextDataForActiveDecks();
    void syncMasterAudioInputs();
    /// Re-resolve effect chains for the playing decks and master inputs without seeking or restarting.
    void refreshAudioEffects();
    void releaseAllMasterAudioInputs();

    // ── UI state ─────────────────────────────────────────────────────────────
    struct DeckUiState {
        QString name;
        bool hasTransport = false;
        bool hasSpeed = false;
    };
    static DeckUiState deckUiState(const SourceDescriptor &desc, const QString &name);

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

    Compositor   *m_compositor;
    ClipGraph    *m_graph;
    GraphRuntime *m_runtime;
    OutputHub    *m_outputHub;

    NodeId m_aClipNodeId = 0;
    NodeId m_bClipNodeId = 0;
    double m_lastTimeA   = 0.0;
    double m_lastTimeB   = 0.0;
    double m_speedA      = 1.0;
    double m_speedB      = 1.0;

    std::unique_ptr<AudioPlayer> m_audioPlayerA;
    std::unique_ptr<AudioPlayer> m_audioPlayerB;
    std::unordered_map<NodeId, std::unique_ptr<AudioInputCapture>> m_inputCaptures;
    std::unordered_map<NodeId, std::unique_ptr<AudioLoopbackCapture>> m_loopbackCaptures;
};
