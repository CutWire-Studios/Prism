#include "ui/output/OutputHub.h"
#include "ui/output/MirrorOutputWindow.h"
#include "ui/output/NdiProgramSink.h"
#include "ui/canvas/VideoWidget.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QThread>
#include <utility>

OutputHub::OutputHub(QObject *parent)
    : QObject(parent)
    , m_ndiSink(std::make_unique<NdiProgramSink>())
    , m_virtualCameraSink(std::make_unique<VirtualCameraProgramSink>())
    , m_programRecorder(std::make_unique<ProgramRecorder>(this))
    , m_programAudioRecorder(std::make_unique<ProgramAudioRecorder>(this))
    , m_deckAAudioRecorder(std::make_unique<ProgramAudioRecorder>(this))
    , m_deckBAudioRecorder(std::make_unique<ProgramAudioRecorder>(this))
    , m_deckARecorder(std::make_unique<ProgramRecorder>(this))
    , m_deckBRecorder(std::make_unique<ProgramRecorder>(this))
    , m_outputDir(ProgramRecorder::defaultOutputDir())
    ,     m_progressTimer(new QTimer(this))
{
    m_progressTimer->setInterval(1000);
    connect(m_progressTimer, &QTimer::timeout, this, &OutputHub::onRecordingProgressTick);

    auto connectRecorder = [this](ProgramRecorder *rec) {
        if (!rec) return;
        connect(rec, &ProgramRecorder::errorOccurred, this, &OutputHub::recordingError);
    };
    connectRecorder(m_programRecorder.get());
    connectRecorder(m_deckARecorder.get());
    connectRecorder(m_deckBRecorder.get());
    if (m_programAudioRecorder) {
        connect(m_programAudioRecorder.get(), &ProgramAudioRecorder::errorOccurred,
                this, &OutputHub::recordingError);
    }
    auto connectAudioRecorder = [this](ProgramAudioRecorder *rec) {
        if (!rec) return;
        connect(rec, &ProgramAudioRecorder::errorOccurred, this, &OutputHub::recordingError);
    };
    connectAudioRecorder(m_deckAAudioRecorder.get());
    connectAudioRecorder(m_deckBAudioRecorder.get());

    m_dispatchThread = QThread::create([this] { dispatchLoop(); });
    m_dispatchThread->setObjectName(QStringLiteral("OutputDispatch"));
    m_dispatchThread->start();
}

OutputHub::~OutputHub() {
    {
        QMutexLocker lk(&m_mailboxMutex);
        m_dispatchStop = true;
        m_mailboxCv.wakeAll();
    }
    if (m_dispatchThread) {
        m_dispatchThread->wait();
        delete m_dispatchThread;
        m_dispatchThread = nullptr;
    }
    stopAllRecording();
}

void OutputHub::dispatchLoop() {
    for (;;) {
        QImage program, deckA, deckB;
        {
            QMutexLocker lk(&m_mailboxMutex);
            while (!m_frameDirty && !m_dispatchStop)
                m_mailboxCv.wait(&m_mailboxMutex);
            if (m_dispatchStop)
                return;
            program = std::move(m_pendingProgram);
            deckA   = std::move(m_pendingDeckA);
            deckB   = std::move(m_pendingDeckB);
            m_pendingProgram = QImage();
            m_pendingDeckA   = QImage();
            m_pendingDeckB   = QImage();
            m_frameDirty = false;
        }
        QMutexLocker sink(&m_sinkMutex);
        distributeFrames(program, deckA, deckB);
    }
}

