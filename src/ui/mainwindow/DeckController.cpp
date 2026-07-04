#include "ui/mainwindow/DeckController.h"
#include "ui/mainwindow/SourceFactory.h"
#include "core/sources/SlideshowSource.h"
#include "core/sources/CameraSource.h"
#include "core/sources/ScreenSource.h"
#include "core/sources/CanvasSource.h"
#include "core/sources/ShaderSource.h"
#include "core/sources/HtmlSource.h"
#include "core/sources/TextSource.h"
#include "ui/nodes/ProcessEffects.h"
#include "core/media/AudioInputCapture.h"
#include "core/media/AudioInputMixRegistry.h"
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QSet>
#include <algorithm>

DeckController::DeckController(OutputWindow  *outputWindow,
                               ClipNodeEditor *editor,
                               OutputHub     *outputHub,
                               QObject       *parent)
    : QObject(parent)
    , m_outputWindow(outputWindow)
    , m_editor(editor)
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
    m_outputWindow->videoWidget()->setDeckSpeed(deckA, speed);
    const NodeId nodeId = deckA ? m_aClipNodeId : m_bClipNodeId;
    // Re-seek the audio to the video position so both resume in sync at the
    // new rate.
    if (nodeId)
        applyAudioControllerToDeck(deckA, nodeId, true);
}

void DeckController::releaseAllMasterAudioInputs() {
    m_inputCaptures.clear();
    AudioInputMixRegistry::clearAll();
}

