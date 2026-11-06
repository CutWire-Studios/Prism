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

void DeckController::wireAudioPlayerTap(bool deckA, NodeId clipId) {
    auto &player = deckA ? m_audioPlayerA : m_audioPlayerB;
    if (!player) return;

    const int deckIndex = deckA ? 0 : 1;
    player->setIsoPcmTap([this, deckIndex, clipId](const QByteArray &pcm) {
        if (m_outputHub)
            m_outputHub->submitDeckAudioChunk(deckIndex, clipId, pcm);
    });
    player->setPcmTap([this, deckIndex](const QByteArray &pcm) {
        if (m_outputHub)
            m_outputHub->submitProgramAudioChunk(deckIndex, pcm);
    });
}

void DeckController::stopDeckAudio(bool deckA) {
    auto &player = deckA ? m_audioPlayerA : m_audioPlayerB;
    if (player) player->stop();
}

void DeckController::releaseAllDeckAudio() {
    m_audioPlayerA.reset();
    m_audioPlayerB.reset();
}

void DeckController::swapDeckAudio() {
    std::swap(m_audioPlayerA, m_audioPlayerB);
    std::swap(m_aClipNodeId, m_bClipNodeId);
    std::swap(m_lastTimeA, m_lastTimeB);
    // Speed belongs to the deck, not the clip: the swapped players adopt their
    // new deck's rate.
    if (m_audioPlayerA) m_audioPlayerA->setSpeed(m_speedA);
    if (m_audioPlayerB) m_audioPlayerB->setSpeed(m_speedB);
}