void OutputHub::distributeFrames(const QImage &programFrame,
                                 const QImage &deckAFrame,
                                 const QImage &deckBFrame) {
    if (m_ndiEnabled && m_ndiSink && m_ndiSink->isActive() && !programFrame.isNull())
        m_ndiSink->submitFrame(programFrame);

    if (m_virtualCameraEnabled && m_virtualCameraSink && m_virtualCameraSink->isActive()
        && !programFrame.isNull())
        m_virtualCameraSink->submitFrame(programFrame);

    if (m_programRecorder && m_programRecorder->isRecording() && !programFrame.isNull())
        m_programRecorder->submitFrame(programFrame);

    if (m_deckARecorder && m_deckARecorder->isRecording() && !deckAFrame.isNull())
        m_deckARecorder->submitFrame(deckAFrame);

    if (m_deckBRecorder && m_deckBRecorder->isRecording() && !deckBFrame.isNull())
        m_deckBRecorder->submitFrame(deckBFrame);

    for (const auto &entry : m_sourceRecorders) {
        const NodeId nodeId = entry.first;
        ProgramRecorder *rec = entry.second.get();
        if (!rec || !rec->isRecording()) continue;

        const bool onA = nodeId != 0 && nodeId == m_activeDeckA;
        const bool onB = nodeId != 0 && nodeId == m_activeDeckB;
        if (!onA && !onB) continue;

        const QImage &srcFrame = onA ? deckAFrame : deckBFrame;
        if (!srcFrame.isNull())
            rec->submitFrame(srcFrame);
    }
}

void OutputHub::setOutputDir(const QString &dir) {
    m_outputDir = dir.isEmpty() ? ProgramRecorder::defaultOutputDir() : dir;
}

void OutputHub::setProgramSource(VideoWidget *source) {
    if (m_videoWidget)
        disconnect(m_videoWidget, nullptr, this, nullptr);
    m_videoWidget = source;
    m_frameSource = source;
    if (!m_frameSource) return;

    connect(m_videoWidget, &VideoWidget::programFrameReady,
            this, &OutputHub::onProgramFrameReady);

    syncFrameConsumers();
}

void OutputHub::setProgramSourceForTest(ProgramFrameSource *source) {
    m_frameSource = source;
}

VideoWidget *OutputHub::programSource() const {
    return m_videoWidget.data();
}

void OutputHub::setActiveDeckNodes(NodeId deckA, NodeId deckB) {
    QMutexLocker locker(&m_sinkMutex);
    m_activeDeckA = deckA;
    m_activeDeckB = deckB;
}

MirrorOutputWindow *OutputHub::addMirrorOutput(const QString &title) {
    if (!m_frameSource) return nullptr;

    auto *window = new MirrorOutputWindow();
    window->setAttribute(Qt::WA_DeleteOnClose);

    if (!title.isEmpty())
        window->setWindowTitle(title);

    connect(window, &QObject::destroyed, this, &OutputHub::onMirrorDestroyed);

    m_mirrors.append(window);
    syncFrameConsumers();
    placeOnSecondaryScreen(window);
    window->show();
    return window;
}

bool OutputHub::ndiAvailable() const {
    return m_ndiSink && m_ndiSink->isAvailable();
}

QString OutputHub::ndiStreamName() const {
    return m_ndiSink ? m_ndiSink->ndiName() : QString{};
}

bool OutputHub::setNdiOutputEnabled(bool enabled, const QString &streamName) {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_ndiSink || !ndiAvailable()) {
        if (enabled)
            return false;
        enabled = false;
    }

    if (enabled == m_ndiEnabled)
        return true;

    if (enabled) {
        if (!m_ndiSink->start(streamName))
            return false;
        m_ndiEnabled = true;
    } else {
        m_ndiSink->stop();
        m_ndiEnabled = false;
    }

    syncFrameConsumers();
    emit ndiOutputEnabledChanged(m_ndiEnabled);
    return true;
}

bool OutputHub::virtualCameraAvailable() const {
    return m_virtualCameraSink && m_virtualCameraSink->isAvailable();
}

QString OutputHub::virtualCameraDevicePath() const {
    return m_virtualCameraSink ? m_virtualCameraSink->devicePath() : QString{};
}

