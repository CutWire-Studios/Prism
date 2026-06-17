#include "ui/OutputHub.h"
#include "ui/MirrorOutputWindow.h"
#include "ui/NdiProgramSink.h"
#include "ui/VideoWidget.h"
#include <QGuiApplication>
#include <QScreen>

OutputHub::OutputHub(QObject *parent)
    : QObject(parent)
    , m_ndiSink(std::make_unique<NdiProgramSink>())
    , m_virtualCameraSink(std::make_unique<VirtualCameraProgramSink>())
    , m_recorder(std::make_unique<ProgramRecorder>(this))
{
    connect(m_recorder.get(), &ProgramRecorder::recordingChanged,
            this, &OutputHub::programRecordingChanged);
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

bool OutputHub::isProgramRecording() const {
    return m_recorder && m_recorder->isRecording();
}

QString OutputHub::recordingOutputPath() const {
    return m_recorder ? m_recorder->outputPath() : QString{};
}

QString OutputHub::recordingMarkersPath() const {
    return m_recorder ? m_recorder->markersPath() : QString{};
}

bool OutputHub::setProgramRecordingEnabled(bool enabled, const QString &outputPath) {
    if (!m_recorder) return false;

    if (enabled == m_recorder->isRecording())
        return true;

    if (enabled) {
        if (!m_recorder->startRecording(outputPath))
            return false;
        m_recorder->addMarker(tr("Recording started"));
    } else {
        m_recorder->stopRecording();
    }

    syncFrameConsumers();
    return true;
}

void OutputHub::addRecordingMarker(const QString &label) {
    if (m_recorder && m_recorder->isRecording())
        m_recorder->addMarker(label);
}

void OutputHub::onProgramFrameReady() {
    if (!m_source) return;

    const QImage frame = m_source->programFrame();

    if (!frame.isNull()) {
        for (const auto &mirror : m_mirrors) {
            if (mirror)
                mirror->setFrame(frame);
        }
    }

    if (m_ndiEnabled && m_ndiSink && m_ndiSink->isActive() && !frame.isNull())
        m_ndiSink->submitFrame(frame);

    if (m_virtualCameraEnabled && m_virtualCameraSink && m_virtualCameraSink->isActive() && !frame.isNull())
        m_virtualCameraSink->submitFrame(frame);

    if (m_recorder && m_recorder->isRecording() && !frame.isNull())
        m_recorder->submitFrame(frame);
}

void OutputHub::onMirrorDestroyed(QObject *obj) {
    m_mirrors.removeAll(static_cast<MirrorOutputWindow *>(obj));
    syncFrameConsumers();
}

void OutputHub::syncFrameConsumers() {
    if (m_source)
        m_source->setProgramFrameConsumerCount(activeFrameConsumerCount());
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
    if (m_recorder && m_recorder->isRecording())
        ++count;
    return count;
}

void OutputHub::placeOnSecondaryScreen(QWidget *window) {
    const auto screens = QGuiApplication::screens();
    if (screens.size() < 2) return;

    QScreen *secondary = screens.at(1);
    window->setScreen(secondary);
    const QRect geo = secondary->availableGeometry();
    window->move(geo.topLeft() + QPoint(40, 40));
}