void DeckController::setDeckSpeed(bool deckA, double speed) {
    (deckA ? m_speedA : m_speedB) = speed;
    m_compositor->setDeckSpeed(deckA, speed);
    const NodeId nodeId = deckA ? m_aClipNodeId : m_bClipNodeId;
    // Re-seek the audio to the video position so both resume in sync at the
    // new rate.
    if (nodeId)
        applyAudioControllerToDeck(deckA, nodeId, true);
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

    const auto refreshDeck = [this](bool deckA, NodeId clipId) {
        auto &player = deckA ? m_audioPlayerA : m_audioPlayerB;
        if (!clipId || !player) return;
        ClipGraph::ResolvedAudioRoute route;
        if (m_graph->resolveAudioStreamRoute(clipId, route))
            player->setEffectChain(route.effects);
        else
            player->setEffectChain({});
    };
    refreshDeck(true, m_aClipNodeId);
    refreshDeck(false, m_bClipNodeId);

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

void DeckController::updateDeckAudio(bool deckA, NodeId clipId,
                                     double currentTimeHint, bool forceSeek) {
    const SourceDescriptor desc = m_graph->sourceDescriptor(clipId);
    if (desc.kind != SourceDescriptor::Kind::VideoFile
        && desc.kind != SourceDescriptor::Kind::AudioFile) {
        stopDeckAudio(deckA);
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
        stopDeckAudio(deckA);
        return;
    }

    auto &player = deckA ? m_audioPlayerA : m_audioPlayerB;
    if (!player) player = std::make_unique<AudioPlayer>(this);
    wireAudioPlayerTap(deckA, clipId);

    const bool deviceChanged = (player->outputDeviceId() != outputDeviceId);
    player->setOutputDeviceId(outputDeviceId);
    player->setDelayMs(audioDelayMs);
    player->setSpeed(deckA ? m_speedA : m_speedB);

    ClipGraph::ResolvedAudioRoute route;
    if (m_graph->resolveAudioStreamRoute(clipId, route))
        player->setEffectChain(route.effects);
    else
        player->setEffectChain({});

    const QString &path = desc.path;
    // A device change requires re-creating the sink, so restart at the current
    // position rather than just seeking.
    if (player->currentFilePath() != path || deviceChanged) {
        const double startTime = (currentTimeHint >= 0.0) ? currentTimeHint : m_graph->clipSettings(clipId).startTime;
        if (!player->start(path, startTime)) {
            stopDeckAudio(deckA);
            return;
        }
    } else if (forceSeek && currentTimeHint >= 0.0) {
        player->seek(currentTimeHint);
    }

    auto *out = m_compositor;
    const float mixB = out->crossfade();

    // ActiveDeck: fade with the crossfader so the clip is only audible while
    // its own deck is the active side. Always: full volume on either deck.
    float volumeFactor = 1.0f;
    if (playbackMode == 1)
        volumeFactor = deckA ? (1.0f - mixB) : mixB;

    player->setVolumePercent(volume);
    player->setCrossfadeFactor(volumeFactor);
    player->setMuted(muted);

    const bool deckPlaying = deckA ? out->isPlayingA() : out->isPlayingB();
    if (deckPlaying) player->resume();
    else             player->pause();
}

void DeckController::applyAudioControllerToDeck(bool deckA, NodeId clipId, bool forceSeek) {
    const auto kind = m_graph->sourceDescriptor(clipId).kind;
    if (kind != SourceDescriptor::Kind::VideoFile && kind != SourceDescriptor::Kind::AudioFile) {
        stopDeckAudio(deckA);
        return;
    }
    auto *out = m_compositor;
    const double t = deckA ? out->getCurrentTimeA() : out->getCurrentTimeB();
    updateDeckAudio(deckA, clipId, t, forceSeek);
}

void DeckController::refreshShaderAudioForActiveDecks() {
    auto *out = m_compositor;

    // Drive every audio-script node from whichever deck owns its source clip, so
    // audio data is produced whether it feeds a shader, a script chain, or an A/B
    // switcher.
    m_runtime->syncAllAudioScripts(
        m_aClipNodeId, out->getCurrentTimeA(), out->isPlayingA(), m_speedA,
        m_bClipNodeId, out->getCurrentTimeB(), out->isPlayingB(), m_speedB);

    // Point each active shader at its (possibly merged) JSON data feed. Shaders
    // are now purely JSON-driven: their uniforms come from the script graph.
    auto refresh = [&](bool deckA, NodeId nodeId) {
        if (!nodeId) return;
        if (m_graph->sourceDescriptor(nodeId).kind != SourceDescriptor::Kind::Shader)
            return;
        MediaSource *src = deckA ? out->sourceA() : out->sourceB();
        if (!src) return;
        if (auto *shader = dynamic_cast<ShaderSource *>(src->innermost()))
            shader->setDataSource(m_runtime->scriptOutputForDataNode(nodeId));
    };

    refresh(true,  m_aClipNodeId);
    refresh(false, m_bClipNodeId);
}

void DeckController::refreshTextDataForActiveDecks() {
    auto refresh = [&](bool deckA, NodeId nodeId) {
        if (!nodeId) return;
        const auto kind = m_graph->sourceDescriptor(nodeId).kind;
        if (kind != SourceDescriptor::Kind::Text && kind != SourceDescriptor::Kind::SvgTemplate)
            return;

        auto *out = m_compositor;
        MediaSource *src = deckA ? out->sourceA() : out->sourceB();
        if (!src) return;

        const auto data = m_runtime->scriptOutputForDataNode(nodeId);
        MediaSource *base = src->innermost();
        if (kind == SourceDescriptor::Kind::Text) {
            if (auto *text = dynamic_cast<TextSource *>(base)) text->setDataSource(data);
        } else if (auto *svg = dynamic_cast<SvgTemplateSource *>(base)) {
            svg->setDataSource(data);
        }
    };

    refresh(true,  m_aClipNodeId);
    refresh(false, m_bClipNodeId);
}

// ── Deck assignment ───────────────────────────────────────────────────────────

DeckController::DeckUiState DeckController::deckUiState(const SourceDescriptor &desc,
                                                       const QString &name) {
    DeckUiState st;
    st.name = name;
    st.hasTransport = desc.isPausable();
    st.hasSpeed = desc.hasSpeedControl();
    return st;
}

void DeckController::assignNodeToDeck(NodeId nodeId, bool deckA,
                                       const QVector<SourceEffectRef> &sourceEffects) {
    if (!m_graph->hasNode(nodeId)) return;
    const ClipSettings clip = m_graph->clipSettings(nodeId);
    const bool repeat = m_graph->clipRepeat(nodeId);

    // Record which node feeds the deck so later audio updates (seek, play/pause,
    // crossfade, loop restart) can find it again.
    if (deckA) m_aClipNodeId = nodeId;
    else       m_bClipNodeId = nodeId;

    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor desc = m_graph->sourceDescriptor(nodeId);
    auto *out = m_compositor;

    // Base placement / crop / flip are applied by AppController from the
    // resolved stream; here we only wire overlays and the deck source/audio.
    auto applyTransform = [&](bool a) {
        if (a) out->setOverlaysA(clip.overlays);
        else   out->setOverlaysB(clip.overlays);
    };

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
        applyTransform(deckA);
        if (deckA) {
            out->setRepeatA(repeat);
            out->setTrimPointsA(clip.startTime, clip.endTime);
            out->setSourceA(std::move(src)); out->playA();
        } else {
            out->setRepeatB(repeat);
            out->setTrimPointsB(clip.startTime, clip.endTime);
            out->setSourceB(std::move(src)); out->playB();
        }
        updateDeckAudio(deckA, nodeId, clip.startTime, true);
        return;
    }

    switch (desc.kind) {

    case Kind::VideoFile:
    case Kind::Image: {
        applyTransform(deckA);
        if (deckA) {
            out->setRepeatA(repeat);
            out->setTrimPointsA(clip.startTime, clip.endTime);
            out->loadVideoA(desc.path);
            if (clip.startTime > 0) out->seekA(clip.startTime);
            out->playA();
        } else {
            out->setRepeatB(repeat);
            out->setTrimPointsB(clip.startTime, clip.endTime);
            out->loadVideoB(desc.path);
            if (clip.startTime > 0) out->seekB(clip.startTime);
            out->playB();
        }
        updateDeckAudio(deckA, nodeId, clip.startTime, true);
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
        applyTransform(deckA);
        if (deckA) {
            out->setSourceA(std::move(src));
            if (desc.kind != Kind::Canvas) out->playA();
        } else {
            out->setSourceB(std::move(src));
            if (desc.kind != Kind::Canvas) out->playB();
        }
        return;
    }
    }
}