bool OutputHub::setVirtualCameraEnabled(bool enabled, const QString &devicePath) {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_virtualCameraSink || !virtualCameraAvailable()) {
        if (enabled)
            return false;
        enabled = false;
    }

    if (enabled == m_virtualCameraEnabled)
        return true;

    if (enabled) {
        if (!devicePath.isEmpty())
            m_virtualCameraSink->setDevicePath(devicePath);
        if (!m_virtualCameraSink->start())
            return false;
        m_virtualCameraEnabled = true;
    } else {
        m_virtualCameraSink->stop();
        m_virtualCameraEnabled = false;
    }

    syncFrameConsumers();
    emit virtualCameraEnabledChanged(m_virtualCameraEnabled);
    return true;
}

ProgramRecorder *OutputHub::recorderFor(TrackKind kind, NodeId sourceNodeId) const {
    switch (kind) {
    case TrackKind::Program: return m_programRecorder.get();
    case TrackKind::DeckA:   return m_deckARecorder.get();
    case TrackKind::DeckB:   return m_deckBRecorder.get();
    case TrackKind::ProgramAudio:
    case TrackKind::DeckAAudio:
    case TrackKind::DeckBAudio:
    case TrackKind::ClipAudio:
        return nullptr;
    case TrackKind::Source: {
        const auto it = m_sourceRecorders.find(sourceNodeId);
        return it != m_sourceRecorders.end() ? it->second.get() : nullptr;
    }
    }
    return nullptr;
}

ProgramAudioRecorder *OutputHub::audioRecorderFor(TrackKind kind, NodeId sourceNodeId) const {
    switch (kind) {
    case TrackKind::ProgramAudio: return m_programAudioRecorder.get();
    case TrackKind::DeckAAudio:   return m_deckAAudioRecorder.get();
    case TrackKind::DeckBAudio:   return m_deckBAudioRecorder.get();
    case TrackKind::ClipAudio: {
        const auto it = m_clipAudioRecorders.find(sourceNodeId);
        return it != m_clipAudioRecorders.end() ? it->second.get() : nullptr;
    }
    default:
        return nullptr;
    }
}

bool OutputHub::isRecording() const {
    if (m_programRecorder && m_programRecorder->isRecording()) return true;
    if (m_programAudioRecorder && m_programAudioRecorder->isRecording()) return true;
    if (m_deckAAudioRecorder && m_deckAAudioRecorder->isRecording()) return true;
    if (m_deckBAudioRecorder && m_deckBAudioRecorder->isRecording()) return true;
    for (const auto &entry : m_clipAudioRecorders) {
        if (entry.second && entry.second->isRecording()) return true;
    }
    if (m_deckARecorder && m_deckARecorder->isRecording()) return true;
    if (m_deckBRecorder && m_deckBRecorder->isRecording()) return true;
    for (const auto &entry : m_sourceRecorders) {
        if (entry.second && entry.second->isRecording()) return true;
    }
    return false;
}

bool OutputHub::isProgramRecording() const {
    return m_programRecorder && m_programRecorder->isRecording();
}

bool OutputHub::isTrackRecording(TrackKind kind, NodeId sourceNodeId) const {
    if (const ProgramAudioRecorder *audioRec = audioRecorderFor(kind, sourceNodeId))
        return audioRec->isRecording();
    const ProgramRecorder *rec = recorderFor(kind, sourceNodeId);
    return rec && rec->isRecording();
}

qint64 OutputHub::trackRecordingDurationMs(TrackKind kind, NodeId sourceNodeId) const {
    if (const ProgramAudioRecorder *audioRec = audioRecorderFor(kind, sourceNodeId))
        return audioRec->recordingDurationMs();
    const ProgramRecorder *rec = recorderFor(kind, sourceNodeId);
    return rec ? rec->recordingDurationMs() : 0;
}

