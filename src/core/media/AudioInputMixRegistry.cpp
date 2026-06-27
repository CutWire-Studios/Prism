#include "core/media/AudioInputMixRegistry.h"

#include <QHash>

#include <algorithm>
#include <cstring>

namespace {

QHash<QString, QByteArray> &pendingBuffers() {
    static QHash<QString, QByteArray> buffers;
    return buffers;
}

}

QString AudioInputMixRegistry::deviceKey(const QString &outputDeviceId) {
    return outputDeviceId.isEmpty() ? QStringLiteral("__default__") : outputDeviceId;
}

void AudioInputMixRegistry::appendPcm(const QString &outputDeviceId, const QByteArray &pcm) {
    if (pcm.isEmpty()) return;
    pendingBuffers()[deviceKey(outputDeviceId)].append(pcm);
}

void AudioInputMixRegistry::mixIntoPlaybackChunk(const QString &outputDeviceId, QByteArray &chunk) {
    if (chunk.isEmpty()) return;

    QByteArray &pending = pendingBuffers()[deviceKey(outputDeviceId)];
    if (pending.isEmpty()) return;

    const int mixBytes = std::min(pending.size(), chunk.size());
    auto *out = reinterpret_cast<float *>(chunk.data());
    const auto *in = reinterpret_cast<const float *>(pending.constData());
    const int sampleCount = mixBytes / static_cast<int>(sizeof(float));
    for (int i = 0; i < sampleCount; ++i)
        out[i] = std::clamp(out[i] + in[i], -1.0f, 1.0f);

    if (mixBytes >= pending.size())
        pending.clear();
    else
        pending.remove(0, mixBytes);
}

void AudioInputMixRegistry::clearDevice(const QString &outputDeviceId) {
    pendingBuffers().remove(deviceKey(outputDeviceId));
}

void AudioInputMixRegistry::clearAll() {
    pendingBuffers().clear();
}
