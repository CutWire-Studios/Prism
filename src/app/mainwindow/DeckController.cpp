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
                               ClipGraph  *graph,
                               GraphRuntime *runtime,
                               OutputHub  *outputHub,
                               QObject    *parent)
    : QObject(parent)
    , m_compositor(compositor)
    , m_graph(graph)
    , m_runtime(runtime)
    , m_outputHub(outputHub)
{
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
}

void DeckController::applyClipSpeed(Deck deck) {
    const NodeId nodeId = isLive(deck) ? m_liveNodeId : m_stagingNodeId;
    m_compositor->setDeckSpeed(deck, m_graph->clipSettings(nodeId).speed);
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

void DeckController::syncMasterAudioInputs() {
    if (!m_graph) return;

    QSet<NodeId> activeInputs;
    for (NodeId inputId : m_graph->allMasterAudioInputNodeIds()) {
        ClipGraph::MasterAudioSettings settings;
        if (!m_graph->masterAudioInputSettings(inputId, settings))
            continue;

        ClipGraph::ResolvedAudioRoute route;
        const bool routedToOutput = m_graph->resolveAudioStreamRoute(inputId, route);
        const bool feedsScript = m_graph->hasAudioScriptConsumer(inputId);
        if (!routedToOutput && !feedsScript)
            continue;

        QString outputDeviceId;
        if (routedToOutput) {
            if (settings.routedOutputPortIndex >= 0) {
                if (!m_graph->masterAudioOutputDeviceForPort(
                        settings.routedMasterOutputId, settings.routedOutputPortIndex, outputDeviceId))
                    continue;
            } else if (!m_graph->masterAudioOutputDevice(settings.routedMasterOutputId, outputDeviceId)) {
                continue;
            }
        }

        activeInputs.insert(inputId);
        auto &capture = m_inputCaptures[inputId];
        if (!capture)
            capture = std::make_unique<AudioInputCapture>(this);

        const bool needsRestart = !capture->isRunning()
            || capture->inputDeviceId() != settings.deviceId
            || capture->targetOutputDeviceId() != outputDeviceId;

        capture->setInputDeviceId(settings.deviceId);
        capture->setTargetOutputDeviceId(outputDeviceId);
        capture->setVolumePercent(settings.volume);
        capture->setMuted(settings.muted);
        if (routedToOutput)
            capture->setEffectChain(route.effects);
        else
            capture->setEffectChain({});
        capture->setProgramRecordingTap([this, inputId](const QByteArray &pcm) {
            if (m_outputHub)
                m_outputHub->submitMicProgramAudioChunk(pcm);
            if (m_runtime)
                m_runtime->feedLiveAudioForProducer(inputId, pcm);
        });

        if (needsRestart)
            capture->start();
    }

    QSet<NodeId> activeCaptures;
    for (NodeId captureId : m_graph->allMasterAudioCaptureNodeIds()) {
        ClipGraph::MasterAudioSettings settings;
        if (!m_graph->masterAudioCaptureSettings(captureId, settings))
            continue;

        ClipGraph::ResolvedAudioRoute route;
        const bool routedToOutput = m_graph->resolveAudioStreamRoute(captureId, route);
        const bool feedsScript = m_graph->hasAudioScriptConsumer(captureId);
        if (!routedToOutput && !feedsScript)
            continue;

        QString outputDeviceId;
        if (routedToOutput) {
            if (settings.routedOutputPortIndex >= 0) {
                if (!m_graph->masterAudioOutputDeviceForPort(
                        settings.routedMasterOutputId, settings.routedOutputPortIndex, outputDeviceId))
                    continue;
            } else if (!m_graph->masterAudioOutputDevice(settings.routedMasterOutputId, outputDeviceId)) {
                continue;
            }
        }

        activeCaptures.insert(captureId);
        auto &capture = m_loopbackCaptures[captureId];
        if (!capture)
            capture = std::make_unique<AudioLoopbackCapture>(this);

        const bool needsRestart = !capture->isRunning()
            || capture->playbackDeviceId() != settings.deviceId
            || capture->targetOutputDeviceId() != outputDeviceId;

        capture->setPlaybackDeviceId(settings.deviceId);
        capture->setTargetOutputDeviceId(outputDeviceId);
        capture->setVolumePercent(settings.volume);
        capture->setMuted(settings.muted);
        if (routedToOutput)
            capture->setEffectChain(route.effects);
        else
            capture->setEffectChain({});
        capture->setProgramRecordingTap([this](const QByteArray &pcm) {
            if (m_outputHub)
                m_outputHub->submitMicProgramAudioChunk(pcm);
        });
        capture->setAnalysisTap([this, captureId](const QByteArray &pcm) {
            if (m_runtime)
                m_runtime->feedLiveAudioForProducer(captureId, pcm);
        });

        if (needsRestart)
            capture->start();
    }

    for (auto it = m_inputCaptures.begin(); it != m_inputCaptures.end(); ) {
        if (!activeInputs.contains(it->first)) {
            it->second->stop();
            it = m_inputCaptures.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_loopbackCaptures.begin(); it != m_loopbackCaptures.end(); ) {
        if (!activeCaptures.contains(it->first)) {
            it->second->stop();
            it = m_loopbackCaptures.erase(it);
        } else {
            ++it;
        }
    }
}

void DeckController::refreshAudioEffects() {
    if (!m_graph) return;

    const auto refreshDeck = [this](Deck deck, NodeId clipId) {
        auto &player = isLive(deck) ? m_audioPlayerLive : m_audioPlayerStaging;
        if (!clipId || !player) return;
        ClipGraph::ResolvedAudioRoute route;
        if (m_graph->resolveAudioStreamRoute(clipId, route))
            player->setEffectChain(route.effects);
        else
            player->setEffectChain({});
    };
    refreshDeck(Deck::Live, m_liveNodeId);
    refreshDeck(Deck::Staging, m_stagingNodeId);

    const auto refreshCaptures = [this](auto &captures) {
        for (auto &[id, capture] : captures) {
            ClipGraph::ResolvedAudioRoute route;
            if (m_graph->resolveAudioStreamRoute(id, route))
                capture->setEffectChain(route.effects);
            else
                capture->setEffectChain({});
        }
    };
    refreshCaptures(m_inputCaptures);
    refreshCaptures(m_loopbackCaptures);
}

void DeckController::updateDeckAudio(Deck deck, NodeId clipId,
                                     double currentTimeHint, bool forceSeek) {
    const SourceDescriptor desc = m_graph->sourceDescriptor(clipId);
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
    if (!m_graph->audioSettingsForClip(clipId, volume, muted, routedToMaster, playbackMode, audioDelayMs, outputDeviceId)
        || !routedToMaster) {
        stopDeckAudio(deck);
        return;
    }

    auto &player = isLive(deck) ? m_audioPlayerLive : m_audioPlayerStaging;
    if (!player) player = std::make_unique<AudioPlayer>(this);
    wireAudioPlayerTap(deck, clipId);

    const bool deviceChanged = (player->outputDeviceId() != outputDeviceId);
    player->setOutputDeviceId(outputDeviceId);
    player->setDelayMs(audioDelayMs);
    player->setSpeed(m_graph->clipSettings(clipId).speed);

    ClipGraph::ResolvedAudioRoute route;
    if (m_graph->resolveAudioStreamRoute(clipId, route))
        player->setEffectChain(route.effects);
    else
        player->setEffectChain({});

    const bool deckPlaying = isLive(deck) ? m_compositor->isPlayingLive() : m_compositor->isPlayingStaging();
    const QString &path = desc.path;
    // A device change requires re-creating the sink, so restart at the current
    // position rather than just seeking.
    if (player->currentFilePath() != path || deviceChanged) {
        const double startTime = (currentTimeHint >= 0.0) ? currentTimeHint : m_graph->clipSettings(clipId).startTime;
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
    const auto kind = m_graph->sourceDescriptor(clipId).kind;
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
    // audio data is produced whether it feeds a shader, a script chain, or a
    // switcher.
    m_runtime->syncAllAudioScripts(
        m_liveNodeId, out->getCurrentTimeLive(), out->isPlayingLive(), m_graph->clipSettings(m_liveNodeId).speed,
        m_stagingNodeId, out->getCurrentTimeStaging(), out->isPlayingStaging(), m_graph->clipSettings(m_stagingNodeId).speed);

    // Point each active shader at its (possibly merged) JSON data feed. Shaders
    // are now purely JSON-driven: their uniforms come from the script graph.
    auto refresh = [&](Deck deck, NodeId nodeId) {
        if (!nodeId) return;
        if (m_graph->sourceDescriptor(nodeId).kind != SourceDescriptor::Kind::Shader)
            return;
        MediaSource *src = isLive(deck) ? out->sourceLive() : out->sourceStaging();
        if (!src) return;
        if (auto *shader = dynamic_cast<ShaderSource *>(src->innermost()))
            shader->setDataSource(m_runtime->scriptOutputForDataNode(nodeId));
    };

    refresh(Deck::Live,  m_liveNodeId);
    refresh(Deck::Staging, m_stagingNodeId);
}

void DeckController::refreshTextDataForActiveDecks() {
    auto refresh = [&](Deck deck, NodeId nodeId) {
        if (!nodeId) return;
        const auto kind = m_graph->sourceDescriptor(nodeId).kind;
        if (kind != SourceDescriptor::Kind::Text && kind != SourceDescriptor::Kind::SvgTemplate)
            return;

        auto *out = m_compositor;
        MediaSource *src = isLive(deck) ? out->sourceLive() : out->sourceStaging();
        if (!src) return;

        const auto data = m_runtime->scriptOutputForDataNode(nodeId);
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
    if (!m_graph->hasNode(nodeId)) return;
    const ClipSettings clip = m_graph->clipSettings(nodeId);
    const bool repeat = m_graph->clipRepeat(nodeId);
    m_compositor->setDeckSpeed(deck, clip.speed);

    // Record which node feeds the deck so later audio updates (seek, play/pause,
    // crossfade, loop restart) can find it again.
    if (isLive(deck)) m_liveNodeId = nodeId;
    else       m_stagingNodeId = nodeId;

    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor desc = m_graph->sourceDescriptor(nodeId);
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
                    m_runtime->scriptOutputForDataNode(nodeId));
            } else if (desc.kind == Kind::Text) {
                if (auto data = m_runtime->scriptOutputForDataNode(nodeId))
                    static_cast<TextSource *>(src.get())->setDataSource(data);
            } else if (desc.kind == Kind::SvgTemplate) {
                if (auto data = m_runtime->scriptOutputForDataNode(nodeId))
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
                m_runtime->scriptOutputForDataNode(nodeId));
        }
        if (desc.kind == Kind::Text) {
            if (auto data = m_runtime->scriptOutputForDataNode(nodeId))
                static_cast<TextSource *>(src.get())->setDataSource(data);
        }
        if (desc.kind == Kind::SvgTemplate && src) {
            if (auto data = m_runtime->scriptOutputForDataNode(nodeId))
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
    if (isLive(deck)) m_liveNodeId = primaryNodeId; else m_stagingNodeId = primaryNodeId;
    out->setDeckSpeed(deck, m_graph->clipSettings(primaryNodeId).speed);

    const ClipGraph::Node *primary = primaryNodeId ? m_graph->node(primaryNodeId) : nullptr;
    if (!primary || primary->type != ClipGraph::NodeType::Input) {
        if (isLive(deck)) { out->setSourceLive(std::move(src)); out->playLive(); }
        else       { out->setSourceStaging(std::move(src)); out->playStaging(); }
        stopDeckAudio(deck);
        return;
    }

    const ClipSettings clip = m_graph->clipSettings(primaryNodeId);
    const bool repeat = m_graph->clipRepeat(primaryNodeId);
    const auto primaryKind = m_graph->sourceDescriptor(primaryNodeId).kind;
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
    if (!nodeId) return;
    const auto kind = m_graph->sourceDescriptor(nodeId).kind;
    if (kind != SourceDescriptor::Kind::VideoFile && kind != SourceDescriptor::Kind::AudioFile)
        return;
    if (isLive(deck)) m_compositor->playLive(); else m_compositor->playStaging();
    applyAudioControllerToDeck(deck, nodeId, false);
}

void DeckController::parkDeck(Deck deck) {
    const NodeId nodeId = isLive(deck) ? m_liveNodeId : m_stagingNodeId;
    if (!nodeId) return;
    const auto kind = m_graph->sourceDescriptor(nodeId).kind;
    if (kind != SourceDescriptor::Kind::VideoFile && kind != SourceDescriptor::Kind::AudioFile)
        return;
    const double start = m_graph->clipSettings(nodeId).startTime;
    if (isLive(deck)) { m_compositor->pauseLive(); m_compositor->seekLive(start); }
    else       { m_compositor->pauseStaging(); m_compositor->seekStaging(start); }
    stopDeckAudio(deck);
}
