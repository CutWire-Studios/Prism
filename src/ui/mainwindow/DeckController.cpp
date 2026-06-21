#include "ui/mainwindow/DeckController.h"
#include "ui/mainwindow/SourceFactory.h"
#include "core/sources/SlideshowSource.h"
#include "core/sources/CameraSource.h"
#include "core/sources/ScreenSource.h"
#include "core/sources/CanvasSource.h"
#include "core/sources/ShaderSource.h"
#include "core/sources/HtmlSource.h"
#include "core/sources/TextSource.h"
#include "core/media/AudioPlayer.h"
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QMediaDevices>
#include <QCameraDevice>
#include <algorithm>

DeckController::DeckController(OutputWindow  *outputWindow,
                               ClipNodeEditor *editor,
                               QObject       *parent)
    : QObject(parent)
    , m_outputWindow(outputWindow)
    , m_editor(editor)
{
}

// ── Static helpers ────────────────────────────────────────────────────────────

QString DeckController::formatTimeShort(double secs) {
    if (secs < 0) secs = 0;
    int m = (int)secs / 60, s = (int)secs % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

// ── Audio ─────────────────────────────────────────────────────────────────────

void DeckController::stopDeckAudio(bool deckA) {
    auto &player = deckA ? m_audioPlayerA : m_audioPlayerB;
    if (player) player->stop();
}

void DeckController::releaseAllDeckAudio() {
    m_audioPlayerA.reset();
    m_audioPlayerB.reset();
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

    const bool deviceChanged = (player->outputDeviceId() != outputDeviceId);
    player->setOutputDeviceId(outputDeviceId);
    player->setDelayMs(audioDelayMs);

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

    float volumeFactor = 1.0f;
    if      (playbackMode == AudioPlaybackMode::DeckAOnly) volumeFactor = 1.0f - mixB;
    else if (playbackMode == AudioPlaybackMode::DeckBOnly) volumeFactor = mixB;
    else                                                   volumeFactor = 1.0f;

    player->setVolumePercent(static_cast<int>(volume * volumeFactor));
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

void DeckController::updateDeckUI(bool deckA, const QString &name, bool hasTimeline,
                                   QSlider *progressSlider, QPushButton *playBtn,
                                   QLabel *selectedLabel, QLabel *timeLabel) {
    selectedLabel->setText(QString("%1: %2").arg(deckA ? "A" : "B", name));
    progressSlider->setEnabled(hasTimeline);
    playBtn->setEnabled(true);
    if (!hasTimeline) {
        progressSlider->setValue(0);
        timeLabel->setText("—");
    }
}

void DeckController::assignNodeToDeck(ClipNodeModel *node, NodeId nodeId, bool deckA,
                                       QSlider *progressSlider, QPushButton *playBtn,
                                       QLabel *selectedLabel, QLabel *timeLabel) {
    if (!node) return;

    using Kind = SourceDescriptor::Kind;
    const SourceDescriptor &desc = node->sourceDescriptor();
    auto *out = m_outputWindow->videoWidget();

    // Fetch transform for this clip.
    float baseX = 0.f, baseY = 0.f, baseW = 1.f, baseH = 1.f;
    if (!m_editor->clipTransform(nodeId, baseX, baseY, baseW, baseH)) {
        if (desc.kind == Kind::VideoFile || desc.kind == Kind::Image) {
            // Hard fail for file types that need a transform.
            if (deckA) out->setSourceA(nullptr);
            else       out->setSourceB(nullptr);
            stopDeckAudio(deckA);
            return;
        }
    }

    // Apply transform and crop.
    auto applyTransform = [&](bool a) {
        if (a) { out->setBaseA(baseX, baseY, baseW, baseH); out->setCropA(node->cropX(), node->cropY(), node->cropW(), node->cropH()); out->setOverlaysA(node->overlays()); }
        else   { out->setBaseB(baseX, baseY, baseW, baseH); out->setCropB(node->cropX(), node->cropY(), node->cropW(), node->cropH()); out->setOverlaysB(node->overlays()); }
    };

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
        progressSlider->setEnabled(desc.kind == Kind::VideoFile && dur > 0);
        playBtn->setEnabled(true);
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
        bool canPlay = (desc.kind != Kind::Canvas);
        progressSlider->setEnabled(false);
        playBtn->setEnabled(canPlay);
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
