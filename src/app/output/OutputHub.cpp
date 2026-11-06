#include "app/output/OutputHub.h"
#include "app/output/NdiProgramSink.h"
#include "core/render/Compositor.h"
#include <QGuiApplication>
#include <QScreen>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QUrl>
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
    , m_liveAudioRecorder(std::make_unique<ProgramAudioRecorder>(this))
    , m_stagingAudioRecorder(std::make_unique<ProgramAudioRecorder>(this))
    , m_liveRecorder(std::make_unique<ProgramRecorder>(this))
    , m_stagingRecorder(std::make_unique<ProgramRecorder>(this))
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
    connectRecorder(m_liveRecorder.get());
    connectRecorder(m_stagingRecorder.get());
    if (m_programAudioRecorder) {
        connect(m_programAudioRecorder.get(), &ProgramAudioRecorder::errorOccurred,
                this, &OutputHub::recordingError);
    }
    auto connectAudioRecorder = [this](ProgramAudioRecorder *rec) {
        if (!rec) return;
        connect(rec, &ProgramAudioRecorder::errorOccurred, this, &OutputHub::recordingError);
    };
    connectAudioRecorder(m_liveAudioRecorder.get());
    connectAudioRecorder(m_stagingAudioRecorder.get());

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
        QImage program, live, staging;
        {
            QMutexLocker lk(&m_mailboxMutex);
            while (!m_frameDirty && !m_dispatchStop)
                m_mailboxCv.wait(&m_mailboxMutex);
            if (m_dispatchStop)
                return;
            program = std::move(m_pendingProgram);
            live   = std::move(m_pendingLive);
            staging   = std::move(m_pendingStaging);
            m_pendingProgram = QImage();
            m_pendingLive   = QImage();
            m_pendingStaging   = QImage();
            m_frameDirty = false;
        }
        QMutexLocker sink(&m_sinkMutex);
        distributeFrames(program, live, staging);
    }
}

// Hands a frame still waiting in the mailbox to the sinks. Called with m_sinkMutex held before a
// video recorder stops, so the last captured frame (or the only one, in a short take) is written
// rather than dropped with the mailbox.
void OutputHub::deliverPendingFrames() {
    QImage program, live, staging;
    {
        QMutexLocker lk(&m_mailboxMutex);
        if (!m_frameDirty)
            return;
        program = std::move(m_pendingProgram);
        live   = std::move(m_pendingLive);
        staging   = std::move(m_pendingStaging);
        m_pendingProgram = QImage();
        m_pendingLive   = QImage();
        m_pendingStaging   = QImage();
        m_frameDirty = false;
    }
    distributeFrames(program, live, staging);
}

void OutputHub::distributeFrames(const QImage &programFrame,
                                 const QImage &liveFrame,
                                 const QImage &stagingFrame) {
    if (m_ndiEnabled && m_ndiSink && m_ndiSink->isActive() && !programFrame.isNull())
        m_ndiSink->submitFrame(programFrame);

    if (m_virtualCameraEnabled && m_virtualCameraSink && m_virtualCameraSink->isActive()
        && !programFrame.isNull())
        m_virtualCameraSink->submitFrame(programFrame);

    if (m_programRecorder && m_programRecorder->isRecording() && !programFrame.isNull())
        m_programRecorder->submitFrame(programFrame);

    if (m_liveRecorder && m_liveRecorder->isRecording() && !liveFrame.isNull())
        m_liveRecorder->submitFrame(liveFrame);

    if (m_stagingRecorder && m_stagingRecorder->isRecording() && !stagingFrame.isNull())
        m_stagingRecorder->submitFrame(stagingFrame);

    for (const auto &entry : m_sourceRecorders) {
        const NodeId nodeId = entry.first;
        ProgramRecorder *rec = entry.second.get();
        if (!rec || !rec->isRecording()) continue;

        const bool onLive = nodeId != 0 && nodeId == m_activeLive;
        const bool onStaging = nodeId != 0 && nodeId == m_activeStaging;
        if (!onLive && !onStaging) continue;

        const QImage &srcFrame = onLive ? liveFrame : stagingFrame;
        if (!srcFrame.isNull())
            rec->submitFrame(srcFrame);
    }
}

