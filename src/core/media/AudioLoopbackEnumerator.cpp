#include "core/media/AudioLoopbackEnumerator.h"

#include <QMediaDevices>
#include <QObject>

#include <cmath>

namespace AudioLoopbackEnumerator {

namespace {

QString normalizeLabel(const QString &label) {
    return label.simplified().toLower();
}

bool isMonitorInputDevice(const QAudioDevice &dev) {
    const QString id = QString::fromUtf8(dev.id());
    const QString desc = dev.description();
    if (id.contains(QStringLiteral(".monitor"), Qt::CaseInsensitive))
        return true;
    if (desc.contains(QStringLiteral("Monitor of"), Qt::CaseInsensitive))
        return true;
    if (id.startsWith(QStringLiteral("alsa_output.")) && id.endsWith(QStringLiteral(".monitor")))
        return true;
    return false;
}

bool isMicrophoneLikeInput(const QAudioDevice &dev) {
    const QString id = QString::fromUtf8(dev.id());
    if (isMonitorInputDevice(dev))
        return false;
    if (id.startsWith(QStringLiteral("alsa_input.")))
        return true;
    if (id.contains(QStringLiteral("input"), Qt::CaseInsensitive)
        && !id.contains(QStringLiteral("monitor"), Qt::CaseInsensitive))
        return true;
    return false;
}

QAudioDevice outputDeviceForId(const QString &outputDeviceId) {
    if (outputDeviceId.isEmpty())
        return QMediaDevices::defaultAudioOutput();
    for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
        if (QString::fromUtf8(dev.id()) == outputDeviceId)
            return dev;
    }
    return {};
}

} // namespace

float pcmRms(const QByteArray &pcm) {
    if (pcm.isEmpty())
        return 0.f;
    const auto *data = reinterpret_cast<const float *>(pcm.constData());
    const int sampleCount = pcm.size() / static_cast<int>(sizeof(float));
    if (sampleCount <= 0)
        return 0.f;
    double sumSq = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        const double s = data[i];
        sumSq += s * s;
    }
    return static_cast<float>(std::sqrt(sumSq / sampleCount));
}

QString monitorSourceIdForPlayback(const QString &outputDeviceId) {
    const QAudioDevice output = outputDeviceForId(outputDeviceId);
    if (output.isNull())
        return {};

    QString id = QString::fromUtf8(output.id());
    if (id.contains(QStringLiteral(".monitor"), Qt::CaseInsensitive))
        return id;
    if (id.startsWith(QStringLiteral("alsa_output.")))
        return id + QStringLiteral(".monitor");
    return id + QStringLiteral(".monitor");
}

QString sinkNodeIdForPlayback(const QString &outputDeviceId) {
    const QAudioDevice output = outputDeviceForId(outputDeviceId);
    if (output.isNull())
        return {};
    return QString::fromUtf8(output.id());
}

QList<AudioOutputDeviceInfo> listPlaybackDevices()
{
    QList<AudioOutputDeviceInfo> devices;
    const QAudioDevice defaultOut = QMediaDevices::defaultAudioOutput();
    const QString defaultId = defaultOut.isNull() ? QString() : QString::fromUtf8(defaultOut.id());

    if (!defaultOut.isNull()) {
        devices.append({QString(), QObject::tr("System Default Output (%1)")
                            .arg(defaultOut.description()), true});
    } else {
        devices.append({QString(), QObject::tr("System Default Output"), true});
    }

    for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
        const QString id = QString::fromUtf8(dev.id());
        if (!defaultId.isEmpty() && id == defaultId)
            continue;
        devices.append({id, dev.description(), false});
    }
    return devices;
}

QAudioDevice monitorForPlaybackDevice(const QString &outputDeviceId)
{
    const QString monitorId = monitorSourceIdForPlayback(outputDeviceId);
    if (monitorId.isEmpty())
        return {};

    for (const QAudioDevice &in : QMediaDevices::audioInputs()) {
        if (QString::fromUtf8(in.id()) == monitorId && isMonitorInputDevice(in))
            return in;
    }

    for (const QAudioDevice &in : QMediaDevices::audioInputs()) {
        if (!isMonitorInputDevice(in) || isMicrophoneLikeInput(in))
            continue;

        const QString desc = in.description();
        const QAudioDevice output = outputDeviceForId(outputDeviceId);
        if (output.isNull())
            continue;

        const QString outDesc = output.description();
        const QString prefix = QStringLiteral("Monitor of ");
        const QString expectedMonitor = prefix + outDesc;
        if (normalizeLabel(desc) == normalizeLabel(expectedMonitor))
            return in;
        if (desc.contains(outDesc, Qt::CaseInsensitive))
            return in;
    }

    return {};
}

QString monitorLabelForPlaybackDevice(const QString &outputDeviceId) {
    const QAudioDevice monitor = monitorForPlaybackDevice(outputDeviceId);
    if (!monitor.isNull())
        return monitor.description();

    const QString monitorId = monitorSourceIdForPlayback(outputDeviceId);
    if (monitorId.isEmpty())
        return {};
    return QObject::tr("Monitor: %1").arg(monitorId);
}

bool pcmHasSignal(const QByteArray &pcm, float threshold) {
    return pcmRms(pcm) >= threshold;
}

bool LiveAudioGate::accept(float rms, qint64 nowMs) {
    if (!m_open) {
        if (rms >= kOpenThreshold) {
            m_open = true;
            m_belowSinceMs = 0;
        }
        return m_open;
    }

    if (rms >= kCloseThreshold) {
        m_belowSinceMs = 0;
        return true;
    }

    if (m_belowSinceMs == 0)
        m_belowSinceMs = nowMs;
    if (nowMs - m_belowSinceMs >= kCloseHoldMs)
        m_open = false;
    return m_open;
}

} // namespace AudioLoopbackEnumerator
