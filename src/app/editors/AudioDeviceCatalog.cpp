#include "app/editors/AudioDeviceCatalog.h"

#include "ui/common/AudioLoopbackEnumerator.h"

#include <QAudioDevice>
#include <QMediaDevices>

AudioDeviceCatalog::AudioDeviceCatalog(QObject *parent)
    : QObject(parent)
{
}

QVariantList AudioDeviceCatalog::inputDevices() const
{
    QVariantList out;
    out.append(QVariantMap{
        {QStringLiteral("id"), QString()},
        {QStringLiteral("label"), tr("System Default Mic")},
    });
    for (const QAudioDevice &dev : QMediaDevices::audioInputs()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), QString::fromUtf8(dev.id())},
            {QStringLiteral("label"), dev.description()},
        });
    }
    return out;
}

QVariantList AudioDeviceCatalog::playbackDevices() const
{
    QVariantList out;
    for (const AudioOutputDeviceInfo &dev : AudioLoopbackEnumerator::listPlaybackDevices()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), dev.id},
            {QStringLiteral("label"), dev.label},
        });
    }
    return out;
}

QString AudioDeviceCatalog::captureHint(const QString &playbackId) const
{
    const QString monitor = AudioLoopbackEnumerator::monitorLabelForPlaybackDevice(playbackId);
    if (monitor.isEmpty()) {
        return tr("Captures audio playing on the selected output device. "
                  "No monitor source was found for this device — pick the device "
                  "that is actually playing sound (speakers vs headset).");
    }
    return tr("Captures audio playing on the selected output device via:\n%1").arg(monitor);
}
