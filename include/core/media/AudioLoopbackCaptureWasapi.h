#pragma once

#include <QByteArray>
#include <QString>

namespace AudioLoopbackWasapi {

/// Starts WASAPI loopback capture of the render endpoint identified by
/// `outputDeviceId` (expected to be QAudioDevice::id() for a Windows
/// playback device, which the Qt Windows multimedia backend derives from
/// IMMDevice::GetId()). Empty id = system default output. Falls back to the
/// default render endpoint if the id can't be resolved.
bool start(const QString &outputDeviceId);
void stop();
bool isRunning();
/// Pulls available PCM, resampled to float32 stereo interleaved at 44.1kHz
/// (may return false if no data is ready yet).
bool pull(QByteArray &pcmOut);

} // namespace AudioLoopbackWasapi
