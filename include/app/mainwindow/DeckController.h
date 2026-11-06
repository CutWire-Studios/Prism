#pragma once

#include <QObject>
#include "core/Deck.h"
#include <memory>
#include <unordered_map>
#include "app/graph/GraphRuntime.h"
#include "core/graph/ClipGraph.h"
#include "core/render/Compositor.h"
#include "app/output/OutputHub.h"
#include "core/media/AudioPlayer.h"
#include "core/media/AudioInputCapture.h"
#include "core/media/AudioLoopbackCapture.h"

/// Owns Live-deck and Staging-deck state: source assignment and audio players.
class DeckController : public QObject {
    Q_OBJECT
public:
    explicit DeckController(Compositor *compositor,
                            ClipGraph  *graph,
                            GraphRuntime *runtime,
                            OutputHub  *outputHub,
                            QObject    *parent = nullptr);

    // ── Deck assignment ──────────────────────────────────────────────────────
    /// Assign a node to a deck and start playback. With @p startPlayback false (Staging deck),
    /// file sources load paused at their first frame / trim-in; live and generated sources still run
    /// so the staging preview stays live.
    void assignNodeToDeck(NodeId nodeId, Deck deck,
                          const QVector<SourceEffectRef> &sourceEffects = {},
                          bool startPlayback = true);

    /// Assign a ready-made source to the Live deck.
    void assignSourceToLiveDeck(std::unique_ptr<MediaSource> src, const QString &name);

    /// Assign a pre-built flattened Layer-group (composite) source to a deck. The
    /// composite delegates its timeline to its bottom (primary) clip, so the deck's
    /// scrub/speed/play/audio bind to @p primaryNodeId. When it is 0 or not an Input node
    /// (bottom is not a plain clip), the deck runs LIVE with no scrub.
    void assignCompositeToDeck(std::unique_ptr<MediaSource> src, NodeId primaryNodeId, Deck deck,
                               bool startPlayback = true);

    /// Starts a paused file/audio deck (Staging -> incoming) and refreshes its audio.
    void startDeckPlayback(Deck deck);
    /// Pauses a deck, rewinds it to its trim-in and silences its audio (outgoing deck after a take).
    void parkDeck(Deck deck);

    // ── Audio ────────────────────────────────────────────────────────────────
    void stopDeckAudio(Deck deck);
    /// Exchange the Live/Staging audio players, node ids and per-deck state (pairs with Compositor::swapDeckContents).
    void swapDeckAudio();
    void releaseAllDeckAudio();
    /// Full reconfigure (route, effects, device, playback state); use on graph/deck changes only.
    void updateDeckAudio(Deck deck, NodeId clipId,
                         double currentTimeHint = -1.0, bool forceSeek = false);
    /// Cheap per-frame update: re-applies only the transition gain to the deck's player.
    void updateDeckAudioGain(Deck deck);
    void applyAudioControllerToDeck(Deck deck, NodeId clipId, bool forceSeek = true);
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
    void   setDeckSpeed(Deck deck, double speed);
    double deckSpeed(Deck deck) const { return isLive(deck) ? m_speedLive : m_speedStaging; }

    // ── Active node IDs ──────────────────────────────────────────────────────
    NodeId activeNode(Deck deck) const { return isLive(deck) ? m_liveNodeId : m_stagingNodeId; }
    void   setActiveNode(Deck deck, NodeId id) { (isLive(deck) ? m_liveNodeId : m_stagingNodeId) = id; }

    // ── Time tracking (for audio loop-detection) ─────────────────────────────
    double lastTime(Deck deck) const { return isLive(deck) ? m_lastTimeLive : m_lastTimeStaging; }
    void   setLastTime(Deck deck, double t) { (isLive(deck) ? m_lastTimeLive : m_lastTimeStaging) = t; }

    static QString formatTimeShort(double secs);

private:
    void wireAudioPlayerTap(Deck deck, NodeId clipId);

    Compositor   *m_compositor;
    ClipGraph    *m_graph;
    GraphRuntime *m_runtime;
    OutputHub    *m_outputHub;

    NodeId m_liveNodeId = 0;
    NodeId m_stagingNodeId = 0;
    double m_lastTimeLive   = 0.0;
    double m_lastTimeStaging   = 0.0;
    double m_speedLive      = 1.0;
    double m_speedStaging      = 1.0;

    bool m_audioFollowsFaderLive = false;
    bool m_audioFollowsFaderStaging = false;
    std::unique_ptr<AudioPlayer> m_audioPlayerLive;
    std::unique_ptr<AudioPlayer> m_audioPlayerStaging;
    std::unordered_map<NodeId, std::unique_ptr<AudioInputCapture>> m_inputCaptures;
    std::unordered_map<NodeId, std::unique_ptr<AudioLoopbackCapture>> m_loopbackCaptures;
};