void DeckController::assignSourceToActiveDeck(std::unique_ptr<MediaSource> src,
                                               const QString &name) {
    Q_UNUSED(name);
    const bool toA = (m_compositor->crossfade() <= 0.5f);
    auto *out = m_compositor;
    if (toA) { out->setSourceA(std::move(src)); out->playA(); }
    else     { out->setSourceB(std::move(src)); out->playB(); }
}

void DeckController::assignCompositeToDeck(std::unique_ptr<MediaSource> src, NodeId primaryNodeId,
                                           bool deckA) {
    auto *out = m_compositor;
    if (deckA) m_aClipNodeId = primaryNodeId; else m_bClipNodeId = primaryNodeId;
    if (deckA) out->setOverlaysA({}); else out->setOverlaysB({});

    const ClipGraph::Node *primary = primaryNodeId ? m_graph->node(primaryNodeId) : nullptr;
    if (!primary || primary->type != ClipGraph::NodeType::Input) {
        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else       { out->setSourceB(std::move(src)); out->playB(); }
        stopDeckAudio(deckA);
        return;
    }

    const ClipSettings clip = m_graph->clipSettings(primaryNodeId);
    const bool repeat = m_graph->clipRepeat(primaryNodeId);
    if (deckA) {
        out->setRepeatA(repeat);
        out->setTrimPointsA(clip.startTime, clip.endTime);
        out->setSourceA(std::move(src));
        if (clip.startTime > 0) out->seekA(clip.startTime);
        out->playA();
    } else {
        out->setRepeatB(repeat);
        out->setTrimPointsB(clip.startTime, clip.endTime);
        out->setSourceB(std::move(src));
        if (clip.startTime > 0) out->seekB(clip.startTime);
        out->playB();
    }
    updateDeckAudio(deckA, primaryNodeId, clip.startTime, true);
}