void OutputHub::setOutputDir(const QString &dir) {
    m_outputDir = dir.isEmpty() ? ProgramRecorder::defaultOutputDir() : dir;
}

void OutputHub::setProgramSource(Compositor *source) {
    if (m_compositor)
        disconnect(m_compositor, nullptr, this, nullptr);
    m_compositor = source;
    m_frameSource = source;
    if (!m_frameSource) return;

    connect(m_compositor, &Compositor::programFrameReady,
            this, &OutputHub::onProgramFrameReady);

    syncFrameConsumers();
}

void OutputHub::setProgramSourceForTest(ProgramFrameSource *source) {
    m_frameSource = source;
}

Compositor *OutputHub::programSource() const {
    return m_compositor.data();
}

void OutputHub::setActiveDeckNodes(NodeId live, NodeId staging) {
    QMutexLocker locker(&m_sinkMutex);
    m_activeLive = live;
    m_activeStaging = staging;
}

void OutputHub::setQmlEngine(QQmlEngine *engine) {
    m_engine = engine;
}

QQuickWindow *OutputHub::addMirrorOutput(const QString &title) {
    if (!m_engine) {
        qWarning("OutputHub: no QML engine set; cannot open program output window");
        return nullptr;
    }

    QQmlComponent component(m_engine, QUrl(QStringLiteral(
        "qrc:/qt/qml/Prism/src/qml/components/preview/ProgramOutputWindow.qml")));
    QObject *obj = component.create();
    if (!obj) {
        qWarning().noquote() << "OutputHub: failed to create program output window:"
                             << component.errorString();
        return nullptr;
    }
    auto *window = qobject_cast<QQuickWindow *>(obj);
    if (!window) {
        qWarning("OutputHub: ProgramOutputWindow.qml root is not a window");
        delete obj;
        return nullptr;
    }
    window->QObject::setParent(this);

    if (!title.isEmpty())
        window->setTitle(title);

    connect(window, &QObject::destroyed, this, &OutputHub::onMirrorDestroyed);

    m_mirrors.append(window);
    placeOnSecondaryScreen(window);
    window->show();
    return window;
}