void DeckController::syncMasterAudioInputs() {
    if (!m_editor) return;

    QSet<NodeId> activeInputs;
    for (NodeId inputId : m_editor->allMasterAudioInputNodeIds()) {
        MasterAudioInputSettings settings;
        if (!m_editor->masterAudioInputSettings(inputId, settings))
            continue;
        if (settings.routedMasterOutputId == 0)
            continue;

        QString outputDeviceId;
        if (!m_editor->masterAudioOutputDevice(settings.routedMasterOutputId, outputDeviceId))
            continue;

        activeInputs.insert(inputId);
        auto &capture = m_inputCaptures[inputId];
        if (!capture)
            capture = std::make_unique<AudioInputCapture>(this);

        const bool needsRestart = !capture->isRunning()
            || capture->inputDeviceId() != settings.inputDeviceId
            || capture->targetOutputDeviceId() != outputDeviceId;

        capture->setInputDeviceId(settings.inputDeviceId);
        capture->setTargetOutputDeviceId(outputDeviceId);
        capture->setVolumePercent(settings.volume);
        capture->setMuted(settings.muted);
        capture->setProgramRecordingTap([this](const QByteArray &pcm) {
            if (m_outputHub)
                m_outputHub->submitMicProgramAudioChunk(pcm);
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
}

void DeckController::updateDeckAudio(bool deckA, NodeId clipId, const ClipNodeModel *node,
                                     double currentTimeHint, bool forceSeek) {
    if (!node || node->sourceDescriptor().kind != SourceDescriptor::Kind::VideoFile) {
        stopDeckAudio(deckA);
        return;
    }

    int volume = 100;
    bool muted = false;
    bool routedToMaster = false;
    AudioPlaybackMode playbackMode = AudioPlaybackMode::Always;
    int audioDelayMs = 0;
    QString outputDeviceId;
    if (!m_editor->audioSettingsForClip(clipId, volume, muted, routedToMaster, playbackMode, audioDelayMs, outputDeviceId)
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

    const QString &path = node->sourceDescriptor().path;
    // A device change requires re-creating the sink, so restart at the current
    // position rather than just seeking.
    if (player->currentFilePath() != path || deviceChanged) {
        const double startTime = (currentTimeHint >= 0.0) ? currentTimeHint : node->startTime();
        if (!player->start(path, startTime)) {
            stopDeckAudio(deckA);
            return;
        }
    } else if (forceSeek && currentTimeHint >= 0.0) {
        player->seek(currentTimeHint);
    }

    auto *out = m_outputWindow->videoWidget();
    const float mixB = out->crossfade();

    // ActiveDeck: fade with the crossfader so the clip is only audible while
    // its own deck is the active side. Always: full volume on either deck.
    float volumeFactor = 1.0f;
    if (playbackMode == AudioPlaybackMode::ActiveDeck)
        volumeFactor = deckA ? (1.0f - mixB) : mixB;

    player->setVolumePercent(volume);
    player->setCrossfadeFactor(volumeFactor);
    player->setMuted(muted);

    const bool deckPlaying = deckA ? out->isPlayingA() : out->isPlayingB();
    if (deckPlaying) player->resume();
    else             player->pause();
}

void DeckController::applyAudioControllerToDeck(bool deckA, NodeId clipId, bool forceSeek) {
    auto *node = m_editor->nodeAt(clipId);
    if (!node || node->sourceDescriptor().kind != SourceDescriptor::Kind::VideoFile) {
        stopDeckAudio(deckA);
        return;
    }
    auto *out = m_outputWindow->videoWidget();
    const double t = deckA ? out->getCurrentTimeA() : out->getCurrentTimeB();
    updateDeckAudio(deckA, clipId, node, t, forceSeek);
}

void DeckController::refreshShaderAudioForActiveDecks() {
    auto refresh = [&](bool deckA, NodeId nodeId) {
        if (!nodeId) return;
        auto *node = m_editor->nodeAt(nodeId);
        if (!node || node->sourceDescriptor().kind != SourceDescriptor::Kind::Shader)
            return;

        auto *out = m_outputWindow->videoWidget();
        MediaSource *src = deckA ? out->sourceA() : out->sourceB();
        if (!src || src->type() != MediaSource::Type::Shader) return;

        QString audioPath;
        if (m_editor->audioSourceForShader(nodeId, audioPath))
            static_cast<ShaderSource *>(src)->setAudioSource(audioPath);
        else
            static_cast<ShaderSource *>(src)->setAudioSource(QString());
    };

    refresh(true,  m_aClipNodeId);
    refresh(false, m_bClipNodeId);
}

void DeckController::refreshTextDataForActiveDecks() {
    auto refresh = [&](bool deckA, NodeId nodeId) {
        if (!nodeId) return;
        auto *node = m_editor->nodeAt(nodeId);
        if (!node || node->sourceDescriptor().kind != SourceDescriptor::Kind::Text)
            return;

        auto *out = m_outputWindow->videoWidget();
        MediaSource *src = deckA ? out->sourceA() : out->sourceB();
        if (!src || src->type() != MediaSource::Type::Text) return;

        if (auto data = m_editor->scriptOutputForDataNode(nodeId))
            static_cast<TextSource *>(src)->setDataSource(data);
        else
            static_cast<TextSource *>(src)->setDataSource(nullptr);
    };

    refresh(true,  m_aClipNodeId);
    refresh(false, m_bClipNodeId);
}

// ── Deck assignment ───────────────────────────────────────────────────────────

void DeckController::updateDeckUI(bool deckA, const QString &name,
                                   const SourceDescriptor &desc,
                                   QSlider *progressSlider, QPushButton *playBtn,
                                   QLabel *selectedLabel, QLabel *timeLabel) {
    selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", name));
    progressSlider->setVisible(desc.isSeekable());
    progressSlider->setEnabled(desc.isSeekable());
    playBtn->setVisible(desc.isPausable());
    playBtn->setEnabled(desc.isPausable());
    if (!desc.isSeekable()) {
        progressSlider->setValue(0);
        timeLabel->setText("—");
    }
}

void DeckController::assignNodeToDeck(ClipNodeModel *node, NodeId nodeId, bool deckA,
                                       QSlider *progressSlider, QPushButton *playBtn,
                                       QLabel *selectedLabel, QLabel *timeLabel,
                                       const QVector<SourceEffectRef> &sourceEffects) {
    if (!node) return;

    // Record which node feeds the deck so later audio updates (seek, play/pause,
    // crossfade, loop restart) can find it again.
    if (deckA) m_aClipNodeId = nodeId;
    else       m_bClipNodeId = nodeId;

    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor &desc = node->sourceDescriptor();
    auto *out = m_outputWindow->videoWidget();

    // Base placement / crop / flip are applied by MainWindow::pushDecks() from the
    // resolved stream; here we only wire overlays and the deck source/audio.
    auto applyTransform = [&](bool a) {
        if (a) out->setOverlaysA(node->overlays());
        else   out->setOverlaysB(node->overlays());
    };

    // Decorator effects on the deck primary: the source must be a wrappable
    // MediaSource, so route every kind (including VideoFile/Image, which
    // normally take the loadVideo fast path) through SourceFactory and wrap it.
    if (!sourceEffects.isEmpty()) {
        auto src = SourceFactory::create(desc);
        if (src) {
            if (desc.kind == Kind::Shader) {
                QString audioPath;
                if (m_editor->audioSourceForShader(nodeId, audioPath))
                    static_cast<ShaderSource *>(src.get())->setAudioSource(audioPath);
            } else if (desc.kind == Kind::Text) {
                if (auto data = m_editor->scriptOutputForDataNode(nodeId))
                    static_cast<TextSource *>(src.get())->setDataSource(data);
            } else if (desc.kind == Kind::VideoFile && node->startTime() > 0) {
                src->seek(node->startTime());
            }
            src = ProcessEffects::applySourceEffects(std::move(src), sourceEffects);
        }
        applyTransform(deckA);
        if (deckA) { out->setSourceA(std::move(src)); out->playA(); }
        else       { out->setSourceB(std::move(src)); out->playB(); }
        updateDeckAudio(deckA, nodeId, node, node->startTime(), true);
        progressSlider->setVisible(false);
        playBtn->setVisible(desc.isPausable());
        playBtn->setEnabled(desc.isPausable());
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText("LIVE");
        return;
    }

    switch (desc.kind) {

    case Kind::VideoFile:
    case Kind::Image: {
        applyTransform(deckA);
        if (deckA) {
            out->setRepeatA(node->isRepeat());
            out->setTrimPointsA(node->startTime(), node->endTime());
            out->loadVideoA(desc.path);
            if (node->startTime() > 0) out->seekA(node->startTime());
            out->playA();
        } else {
            out->setRepeatB(node->isRepeat());
            out->setTrimPointsB(node->startTime(), node->endTime());
            out->loadVideoB(desc.path);
            if (node->startTime() > 0) out->seekB(node->startTime());
            out->playB();
        }
        updateDeckAudio(deckA, nodeId, node, node->startTime(), true);
        double dur = deckA ? out->getDurationA() : out->getDurationB();
        progressSlider->setRange(0, 1000);
        progressSlider->setValue(0);
        progressSlider->setVisible(desc.isSeekable());
        progressSlider->setEnabled(desc.isSeekable() && dur > 0);
        playBtn->setVisible(desc.isPausable());
        playBtn->setEnabled(desc.isPausable());
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        timeLabel->setText(formatTimeShort(node->startTime()) + " / " + formatTimeShort(dur));
        return;
    }

    default: {
        // All other source kinds: use SourceFactory.
        auto src = SourceFactory::create(desc);
        if (desc.kind == Kind::Shader) {
            QString audioPath;
            if (m_editor->audioSourceForShader(nodeId, audioPath))
                static_cast<ShaderSource *>(src.get())->setAudioSource(audioPath);
        }
        if (desc.kind == Kind::Text) {
            if (auto data = m_editor->scriptOutputForDataNode(nodeId))
                static_cast<TextSource *>(src.get())->setDataSource(data);
        }
        applyTransform(deckA);
        if (deckA) {
            out->setSourceA(std::move(src));
            if (desc.kind != Kind::Canvas) out->playA();
        } else {
            out->setSourceB(std::move(src));
            if (desc.kind != Kind::Canvas) out->playB();
        }
        progressSlider->setVisible(false);
        playBtn->setVisible(desc.isPausable());
        playBtn->setEnabled(desc.isPausable());
        selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", node->sourceName()));
        if (desc.kind == Kind::Canvas)
            timeLabel->setText(QString("%1x%2").arg(desc.canvasWidth).arg(desc.canvasHeight));
        else
            timeLabel->setText("LIVE");
        return;
    }
    }
}

void DeckController::assignSourceToActiveDeck(std::unique_ptr<MediaSource> src,
                                               const QString &name,
                                               QSlider *crossfader) {
    const bool toA = (crossfader->value() <= 50);
    auto *out = m_outputWindow->videoWidget();
    if (toA) { out->setSourceA(std::move(src)); out->playA(); }
    else     { out->setSourceB(std::move(src)); out->playB(); }
}
