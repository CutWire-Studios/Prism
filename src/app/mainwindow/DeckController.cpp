#include "app/mainwindow/DeckController.h"
#include "app/mainwindow/SourceFactory.h"
#include "core/sources/SlideshowSource.h"
#include "core/sources/CameraSource.h"
#include "core/sources/ScreenSource.h"
#include "core/sources/CanvasSource.h"
#include "core/sources/ShaderSource.h"
#include "core/sources/SvgTemplateSource.h"
#include "core/sources/TextSource.h"
#include "core/graph/ProcessEffects.h"
#include "core/media/AudioInputCapture.h"
#include "core/media/AudioInputMixRegistry.h"
#include <QMediaDevices>
#include <QCameraDevice>
#include <QSet>
#include <algorithm>

DeckController::DeckController(Compositor *compositor,
                               OutputHub  *outputHub,
                               QObject    *parent)
    : QObject(parent)
    , m_compositor(compositor)
    , m_outputHub(outputHub)
{
}

void DeckController::setDeckGraph(Deck deck, ClipGraph *graph, GraphRuntime *runtime) {
    m_deckGraph[int(deck)] = graph;
    m_deckRuntime[int(deck)] = runtime;
}

QString DeckController::physicalDevice(const QString &logical) const {
    return m_deviceResolver ? m_deviceResolver(logical) : logical;
}

// ── Static helpers ────────────────────────────────────────────────────────────

