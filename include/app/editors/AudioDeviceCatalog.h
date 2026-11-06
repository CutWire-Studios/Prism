#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Audio input (mic) and playback (loopback capture) devices for the device pickers.
class AudioDeviceCatalog : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit AudioDeviceCatalog(QObject *parent = nullptr);

    // [{id, label}]; the first entry is the system default with an empty id.
    Q_INVOKABLE QVariantList inputDevices() const;
    // [{id, label}] of the playback devices a capture can follow.
    Q_INVOKABLE QVariantList playbackDevices() const;
    Q_INVOKABLE QString captureHint(const QString &playbackId) const;
};