qint64 OutputHub::longestActiveRecordingMs() const {
    qint64 maxMs = 0;
    auto consider = [&](const ProgramRecorder *rec) {
        if (rec && rec->isRecording())
            maxMs = qMax(maxMs, rec->recordingDurationMs());
    };
    consider(m_programRecorder.get());
    consider(m_deckARecorder.get());
    consider(m_deckBRecorder.get());
    if (m_programAudioRecorder && m_programAudioRecorder->isRecording())
        maxMs = qMax(maxMs, m_programAudioRecorder->recordingDurationMs());
    if (m_deckAAudioRecorder && m_deckAAudioRecorder->isRecording())
        maxMs = qMax(maxMs, m_deckAAudioRecorder->recordingDurationMs());
    if (m_deckBAudioRecorder && m_deckBAudioRecorder->isRecording())
        maxMs = qMax(maxMs, m_deckBAudioRecorder->recordingDurationMs());
    for (const auto &entry : m_clipAudioRecorders) {
        if (entry.second && entry.second->isRecording())
            maxMs = qMax(maxMs, entry.second->recordingDurationMs());
    }
    for (const auto &entry : m_sourceRecorders)
        consider(entry.second.get());
    return maxMs;
}

QStringList OutputHub::activeRecordingTrackLabels() const {
    QStringList labels;
    if (m_programRecorder && m_programRecorder->isRecording())
        labels << tr("Program");
    if (m_programAudioRecorder && m_programAudioRecorder->isRecording())
        labels << tr("Program audio");
    if (m_deckAAudioRecorder && m_deckAAudioRecorder->isRecording())
        labels << tr("Deck A audio");
    if (m_deckBAudioRecorder && m_deckBAudioRecorder->isRecording())
        labels << tr("Deck B audio");
    for (const auto &entry : m_clipAudioRecorders) {
        if (entry.second && entry.second->isRecording())
            labels << entry.second->trackLabel();
    }
    if (m_deckARecorder && m_deckARecorder->isRecording())
        labels << tr("Deck A");
    if (m_deckBRecorder && m_deckBRecorder->isRecording())
        labels << tr("Deck B");
    for (const auto &entry : m_sourceRecorders) {
        if (entry.second && entry.second->isRecording())
            labels << entry.second->trackLabel();
    }
    return labels;
}

QString OutputHub::sanitizeFileStem(const QString &name) {
    QString stem = name.trimmed();
    if (stem.isEmpty())
        stem = QStringLiteral("source");
    stem.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    stem.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral("_"));
    return stem.left(48);
}

