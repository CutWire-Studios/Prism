#pragma once

#include <QByteArray>
#include <QString>

/// Buffers live mic PCM keyed by master output device, mixed into deck playback.
class AudioInputMixRegistry {
public:
    static QString deviceKey(const QString &outputDeviceId);

    static void appendPcm(const QString &outputDeviceId, const QByteArray &pcm);
    static void mixIntoPlaybackChunk(const QString &outputDeviceId, QByteArray &chunk);
    static void clearDevice(const QString &outputDeviceId);
    static void clearAll();
};