void OutputHub::closeMirrorOutputs() {
    const auto mirrors = m_mirrors;
    m_mirrors.clear();
    for (const auto &mirror : mirrors) {
        if (mirror)
            mirror->deleteLater();
    }
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
    case TrackKind::Live:   return m_liveRecorder.get();
    case TrackKind::Staging:   return m_stagingRecorder.get();
    case TrackKind::ProgramAudio:
    case TrackKind::LiveAudio:
    case TrackKind::StagingAudio:
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
    case TrackKind::LiveAudio:   return m_liveAudioRecorder.get();
    case TrackKind::StagingAudio:   return m_stagingAudioRecorder.get();
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
    if (m_liveAudioRecorder && m_liveAudioRecorder->isRecording()) return true;
    if (m_stagingAudioRecorder && m_stagingAudioRecorder->isRecording()) return true;
    for (const auto &entry : m_clipAudioRecorders) {
        if (entry.second && entry.second->isRecording()) return true;
    }
    if (m_liveRecorder && m_liveRecorder->isRecording()) return true;
    if (m_stagingRecorder && m_stagingRecorder->isRecording()) return true;
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
    consider(m_liveRecorder.get());
    consider(m_stagingRecorder.get());
    if (m_programAudioRecorder && m_programAudioRecorder->isRecording())
        maxMs = qMax(maxMs, m_programAudioRecorder->recordingDurationMs());
    if (m_liveAudioRecorder && m_liveAudioRecorder->isRecording())
        maxMs = qMax(maxMs, m_liveAudioRecorder->recordingDurationMs());
    if (m_stagingAudioRecorder && m_stagingAudioRecorder->isRecording())
        maxMs = qMax(maxMs, m_stagingAudioRecorder->recordingDurationMs());
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
    if (m_liveAudioRecorder && m_liveAudioRecorder->isRecording())
        labels << tr("Live audio");
    if (m_stagingAudioRecorder && m_stagingAudioRecorder->isRecording())
        labels << tr("Staging audio");
    for (const auto &entry : m_clipAudioRecorders) {
        if (entry.second && entry.second->isRecording())
            labels << entry.second->trackLabel();
    }
    if (m_liveRecorder && m_liveRecorder->isRecording())
        labels << tr("Live");
    if (m_stagingRecorder && m_stagingRecorder->isRecording())
        labels << tr("Staging");
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
    if (!m_programRecorder->startRecording(path, tr("Program"), true,
                                       Compositor::programWidth(), Compositor::programHeight()))
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
    deliverPendingFrames();
    if (!m_programRecorder->isRecording()) return;
    m_programRecorder->stopRecording();
    syncFrameConsumers();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startDeckLiveRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_frameSource || m_liveRecorder->isRecording()) return m_liveRecorder->isRecording();
    const QString path = makeTrackOutputPath(QStringLiteral("live"));
    if (!m_liveRecorder->startRecording(path, tr("Live"), true,
                                     Compositor::programWidth(), Compositor::programHeight()))
        return false;
    m_liveRecorder->addMarker(tr("Recording started"));
    syncFrameConsumers();
    if (m_frameSource)
        m_frameSource->captureOutputFrameNow();
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopDeckLiveRecording() {
    QMutexLocker locker(&m_sinkMutex);
    deliverPendingFrames();
    if (!m_liveRecorder->isRecording()) return;
    m_liveRecorder->stopRecording();
    syncFrameConsumers();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startDeckStagingRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_frameSource || m_stagingRecorder->isRecording()) return m_stagingRecorder->isRecording();
    const QString path = makeTrackOutputPath(QStringLiteral("staging"));
    if (!m_stagingRecorder->startRecording(path, tr("Staging"), true,
                                     Compositor::programWidth(), Compositor::programHeight()))
        return false;
    m_stagingRecorder->addMarker(tr("Recording started"));
    syncFrameConsumers();
    if (m_frameSource)
        m_frameSource->captureOutputFrameNow();
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopDeckStagingRecording() {
    QMutexLocker locker(&m_sinkMutex);
    deliverPendingFrames();
    if (!m_stagingRecorder->isRecording()) return;
    m_stagingRecorder->stopRecording();
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
    if (!rec->startRecording(path, trackLabel, true,
                        Compositor::programWidth(), Compositor::programHeight()))
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
    deliverPendingFrames();
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

    if (deckIndex == 0 && m_liveAudioRecorder && m_liveAudioRecorder->isRecording())
        m_liveAudioRecorder->submitPcm(pcm);
    if (deckIndex == 1 && m_stagingAudioRecorder && m_stagingAudioRecorder->isRecording())
        m_stagingAudioRecorder->submitPcm(pcm);

    const NodeId activeClip = deckIndex == 0 ? m_activeLive : m_activeStaging;
    if (activeClip == 0 || activeClip != clipId)
        return;

    const auto it = m_clipAudioRecorders.find(clipId);
    if (it != m_clipAudioRecorders.end() && it->second && it->second->isRecording())
        it->second->submitPcm(pcm);
}

bool OutputHub::startDeckLiveAudioRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_liveAudioRecorder || m_liveAudioRecorder->isRecording())
        return m_liveAudioRecorder && m_liveAudioRecorder->isRecording();

    const QString path = makeAudioTrackOutputPath(QStringLiteral("live"));
    if (!m_liveAudioRecorder->startRecording(path, tr("Live audio"), true))
        return false;

    m_liveAudioRecorder->addMarker(tr("Recording started"));
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopDeckLiveAudioRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_liveAudioRecorder || !m_liveAudioRecorder->isRecording())
        return;
    m_liveAudioRecorder->stopRecording();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startDeckStagingAudioRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_stagingAudioRecorder || m_stagingAudioRecorder->isRecording())
        return m_stagingAudioRecorder && m_stagingAudioRecorder->isRecording();

    const QString path = makeAudioTrackOutputPath(QStringLiteral("staging"));
    if (!m_stagingAudioRecorder->startRecording(path, tr("Staging audio"), true))
        return false;

    m_stagingAudioRecorder->addMarker(tr("Recording started"));
    ensureProgressTimer();
    emit recordingStateChanged();
    return true;
}

