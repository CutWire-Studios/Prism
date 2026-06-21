#include "ui/output/OutputHub.h"
#include "ui/output/MirrorOutputWindow.h"
#include "ui/output/NdiProgramSink.h"
#include "ui/canvas/VideoWidget.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>

OutputHub::OutputHub(QObject *parent)
    : QObject(parent)
    , m_ndiSink(std::make_unique<NdiProgramSink>())
    , m_virtualCameraSink(std::make_unique<VirtualCameraProgramSink>())
    , m_programRecorder(std::make_unique<ProgramRecorder>(this))
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
}

OutputHub::~OutputHub() {
    stopAllRecording();
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
    case TrackKind::Source: {
        const auto it = m_sourceRecorders.find(sourceNodeId);
        return it != m_sourceRecorders.end() ? it->second.get() : nullptr;
    }
    }
    return nullptr;
}

bool OutputHub::isRecording() const {
    if (m_programRecorder && m_programRecorder->isRecording()) return true;
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
    const ProgramRecorder *rec = recorderFor(kind, sourceNodeId);
    return rec && rec->isRecording();
}

qint64 OutputHub::trackRecordingDurationMs(TrackKind kind, NodeId sourceNodeId) const {
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
    for (const auto &entry : m_sourceRecorders)
        consider(entry.second.get());
    return maxMs;
}

QStringList OutputHub::activeRecordingTrackLabels() const {
    QStringList labels;
    if (m_programRecorder && m_programRecorder->isRecording())
        labels << tr("Program");
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

bool OutputHub::startProgramRecording() {
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
    if (!m_programRecorder->isRecording()) return;
    m_programRecorder->stopRecording();
    syncFrameConsumers();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startDeckARecording() {
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
    if (!m_deckARecorder->isRecording()) return;
    m_deckARecorder->stopRecording();
    syncFrameConsumers();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startDeckBRecording() {
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
    if (!m_deckBRecorder->isRecording()) return;
    m_deckBRecorder->stopRecording();
    syncFrameConsumers();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

bool OutputHub::startSourceRecording(NodeId nodeId, const QString &label) {
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
    const auto it = m_sourceRecorders.find(nodeId);
    if (it == m_sourceRecorders.end() || !it->second->isRecording())
        return;
    it->second->stopRecording();
    m_sourceRecorders.erase(it);
    syncFrameConsumers();
    maybeStopProgressTimer();
    emit recordingStateChanged();
}

void OutputHub::stopAllRecording() {
    if (m_programRecorder && m_programRecorder->isRecording())
        m_programRecorder->stopRecording();
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

    const QImage programFrame = m_frameSource->programFrame();
    const QImage deckAFrame   = needsDeckFrameReadback() ? m_frameSource->deckProgramFrame(true)  : QImage();
    const QImage deckBFrame   = needsDeckFrameReadback() ? m_frameSource->deckProgramFrame(false) : QImage();

    if (!programFrame.isNull()) {
        for (const auto &mirror : m_mirrors) {
            if (mirror)
                mirror->setFrame(programFrame);
        }
    }

    if (m_ndiEnabled && m_ndiSink && m_ndiSink->isActive() && !programFrame.isNull())
        m_ndiSink->submitFrame(programFrame);

    if (m_virtualCameraEnabled && m_virtualCameraSink && m_virtualCameraSink->isActive() && !programFrame.isNull())
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
    if (!m_frameSource) return;
    m_frameSource->setProgramFrameConsumerCount(activeFrameConsumerCount());
    m_frameSource->setDeckFrameConsumerCount(needsDeckFrameReadback() ? 1 : 0);
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
