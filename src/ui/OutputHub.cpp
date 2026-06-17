#include "ui/OutputHub.h"
#include "ui/MirrorOutputWindow.h"
#include "ui/NdiProgramSink.h"
#include "ui/VideoWidget.h"
#include <QGuiApplication>
#include <QScreen>

OutputHub::OutputHub(QObject *parent)
    : QObject(parent)
    , m_ndiSink(std::make_unique<NdiProgramSink>())
{
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