void OutputHub::stopDeckStagingAudioRecording() {
    QMutexLocker locker(&m_sinkMutex);
    if (!m_stagingAudioRecorder || !m_stagingAudioRecorder->isRecording())
        return;
    m_stagingAudioRecorder->stopRecording();
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
    deliverPendingFrames();
    if (m_programRecorder && m_programRecorder->isRecording())
        m_programRecorder->stopRecording();
    if (m_programAudioRecorder && m_programAudioRecorder->isRecording())
        m_programAudioRecorder->stopRecording();
    if (m_liveAudioRecorder && m_liveAudioRecorder->isRecording())
        m_liveAudioRecorder->stopRecording();
    if (m_stagingAudioRecorder && m_stagingAudioRecorder->isRecording())
        m_stagingAudioRecorder->stopRecording();
    for (auto it = m_clipAudioRecorders.begin(); it != m_clipAudioRecorders.end(); ) {
        if (it->second && it->second->isRecording())
            it->second->stopRecording();
        it = m_clipAudioRecorders.erase(it);
    }
    if (m_liveRecorder && m_liveRecorder->isRecording())
        m_liveRecorder->stopRecording();
    if (m_stagingRecorder && m_stagingRecorder->isRecording())
        m_stagingRecorder->stopRecording();
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
    mark(m_liveRecorder.get());
    mark(m_stagingRecorder.get());
    for (auto &entry : m_sourceRecorders)
        mark(entry.second.get());
    auto markAudio = [&](ProgramAudioRecorder *rec) {
        if (rec && rec->isRecording())
            rec->addMarker(label);
    };
    markAudio(m_programAudioRecorder.get());
    markAudio(m_liveAudioRecorder.get());
    markAudio(m_stagingAudioRecorder.get());
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
    const QImage liveFrame = needDeck ? m_frameSource->deckProgramFrame(Deck::Live)    : QImage();
    const QImage stagingFrame = needDeck ? m_frameSource->deckProgramFrame(Deck::Staging) : QImage();

    // Hand the heavy sinks (NDI / virtual camera / recorders) to the dispatch
    // thread. Keep only the most recent frame set (drop-old) to stay realtime.
    QMutexLocker lk(&m_mailboxMutex);
    m_pendingProgram = programFrame;
    m_pendingLive   = liveFrame;
    m_pendingStaging   = stagingFrame;
    m_frameDirty = true;
    m_mailboxCv.wakeOne();
}

void OutputHub::onMirrorDestroyed(QObject *obj) {
    m_mirrors.removeAll(static_cast<QQuickWindow *>(obj));
    m_mirrors.removeAll(nullptr);
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
    if (m_ndiEnabled)
        ++count;
    if (m_virtualCameraEnabled)
        ++count;
    if (m_programRecorder && m_programRecorder->isRecording())
        ++count;
    return count;
}

bool OutputHub::needsDeckFrameReadback() const {
    if (m_liveRecorder && m_liveRecorder->isRecording()) return true;
    if (m_stagingRecorder && m_stagingRecorder->isRecording()) return true;
    for (const auto &entry : m_sourceRecorders) {
        if (entry.second && entry.second->isRecording()) return true;
    }
    return false;
}

void OutputHub::placeOnSecondaryScreen(QQuickWindow *window) {
    const auto screens = QGuiApplication::screens();
    QScreen *screen = screens.size() < 2 ? window->screen() : screens.at(1);
    if (screens.size() >= 2)
        window->setScreen(screen);
    const QRect geo = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    const QSize box = QSize(960, 960).boundedTo(geo.size() - QSize(80, 80));
    const QSize size = QSize(Compositor::programWidth(), Compositor::programHeight())
                           .scaled(box, Qt::KeepAspectRatio);
    window->setGeometry(QRect(geo.topLeft() + QPoint(40, 40), size));
}
