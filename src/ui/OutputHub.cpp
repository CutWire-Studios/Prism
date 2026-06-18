#include "ui/OutputHub.h"
#include "ui/MirrorOutputWindow.h"
#include "ui/NdiProgramSink.h"
#include "ui/VideoWidget.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QFileInfo>

OutputHub::OutputHub(QObject *parent)
    : QObject(parent)
    , m_ndiSink(std::make_unique<NdiProgramSink>())
    , m_virtualCameraSink(std::make_unique<VirtualCameraProgramSink>())
    , m_programRecorder(std::make_unique<ProgramRecorder>(this))
    , m_deckARecorder(std::make_unique<ProgramRecorder>(this))
    , m_deckBRecorder(std::make_unique<ProgramRecorder>(this))
    , m_progressTimer(new QTimer(this))
{
    m_progressTimer->setInterval(1000);
    connect(m_progressTimer, &QTimer::timeout, this, &OutputHub::onRecordingProgressTick);
}

OutputHub::~OutputHub() {
    stopRecording();
}

void OutputHub::setProgramSource(VideoWidget *source) {
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (!m_source) return;

    connect(m_source, &VideoWidget::programFrameReady,
            this, &OutputHub::onProgramFrameReady);

    syncFrameConsumers();
}

void OutputHub::setActiveDeckNodes(NodeId deckA, NodeId deckB) {
    m_activeDeckA = deckA;
    m_activeDeckB = deckB;
}