QString DeckController::formatTimeShort(double secs) {
    if (secs < 0) secs = 0;
    int m = (int)secs / 60, s = (int)secs % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

// ── Audio ─────────────────────────────────────────────────────────────────────

void DeckController::wireAudioPlayerTap(Deck deck, NodeId clipId) {
    auto &player = isLive(deck) ? m_audioPlayerLive : m_audioPlayerStaging;
    if (!player) return;

    const int deckIndex = static_cast<int>(deck);
    player->setIsoPcmTap([this, deckIndex, clipId](const QByteArray &pcm) {
        if (m_outputHub)
            m_outputHub->submitDeckAudioChunk(deckIndex, clipId, pcm);
    });
    player->setPcmTap([this, deckIndex](const QByteArray &pcm) {
        if (m_outputHub)
            m_outputHub->submitProgramAudioChunk(deckIndex, pcm);
    });
}

void DeckController::stopDeckAudio(Deck deck) {
    auto &player = isLive(deck) ? m_audioPlayerLive : m_audioPlayerStaging;
    if (player) player->stop();
}

void DeckController::releaseAllDeckAudio() {
    m_audioPlayerLive.reset();
    m_audioPlayerStaging.reset();
}

void DeckController::swapDeckAudio() {
    std::swap(m_audioPlayerLive, m_audioPlayerStaging);
    std::swap(m_audioFollowsFaderLive, m_audioFollowsFaderStaging);
    std::swap(m_liveNodeId, m_stagingNodeId);
    std::swap(m_lastTimeLive, m_lastTimeStaging);
    std::swap(m_deckGraph[0], m_deckGraph[1]);
    std::swap(m_deckRuntime[0], m_deckRuntime[1]);
}

void DeckController::applyClipSpeed(Deck deck) {
    const NodeId nodeId = isLive(deck) ? m_liveNodeId : m_stagingNodeId;
    if (!graphOf(deck)) return;
    m_compositor->setDeckSpeed(deck, graphOf(deck)->clipSettings(nodeId).speed);
    // Re-seek the audio to the video position so both resume in sync at the
    // new rate.
    if (nodeId)
        applyAudioControllerToDeck(deck, nodeId, true);
}

void DeckController::releaseAllMasterAudioInputs() {
    m_inputCaptures.clear();
    m_loopbackCaptures.clear();
    AudioInputMixRegistry::clearAll();
}

namespace {

/// A mic or loopback node of a cue that is on a deck, with everything its capture needs.
struct CaptureSource {
    ClipGraph *graph = nullptr;
    GraphRuntime *runtime = nullptr;
    NodeId id = 0;
    bool live = false;
    bool routed = false;
    ClipGraph::MasterAudioSettings settings;
    ClipGraph::ResolvedAudioRoute route;
    QString outputDeviceId;
};

}

void DeckController::syncMasterAudioInputs() {
    // One capture per physical device however many cues use it; the Live cue's gain and effects
    // win, and a device only the Staging cue uses stays muted until the take.
    QMap<QString, QVector<CaptureSource>> inputs, captures;
    for (const Deck deck : {Deck::Live, Deck::Staging}) {
        ClipGraph *g = graphOf(deck);
        if (!g || (!isLive(deck) && g == graphOf(Deck::Live)))
            continue;
        for (const bool loopback : {false, true}) {
            const QVector<NodeId> ids = loopback ? g->allMasterAudioCaptureNodeIds()
                                                 : g->allMasterAudioInputNodeIds();
            for (NodeId id : ids) {
                CaptureSource src;
                src.graph = g;
                src.runtime = m_deckRuntime[int(deck)];
                src.id = id;
                src.live = isLive(deck);
                if (!(loopback ? g->masterAudioCaptureSettings(id, src.settings)
                               : g->masterAudioInputSettings(id, src.settings)))
                    continue;

                src.routed = g->resolveAudioStreamRoute(id, src.route);
                if (!src.routed && !g->hasAudioScriptConsumer(id))
                    continue;
                if (src.routed) {
                    QString device;
                    const bool found = src.settings.routedOutputPortIndex >= 0
                        ? g->masterAudioOutputDeviceForPort(src.settings.routedMasterOutputId,
                                                            src.settings.routedOutputPortIndex, device)
                        : g->masterAudioOutputDevice(src.settings.routedMasterOutputId, device);
                    if (!found)
                        continue;
                    src.outputDeviceId = physicalDevice(device);
                }
                (loopback ? captures : inputs)[physicalDevice(src.settings.deviceId)].append(src);
            }
        }
    }

    auto driverOf = [](const QVector<CaptureSource> &group) -> const CaptureSource & {
        for (const CaptureSource &src : group) {
            if (src.live)
                return src;
        }
        return group.first();
    };

    for (auto it = inputs.cbegin(); it != inputs.cend(); ++it) {
        const CaptureSource &driver = driverOf(it.value());
        auto &entry = m_inputCaptures[it.key()];
        if (!entry.capture)
            entry.capture = std::make_unique<AudioInputCapture>(this);
        AudioInputCapture *capture = entry.capture.get();

        const bool needsRestart = !capture->isRunning()
            || capture->inputDeviceId() != it.key()
            || capture->targetOutputDeviceId() != driver.outputDeviceId;

        capture->setInputDeviceId(it.key());
        capture->setTargetOutputDeviceId(driver.outputDeviceId);
        capture->setVolumePercent(driver.settings.volume);
        capture->setMuted(driver.settings.muted || !driver.live);
        capture->setEffectChain(driver.routed ? driver.route.effects : QVector<AudioEffectRef>{});
        entry.consumers.clear();
        for (const CaptureSource &src : it.value())
            entry.consumers.append({src.runtime, src.id});
        const QString key = it.key();
        capture->setProgramRecordingTap([this, key](const QByteArray &pcm) {
            if (m_outputHub)
                m_outputHub->submitMicProgramAudioChunk(pcm);
            for (const auto &[runtime, id] : m_inputCaptures[key].consumers)
                runtime->feedLiveAudioForProducer(id, pcm);
        });

        if (needsRestart)
            capture->start();
    }

    for (auto it = captures.cbegin(); it != captures.cend(); ++it) {
        const CaptureSource &driver = driverOf(it.value());
        auto &entry = m_loopbackCaptures[it.key()];
        if (!entry.capture)
            entry.capture = std::make_unique<AudioLoopbackCapture>(this);
        AudioLoopbackCapture *capture = entry.capture.get();

        const bool needsRestart = !capture->isRunning()
            || capture->playbackDeviceId() != it.key()
            || capture->targetOutputDeviceId() != driver.outputDeviceId;

        capture->setPlaybackDeviceId(it.key());
        capture->setTargetOutputDeviceId(driver.outputDeviceId);
        capture->setVolumePercent(driver.settings.volume);
        capture->setMuted(driver.settings.muted || !driver.live);
        capture->setEffectChain(driver.routed ? driver.route.effects : QVector<AudioEffectRef>{});
        entry.consumers.clear();
        for (const CaptureSource &src : it.value())
            entry.consumers.append({src.runtime, src.id});
        const QString key = it.key();
        capture->setProgramRecordingTap([this](const QByteArray &pcm) {
            if (m_outputHub)
                m_outputHub->submitMicProgramAudioChunk(pcm);
        });
        capture->setAnalysisTap([this, key](const QByteArray &pcm) {
            for (const auto &[runtime, id] : m_loopbackCaptures[key].consumers)
                runtime->feedLiveAudioForProducer(id, pcm);
        });

        if (needsRestart)
            capture->start();
    }

    for (auto it = m_inputCaptures.begin(); it != m_inputCaptures.end(); ) {
        if (!inputs.contains(it->first)) {
            it->second.capture->stop();
            it = m_inputCaptures.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_loopbackCaptures.begin(); it != m_loopbackCaptures.end(); ) {
        if (!captures.contains(it->first)) {
            it->second.capture->stop();
            it = m_loopbackCaptures.erase(it);
        } else {
            ++it;
        }
    }
}

void DeckController::refreshAudioEffects() {
    const auto refreshDeck = [this](Deck deck, NodeId clipId) {
        auto &player = isLive(deck) ? m_audioPlayerLive : m_audioPlayerStaging;
        ClipGraph *g = graphOf(deck);
        if (!clipId || !player || !g) return;
        ClipGraph::ResolvedAudioRoute route;
        if (g->resolveAudioStreamRoute(clipId, route))
            player->setEffectChain(route.effects);
        else
            player->setEffectChain({});
    };
    refreshDeck(Deck::Live, m_liveNodeId);
    refreshDeck(Deck::Staging, m_stagingNodeId);
}

void DeckController::updateDeckAudio(Deck deck, NodeId clipId,
                                     double currentTimeHint, bool forceSeek) {
    ClipGraph *g = graphOf(deck);
    if (!g) return;
    const SourceDescriptor desc = g->sourceDescriptor(clipId);
    if (desc.kind != SourceDescriptor::Kind::VideoFile
        && desc.kind != SourceDescriptor::Kind::AudioFile) {
        stopDeckAudio(deck);
        return;
    }

    int volume = 100;
    bool muted = false;
    bool routedToMaster = false;
    int playbackMode = 0;
    int audioDelayMs = 0;
    QString outputDeviceId;
    if (!g->audioSettingsForClip(clipId, volume, muted, routedToMaster, playbackMode, audioDelayMs, outputDeviceId)
        || !routedToMaster) {
        stopDeckAudio(deck);
        return;
    }

    auto &player = isLive(deck) ? m_audioPlayerLive : m_audioPlayerStaging;
    if (!player) player = std::make_unique<AudioPlayer>(this);
    wireAudioPlayerTap(deck, clipId);

    outputDeviceId = physicalDevice(outputDeviceId);
    const bool deviceChanged = (player->outputDeviceId() != outputDeviceId);
    player->setOutputDeviceId(outputDeviceId);
    player->setDelayMs(audioDelayMs);
    player->setSpeed(g->clipSettings(clipId).speed);

    ClipGraph::ResolvedAudioRoute route;
    if (g->resolveAudioStreamRoute(clipId, route))
        player->setEffectChain(route.effects);
    else
        player->setEffectChain({});

    const bool deckPlaying = isLive(deck) ? m_compositor->isPlayingLive() : m_compositor->isPlayingStaging();
    const QString &path = desc.path;
    // A device change requires re-creating the sink, so restart at the current
    // position rather than just seeking.
    if (player->currentFilePath() != path || deviceChanged) {
        const double startTime = (currentTimeHint >= 0.0) ? currentTimeHint : g->clipSettings(clipId).startTime;
        if (!player->start(path, startTime, !deckPlaying)) {
            stopDeckAudio(deck);
            return;
        }
    } else if (forceSeek && currentTimeHint >= 0.0) {
        player->seek(currentTimeHint);
    }

    // ActiveDeck: follow the transition's audio curve so the clip is only audible while its own
    // deck is on air. Always: full volume on either deck.
    (isLive(deck) ? m_audioFollowsFaderLive : m_audioFollowsFaderStaging) = playbackMode == 1;
    player->setCrossfadeFactor(1.0f);
    updateDeckAudioGain(deck);

    player->setVolumePercent(volume);
    player->setMuted(muted);

    if (deckPlaying) player->resume();
    else             player->pause();
}

void DeckController::updateDeckAudioGain(Deck deck) {
    auto &player = isLive(deck) ? m_audioPlayerLive : m_audioPlayerStaging;
    if (!player || !(isLive(deck) ? m_audioFollowsFaderLive : m_audioFollowsFaderStaging))
        return;
    const auto gains = m_compositor->computeAudioGains();
    player->setCrossfadeFactor(isLive(deck) ? gains.first : gains.second);
}

void DeckController::applyAudioControllerToDeck(Deck deck, NodeId clipId, bool forceSeek) {
    if (!graphOf(deck)) return;
    const auto kind = graphOf(deck)->sourceDescriptor(clipId).kind;
    if (kind != SourceDescriptor::Kind::VideoFile && kind != SourceDescriptor::Kind::AudioFile) {
        stopDeckAudio(deck);
        return;
    }
    auto *out = m_compositor;
    const double t = isLive(deck) ? out->getCurrentTimeLive() : out->getCurrentTimeStaging();
    updateDeckAudio(deck, clipId, t, forceSeek);
}

void DeckController::refreshShaderAudioForActiveDecks() {
    auto *out = m_compositor;

    // Drive every audio-script node from whichever deck owns its source clip, so
    // audio data is produced whether it feeds a shader or a script chain. Each cue
    // has its own runtime, so each deck syncs the runtime of the cue it plays.
    for (const Deck deck : {Deck::Live, Deck::Staging}) {
        GraphRuntime *rt = runtimeOf(deck);
        ClipGraph *g = graphOf(deck);
        if (!rt || !g) continue;
        const NodeId node = activeNode(deck);
        const double time = isLive(deck) ? out->getCurrentTimeLive() : out->getCurrentTimeStaging();
        const bool playing = isLive(deck) ? out->isPlayingLive() : out->isPlayingStaging();
        if (isLive(deck))
            rt->syncAllAudioScripts(node, time, playing, g->clipSettings(node).speed, 0, 0.0, false, 1.0);
        else
            rt->syncAllAudioScripts(0, 0.0, false, 1.0, node, time, playing, g->clipSettings(node).speed);
    }

    // Point each active shader at its (possibly merged) JSON data feed. Shaders
    // are now purely JSON-driven: their uniforms come from the script graph.
    auto refresh = [&](Deck deck, NodeId nodeId) {
        ClipGraph *g = graphOf(deck);
        if (!nodeId || !g) return;
        if (g->sourceDescriptor(nodeId).kind != SourceDescriptor::Kind::Shader)
            return;
        MediaSource *src = isLive(deck) ? out->sourceLive() : out->sourceStaging();
        if (!src) return;
        if (auto *shader = dynamic_cast<ShaderSource *>(src->innermost()))
            shader->setDataSource(runtimeOf(deck)->scriptOutputForDataNode(nodeId));
    };

    refresh(Deck::Live,  m_liveNodeId);
    refresh(Deck::Staging, m_stagingNodeId);
}

void DeckController::refreshTextDataForActiveDecks() {
    auto refresh = [&](Deck deck, NodeId nodeId) {
        ClipGraph *g = graphOf(deck);
        if (!nodeId || !g) return;
        const auto kind = g->sourceDescriptor(nodeId).kind;
        if (kind != SourceDescriptor::Kind::Text && kind != SourceDescriptor::Kind::SvgTemplate)
            return;

        auto *out = m_compositor;
        MediaSource *src = isLive(deck) ? out->sourceLive() : out->sourceStaging();
        if (!src) return;

        const auto data = runtimeOf(deck)->scriptOutputForDataNode(nodeId);
        MediaSource *base = src->innermost();
        if (kind == SourceDescriptor::Kind::Text) {
            if (auto *text = dynamic_cast<TextSource *>(base)) text->setDataSource(data);
        } else if (auto *svg = dynamic_cast<SvgTemplateSource *>(base)) {
            svg->setDataSource(data);
        }
    };

    refresh(Deck::Live,  m_liveNodeId);
    refresh(Deck::Staging, m_stagingNodeId);
}

// ── Deck assignment ───────────────────────────────────────────────────────────

DeckController::DeckUiState DeckController::deckUiState(const SourceDescriptor &desc,
                                                       const QString &name) {
    DeckUiState st;
    st.name = name;
    st.hasTransport = desc.isPausable();
    return st;
}

void DeckController::assignNodeToDeck(NodeId nodeId, Deck deck,
                                       const QVector<SourceEffectRef> &sourceEffects,
                                       bool startPlayback) {
    ClipGraph *g = graphOf(deck);
    GraphRuntime *rt = runtimeOf(deck);
    if (!g || !g->hasNode(nodeId)) return;
    const ClipSettings clip = g->clipSettings(nodeId);
    const bool repeat = g->clipRepeat(nodeId);
    m_compositor->setDeckSpeed(deck, clip.speed);

    // Record which node feeds the deck so later audio updates (seek, play/pause,
    // crossfade, loop restart) can find it again.
    if (isLive(deck)) m_liveNodeId = nodeId;
    else       m_stagingNodeId = nodeId;

    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor desc = g->sourceDescriptor(nodeId);
    auto *out = m_compositor;
    const bool fileKind = desc.kind == Kind::VideoFile || desc.kind == Kind::AudioFile;
    const bool play = startPlayback || !fileKind;

    // Base placement / crop / flip are applied by AppController from the
    // resolved stream; here we only wire the deck source/audio.

    // Decorator effects on the deck primary: the source must be a wrappable
    // MediaSource, so route every kind (including VideoFile/Image, which
    // normally take the loadVideo fast path) through SourceFactory and wrap it.
    if (!sourceEffects.isEmpty()) {
        auto src = SourceFactory::create(desc);
        if (src) {
            if (desc.kind == Kind::Shader) {
                static_cast<ShaderSource *>(src.get())->setDataSource(
                    rt->scriptOutputForDataNode(nodeId));
            } else if (desc.kind == Kind::Text) {
                if (auto data = rt->scriptOutputForDataNode(nodeId))
                    static_cast<TextSource *>(src.get())->setDataSource(data);
            } else if (desc.kind == Kind::SvgTemplate) {
                if (auto data = rt->scriptOutputForDataNode(nodeId))
                    static_cast<SvgTemplateSource *>(src.get())->setDataSource(data);
            } else if (desc.kind == Kind::VideoFile && clip.startTime > 0) {
                src->seek(clip.startTime);
            }
            src = ProcessEffects::applySourceEffects(std::move(src), sourceEffects);
        }
        if (isLive(deck)) {
            out->setRepeatLive(repeat);
            out->setTrimPointsLive(clip.startTime, clip.endTime);
            out->setSourceLive(std::move(src)); if (play) out->playLive();
        } else {
            out->setRepeatStaging(repeat);
            out->setTrimPointsStaging(clip.startTime, clip.endTime);
            out->setSourceStaging(std::move(src)); if (play) out->playStaging();
        }
        updateDeckAudio(deck, nodeId, clip.startTime, true);
        return;
    }

    switch (desc.kind) {

    case Kind::VideoFile:
    case Kind::Image: {
        if (isLive(deck)) {
            out->setRepeatLive(repeat);
            out->setTrimPointsLive(clip.startTime, clip.endTime);
            out->loadVideoLive(desc.path);
            if (clip.startTime > 0) out->seekLive(clip.startTime);
            if (play) out->playLive();
        } else {
            out->setRepeatStaging(repeat);
            out->setTrimPointsStaging(clip.startTime, clip.endTime);
            out->loadVideoStaging(desc.path);
            if (clip.startTime > 0) out->seekStaging(clip.startTime);
            if (play) out->playStaging();
        }
        updateDeckAudio(deck, nodeId, clip.startTime, true);
        return;
    }

    default: {
        // All other source kinds: use SourceFactory.
        auto src = SourceFactory::create(desc);
        if (desc.kind == Kind::Shader) {
            static_cast<ShaderSource *>(src.get())->setDataSource(
                rt->scriptOutputForDataNode(nodeId));
        }
        if (desc.kind == Kind::Text) {
            if (auto data = rt->scriptOutputForDataNode(nodeId))
                static_cast<TextSource *>(src.get())->setDataSource(data);
        }
        if (desc.kind == Kind::SvgTemplate && src) {
            if (auto data = rt->scriptOutputForDataNode(nodeId))
                static_cast<SvgTemplateSource *>(src.get())->setDataSource(data);
        }
        if (isLive(deck)) {
            out->setSourceLive(std::move(src));
            if (desc.kind != Kind::Canvas && play) out->playLive();
        } else {
            out->setSourceStaging(std::move(src));
            if (desc.kind != Kind::Canvas && play) out->playStaging();
        }
        return;
    }
    }
}

void DeckController::assignSourceToLiveDeck(std::unique_ptr<MediaSource> src,
                                               const QString &name) {
    Q_UNUSED(name);
    m_compositor->setSourceLive(std::move(src));
    m_compositor->playLive();
}

void DeckController::assignCompositeToDeck(std::unique_ptr<MediaSource> src, NodeId primaryNodeId,
                                           Deck deck, bool startPlayback) {
    auto *out = m_compositor;
    ClipGraph *g = graphOf(deck);
    if (isLive(deck)) m_liveNodeId = primaryNodeId; else m_stagingNodeId = primaryNodeId;
    out->setDeckSpeed(deck, g->clipSettings(primaryNodeId).speed);

    const ClipGraph::Node *primary = primaryNodeId ? g->node(primaryNodeId) : nullptr;
    if (!primary || primary->type != ClipGraph::NodeType::Input) {
        if (isLive(deck)) { out->setSourceLive(std::move(src)); out->playLive(); }
        else       { out->setSourceStaging(std::move(src)); out->playStaging(); }
        stopDeckAudio(deck);
        return;
    }

    const ClipSettings clip = g->clipSettings(primaryNodeId);
    const bool repeat = g->clipRepeat(primaryNodeId);
    const auto primaryKind = g->sourceDescriptor(primaryNodeId).kind;
    const bool play = startPlayback || (primaryKind != SourceDescriptor::Kind::VideoFile
                                        && primaryKind != SourceDescriptor::Kind::AudioFile);
    if (isLive(deck)) {
        out->setRepeatLive(repeat);
        out->setTrimPointsLive(clip.startTime, clip.endTime);
        out->setSourceLive(std::move(src));
        if (clip.startTime > 0) out->seekLive(clip.startTime);
        if (play) out->playLive();
    } else {
        out->setRepeatStaging(repeat);
        out->setTrimPointsStaging(clip.startTime, clip.endTime);
        out->setSourceStaging(std::move(src));
        if (clip.startTime > 0) out->seekStaging(clip.startTime);
        if (play) out->playStaging();
    }
    updateDeckAudio(deck, primaryNodeId, clip.startTime, true);
}

void DeckController::startDeckPlayback(Deck deck) {
    const NodeId nodeId = isLive(deck) ? m_liveNodeId : m_stagingNodeId;
    if (!nodeId || !graphOf(deck)) return;
    const auto kind = graphOf(deck)->sourceDescriptor(nodeId).kind;
    if (kind != SourceDescriptor::Kind::VideoFile && kind != SourceDescriptor::Kind::AudioFile)
        return;
    if (isLive(deck)) m_compositor->playLive(); else m_compositor->playStaging();
    applyAudioControllerToDeck(deck, nodeId, false);
}

void DeckController::parkDeck(Deck deck) {
    const NodeId nodeId = isLive(deck) ? m_liveNodeId : m_stagingNodeId;
    if (!nodeId || !graphOf(deck)) return;
    const auto kind = graphOf(deck)->sourceDescriptor(nodeId).kind;
    if (kind != SourceDescriptor::Kind::VideoFile && kind != SourceDescriptor::Kind::AudioFile)
        return;
    const double start = graphOf(deck)->clipSettings(nodeId).startTime;
    if (isLive(deck)) { m_compositor->pauseLive(); m_compositor->seekLive(start); }
    else       { m_compositor->pauseStaging(); m_compositor->seekStaging(start); }
    stopDeckAudio(deck);
}