QString OutputHub::makeTrackOutputPath(const QString &suffix) const {
    QDir().mkpath(m_outputDir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    return ProgramRecorder::makeOutputPath(m_outputDir, stamp, suffix);
}

QString OutputHub::makeAudioTrackOutputPath(const QString &suffix) const {
    QDir().mkpath(m_outputDir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    return ProgramAudioRecorder::makeOutputPath(m_outputDir, stamp, suffix);
}

bool OutputHub::startProgramRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_frameSource || m_programRecorder->isRecording()) return m_programRecorder->isRecording();
    const QString path = makeTrackOutputPath(QStringLiteral("program"));
    if (!m_programRecorder->startRecording(path, tr("Program"), true))
        return false;
    m_programRecorder->addMarker(tr("Recording started"));
    syncFrameConsumers();
    if (m_frameSource)
        m_frameSource->captureOutputFrameNow();
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopProgramRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_programRecorder->isRecording()) return;
    m_programRecorder->stopRecording();
    syncFrameConsumers();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startDeckARecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_frameSource || m_deckARecorder->isRecording()) return m_deckARecorder->isRecording();
    const QString path = makeTrackOutputPath(QStringLiteral("deckA"));
    if (!m_deckARecorder->startRecording(path, tr("Deck A"), true))
        return false;
    m_deckARecorder->addMarker(tr("Recording started"));
    syncFrameConsumers();
    if (m_frameSource)
        m_frameSource->captureOutputFrameNow();
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopDeckARecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_deckARecorder->isRecording()) return;
    m_deckARecorder->stopRecording();
    syncFrameConsumers();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startDeckBRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_frameSource || m_deckBRecorder->isRecording()) return m_deckBRecorder->isRecording();
    const QString path = makeTrackOutputPath(QStringLiteral("deckB"));
    if (!m_deckBRecorder->startRecording(path, tr("Deck B"), true))
        return false;
    m_deckBRecorder->addMarker(tr("Recording started"));
    syncFrameConsumers();
    if (m_frameSource)
        m_frameSource->captureOutputFrameNow();
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopDeckBRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_deckBRecorder->isRecording()) return;
    m_deckBRecorder->stopRecording();
    syncFrameConsumers();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startSourceRecording(NodeId nodeId, const QString &label) {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_frameSource || nodeId == 0) return false;
    if (isTrackRecording(TrackKind::Source, nodeId))
        return true;

    auto rec = std::make_unique<ProgramRecorder>(this);
    const QString suffix = sanitizeFileStem(label);
    const QString path = makeTrackOutputPath(suffix);
    const QString trackLabel = label.isEmpty() ? suffix : label;
    if (!rec->startRecording(path, trackLabel, true))
        return false;
    rec->addMarker(tr("Recording started"));
    m_sourceRecorders[nodeId] = std::move(rec);
    syncFrameConsumers();
    if (m_frameSource)
        m_frameSource->captureOutputFrameNow();
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopSourceRecording(NodeId nodeId) {
    QMutexLocker locker(&m_sinkMutex);
    const auto it = m_sourceRecorders.find(nodeId);
    if (it == m_sourceRecorders.end() || !it->second->isRecording())
        return;
    it->second->stopRecording();
    m_sourceRecorders.erase(it);
    syncFrameConsumers();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::isProgramAudioRecording() const {
    return m_programAudioRecorder && m_programAudioRecorder->isRecording();
}

bool OutputHub::startProgramAudioRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_programAudioRecorder || m_programAudioRecorder->isRecording())
        return isProgramAudioRecording();

    const QString path = makeAudioTrackOutputPath(QStringLiteral("program"));
    if (!m_programAudioRecorder->startRecording(path, tr("Program audio"), true))
        return false;

    m_programAudioRecorder->addMarker(tr("Recording started"));
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopProgramAudioRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_programAudioRecorder || !m_programAudioRecorder->isRecording())
        return;
    m_programAudioRecorder->stopRecording();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

void OutputHub::submitProgramAudioChunk(int deckIndex, const QByteArray &pcm) {
    if (!m_programAudioRecorder || !m_programAudioRecorder->isRecording())
        return;
    m_programAudioRecorder->submitDeckChunk(deckIndex, pcm);
}

void OutputHub::submitMicProgramAudioChunk(const QByteArray &pcm) {
    if (!m_programAudioRecorder || !m_programAudioRecorder->isRecording())
        return;
    m_programAudioRecorder->submitMicChunk(pcm);
}

void OutputHub::submitDeckAudioChunk(int deckIndex, NodeId clipId, const QByteArray &pcm) {
    if (pcm.isEmpty() || (deckIndex != 0 && deckIndex != 1))
        return;

    if (deckIndex == 0 && m_deckAAudioRecorder && m_deckAAudioRecorder->isRecording())
        m_deckAAudioRecorder->submitPcm(pcm);
    if (deckIndex == 1 && m_deckBAudioRecorder && m_deckBAudioRecorder->isRecording())
        m_deckBAudioRecorder->submitPcm(pcm);

    const NodeId activeClip = deckIndex == 0 ? m_activeDeckA : m_activeDeckB;
    if (activeClip == 0 || activeClip != clipId)
        return;

    const auto it = m_clipAudioRecorders.find(clipId);
    if (it != m_clipAudioRecorders.end() && it->second && it->second->isRecording())
        it->second->submitPcm(pcm);
}