MirrorOutputWindow *OutputHub::addMirrorOutput(const QString &title) {
    if (!m_source) return nullptr;

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

qint64 OutputHub::recordingDurationMs() const {
    if (m_programRecorder && m_programRecorder->isRecording())
        return m_programRecorder->recordingDurationMs();
    if (m_deckARecorder && m_deckARecorder->isRecording())
        return m_deckARecorder->recordingDurationMs();
    if (m_deckBRecorder && m_deckBRecorder->isRecording())
        return m_deckBRecorder->recordingDurationMs();
    for (const auto &entry : m_sourceRecorders) {
        if (entry.second && entry.second->isRecording())
            return entry.second->recordingDurationMs();
    }
    return 0;
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

QStringList OutputHub::recordingOutputPaths() const {
    if (!m_lastRecordingPaths.isEmpty())
        return m_lastRecordingPaths;

    QStringList paths;
    auto appendIfRecording = [&](const ProgramRecorder *rec) {
        if (rec && rec->isRecording() && !rec->outputPath().isEmpty())
            paths << rec->outputPath();
    };
    appendIfRecording(m_programRecorder.get());
    appendIfRecording(m_deckARecorder.get());
    appendIfRecording(m_deckBRecorder.get());
    for (const auto &entry : m_sourceRecorders)
        appendIfRecording(entry.second.get());
    return paths;
}

QString OutputHub::recordingMarkersPath() const {
    return m_combinedMarkersPath;
}

QString OutputHub::sanitizeFileStem(const QString &name) {
    QString stem = name.trimmed();
    if (stem.isEmpty())
        stem = QStringLiteral("source");
    stem.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    stem.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral("_"));
    return stem.left(48);
}

bool OutputHub::startRecording(const RecordingOptions &opts) {
    if (!m_source || !opts.hasAnyTarget())
        return false;

    if (isRecording())
        stopRecording();

    m_recordingOptions = opts;
    const QString dir = opts.outputDir.isEmpty()
        ? ProgramRecorder::defaultOutputDir()
        : opts.outputDir;
    QDir().mkpath(dir);

    m_recordingStem = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    m_combinedMarkersPath = QDir(dir).filePath(m_recordingStem + QStringLiteral(".markers.json"));
    m_lastRecordingPaths.clear();

    auto startTrack = [&](ProgramRecorder *rec, const QString &suffix, const QString &label) -> bool {
        if (!rec) return false;
        const QString path = ProgramRecorder::makeOutputPath(dir, m_recordingStem, suffix);
        if (!rec->startRecording(path, label, false))
            return false;
        rec->addMarker(tr("Recording started"));
        return true;
    };

    if (opts.recordProgram) {
        if (!startTrack(m_programRecorder.get(), QStringLiteral("program"), tr("Program")))
            goto fail;
    }

    if (opts.recordDeckA) {
        if (!startTrack(m_deckARecorder.get(), QStringLiteral("deckA"), tr("Deck A")))
            goto fail;
    }

    if (opts.recordDeckB) {
        if (!startTrack(m_deckBRecorder.get(), QStringLiteral("deckB"), tr("Deck B")))
            goto fail;
    }

    for (const RecordingSourceTarget &target : opts.recordSources) {
        auto rec = std::make_unique<ProgramRecorder>(this);
        const QString suffix = sanitizeFileStem(target.label);
        if (!startTrack(rec.get(), suffix, target.label.isEmpty() ? suffix : target.label)) {
            m_sourceRecorders.clear();
            goto fail;
        }
        m_sourceRecorders[target.nodeId] = std::move(rec);
    }

    syncFrameConsumers();
    m_progressTimer->start();
    emit recordingChanged(true);
    emit recordingProgress(recordingDurationMs());
    return true;

fail:
    if (m_programRecorder && m_programRecorder->isRecording())
        m_programRecorder->stopRecording();
    if (m_deckARecorder && m_deckARecorder->isRecording())
        m_deckARecorder->stopRecording();
    if (m_deckBRecorder && m_deckBRecorder->isRecording())
        m_deckBRecorder->stopRecording();
    for (auto &entry : m_sourceRecorders) {
        if (entry.second && entry.second->isRecording())
            entry.second->stopRecording();
    }
    m_sourceRecorders.clear();
    m_recordingOptions = {};
    m_recordingStem.clear();
    m_combinedMarkersPath.clear();
    syncFrameConsumers();
    return false;
}

void OutputHub::stopRecording() {
    if (!isRecording()) return;

    m_progressTimer->stop();

    m_lastRecordingPaths.clear();
    auto collectPath = [&](const ProgramRecorder *rec) {
        if (rec && !rec->outputPath().isEmpty())
            m_lastRecordingPaths << rec->outputPath();
    };
    collectPath(m_programRecorder.get());
    collectPath(m_deckARecorder.get());
    collectPath(m_deckBRecorder.get());
    for (const auto &entry : m_sourceRecorders)
        collectPath(entry.second.get());

    if (m_programRecorder && m_programRecorder->isRecording())
        m_programRecorder->stopRecording();
    if (m_deckARecorder && m_deckARecorder->isRecording())
        m_deckARecorder->stopRecording();
    if (m_deckBRecorder && m_deckBRecorder->isRecording())
        m_deckBRecorder->stopRecording();
    for (auto &entry : m_sourceRecorders) {
        if (entry.second && entry.second->isRecording())
            entry.second->stopRecording();
    }

    writeCombinedMarkersFile();
    m_sourceRecorders.clear();
    m_recordingOptions = {};
    syncFrameConsumers();
    emit recordingChanged(false);
}

bool OutputHub::setProgramRecordingEnabled(bool enabled, const QString &outputPath) {
    if (enabled) {
        if (isRecording()) return true;
        RecordingOptions opts;
        opts.recordProgram = true;
        if (!outputPath.isEmpty()) {
            QFileInfo fi(outputPath);
            opts.outputDir = fi.absolutePath();
        }
        return startRecording(opts);
    }

    if (isRecording())
        stopRecording();
    return true;
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

void OutputHub::writeCombinedMarkersFile() const {
    if (m_combinedMarkersPath.isEmpty()) return;

    QJsonArray tracksArr;
    auto appendTrack = [&](const ProgramRecorder *rec) {
        if (!rec || rec->outputPath().isEmpty()) return;
        QJsonArray markersArr;
        for (const ProgramRecorder::Marker &m : rec->markers()) {
            QJsonObject o;
            o.insert(QStringLiteral("timeMs"), m.timeMs);
            o.insert(QStringLiteral("label"), m.label);
            markersArr.append(o);
        }
        QJsonObject trackObj;
        trackObj.insert(QStringLiteral("track"), rec->trackLabel());
        trackObj.insert(QStringLiteral("video"), rec->outputPath());
        trackObj.insert(QStringLiteral("durationMs"), rec->recordingDurationMs());
        trackObj.insert(QStringLiteral("markers"), markersArr);
        tracksArr.append(trackObj);
    };

    appendTrack(m_programRecorder.get());
    appendTrack(m_deckARecorder.get());
    appendTrack(m_deckBRecorder.get());
    for (const auto &entry : m_sourceRecorders)
        appendTrack(entry.second.get());

    QJsonObject root;
    root.insert(QStringLiteral("sessionStem"), m_recordingStem);
    qint64 maxDuration = 0;
    for (const QJsonValue &v : tracksArr) {
        const QJsonObject o = v.toObject();
        maxDuration = qMax(maxDuration, static_cast<qint64>(o.value(QStringLiteral("durationMs")).toDouble()));
    }
    root.insert(QStringLiteral("durationMs"), maxDuration);
    root.insert(QStringLiteral("frameRate"), 30);
    root.insert(QStringLiteral("tracks"), tracksArr);

    QFile file(m_combinedMarkersPath);
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void OutputHub::onProgramFrameReady() {
    if (!m_source) return;

    const QImage programFrame = m_source->programFrame();
    const QImage deckAFrame   = needsDeckFrameReadback() ? m_source->deckProgramFrame(true)  : QImage();
    const QImage deckBFrame   = needsDeckFrameReadback() ? m_source->deckProgramFrame(false) : QImage();

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
    emit recordingProgress(recordingDurationMs());
}

void OutputHub::syncFrameConsumers() {
    if (!m_source) return;
    m_source->setProgramFrameConsumerCount(activeFrameConsumerCount());
    m_source->setDeckFrameConsumerCount(needsDeckFrameReadback() ? 1 : 0);
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