bool OutputHub::startDeckAAudioRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_deckAAudioRecorder || m_deckAAudioRecorder->isRecording())
        return m_deckAAudioRecorder && m_deckAAudioRecorder->isRecording();

    const QString path = makeAudioTrackOutputPath(QStringLiteral("deckA"));
    if (!m_deckAAudioRecorder->startRecording(path, tr("Deck A audio"), true))
        return false;

    m_deckAAudioRecorder->addMarker(tr("Recording started"));
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopDeckAAudioRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_deckAAudioRecorder || !m_deckAAudioRecorder->isRecording())
        return;
    m_deckAAudioRecorder->stopRecording();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startDeckBAudioRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_deckBAudioRecorder || m_deckBAudioRecorder->isRecording())
        return m_deckBAudioRecorder && m_deckBAudioRecorder->isRecording();

    const QString path = makeAudioTrackOutputPath(QStringLiteral("deckB"));
    if (!m_deckBAudioRecorder->startRecording(path, tr("Deck B audio"), true))
        return false;

    m_deckBAudioRecorder->addMarker(tr("Recording started"));
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopDeckBAudioRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_deckBAudioRecorder || !m_deckBAudioRecorder->isRecording())
        return;
    m_deckBAudioRecorder->stopRecording();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startClipAudioRecording(NodeId nodeId, const QString &label) {
    QMutexLocker locker(&m_sinkMutex);
    if (nodeId == 0)
        return false;
    if (isTrackRecording(TrackKind::ClipAudio, nodeId))
        return true;

    const QString stem = sanitizeFileStem(label);
    const QString path = makeAudioTrackOutputPath(stem);
    auto rec = std::make_unique<ProgramAudioRecorder>(this);
    connect(rec.get(), &ProgramAudioRecorder::errorOccurred, this, &OutputHub::recordingError);
    const QString trackLabel = label.isEmpty() ? tr("Clip audio") : label;
    if (!rec->startRecording(path, trackLabel, true))
        return false;

    rec->addMarker(tr("Recording started"));
    m_clipAudioRecorders[nodeId] = std::move(rec);
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopClipAudioRecording(NodeId nodeId) {
    QMutexLocker locker(&m_sinkMutex);
    const auto it = m_clipAudioRecorders.find(nodeId);
    if (it == m_clipAudioRecorders.end() || !it->second->isRecording())
        return;
    it->second->stopRecording();
    m_clipAudioRecorders.erase(it);
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

void OutputHub::stopAllRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (m_programRecorder && m_programRecorder->isRecording())
        m_programRecorder->stopRecording();
    if (m_programAudioRecorder && m_programAudioRecorder->isRecording())
        m_programAudioRecorder->stopRecording();
    if (m_deckAAudioRecorder && m_deckAAudioRecorder->isRecording())
        m_deckAAudioRecorder->stopRecording();
    if (m_deckBAudioRecorder && m_deckBAudioRecorder->isRecording())
        m_deckBAudioRecorder->stopRecording();
    for (auto it = m_clipAudioRecorders.begin(); it != m_clipAudioRecorders.end(); ) {
        if (it->second && it->second->isRecording())
            it->second->stopRecording();
        it = m_clipAudioRecorders.erase(it);
    }
    if (m_deckARecorder && m_deckARecorder->isRecording())
        m_deckARecorder->stopRecording();
    if (m_deckBRecorder && m_deckBRecorder->isRecording())
        m_deckBRecorder->stopRecording();
    for (auto it = m_sourceRecorders.begin(); it != m_sourceRecorders.end(); ) {
        if (it->second && it->second->isRecording())
            it->second->stopRecording();
        it = m_sourceRecorders.erase(it);
    }
    maybeStopProgressTimer();
    syncFrameConsumers();
}

void OutputHub::addRecordingMarker(const QString &label) {
    QMutexLocker locker(&m_sinkMutex);
    if (!isRecording() || label.isEmpty()) return;

    auto mark = [&](ProgramRecorder *rec) {
        if (rec && rec->isRecording())
            rec->addMarker(label);
    };
    mark(m_programRecorder.get());
    mark(m_deckARecorder.get());
    mark(m_deckBRecorder.get());
    for (auto &entry : m_sourceRecorders)
        mark(entry.second.get());
    auto markAudio = [&](ProgramAudioRecorder *rec) {
        if (rec && rec->isRecording())
            rec->addMarker(label);
    };
    markAudio(m_programAudioRecorder.get());
    markAudio(m_deckAAudioRecorder.get());
    markAudio(m_deckBAudioRecorder.get());
    for (auto &entry : m_clipAudioRecorders)
        markAudio(entry.second.get());
}

void OutputHub::ensureProgressTimer() {
    if (!m_progressTimer->isActive()) {
        m_progressTimer->start();
        emit recordingProgress(longestActiveRecordingMs());
    }
}

void OutputHub::maybeStopProgressTimer() {
    if (!isRecording())
        m_progressTimer->stop();
    emit recordingProgress(longestActiveRecordingMs());
}

void OutputHub::onProgramFrameReady() {
    if (!m_frameSource) return;

    // GL readback happens here on the GUI thread (it must own the GL context).
    const QImage programFrame = m_frameSource->programFrame();
    const bool needDeck = m_needDeckReadback.load(std::memory_order_relaxed);
    const QImage deckAFrame = needDeck ? m_frameSource->deckProgramFrame(true)  : QImage();
    const QImage deckBFrame = needDeck ? m_frameSource->deckProgramFrame(false) : QImage();

    // Mirror windows are QWidgets — update them on the GUI thread. setFrame() just
    // stores the (implicitly-shared) image and schedules a repaint, so it's cheap.
    if (!programFrame.isNull()) {
        for (const auto &mirror : m_mirrors) {
            if (mirror)
                mirror->setFrame(programFrame);
        }
    }

    // Hand the heavy sinks (NDI / virtual camera / recorders) to the dispatch
    // thread. Keep only the most recent frame set (drop-old) to stay realtime.
    QMutexLocker lk(&m_mailboxMutex);
    m_pendingProgram = programFrame;
    m_pendingDeckA   = deckAFrame;
    m_pendingDeckB   = deckBFrame;
    m_frameDirty = true;
    m_mailboxCv.wakeOne();
}

void OutputHub::onMirrorDestroyed(QObject *obj) {
    m_mirrors.removeAll(static_cast<MirrorOutputWindow *>(obj));
    syncFrameConsumers();
}

void OutputHub::onRecordingProgressTick() {
    if (!isRecording()) {
        m_progressTimer->stop();
        return;
    }
    emit recordingProgress(longestActiveRecordingMs());
}

void OutputHub::syncFrameConsumers() {
    QMutexLocker locker(&m_sinkMutex);
    const bool needDeck = needsDeckFrameReadback();
    m_needDeckReadback.store(needDeck, std::memory_order_relaxed);
    if (!m_frameSource) return;
    m_frameSource->setProgramFrameConsumerCount(activeFrameConsumerCount());
    m_frameSource->setDeckFrameConsumerCount(needDeck ? 1 : 0);
}

int OutputHub::activeFrameConsumerCount() const {
    int count = 0;
    for (const auto &mirror : m_mirrors) {
        if (mirror) ++count;
    }
    if (m_ndiEnabled)
        ++count;
    if (m_virtualCameraEnabled)
        ++count;
    if (m_programRecorder && m_programRecorder->isRecording())
        ++count;
    return count;
}

bool OutputHub::needsDeckFrameReadback() const {
    if (m_deckARecorder && m_deckARecorder->isRecording()) return true;
    if (m_deckBRecorder && m_deckBRecorder->isRecording()) return true;
    for (const auto &entry : m_sourceRecorders) {
        if (entry.second && entry.second->isRecording()) return true;
    }
    return false;
}

void OutputHub::placeOnSecondaryScreen(QWidget *window) {
    const auto screens = QGuiApplication::screens();
    if (screens.size() < 2) return;

    QScreen *secondary = screens.at(1);
    window->setScreen(secondary);
    const QRect geo = secondary->availableGeometry();
    window->move(geo.topLeft() + QPoint(40, 40));
}
