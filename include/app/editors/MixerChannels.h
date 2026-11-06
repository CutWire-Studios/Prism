#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "core/graph/ClipGraphModels.h"

// Connected input channels of an Audio Mixer node, and writing their volume/mute/name back.
class MixerChannels : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ClipGraphController *controller MEMBER m_controller)

public:
    explicit MixerChannels(QObject *parent = nullptr);

    // [{index, name, volume, muted}] for every connected input slot.
    Q_INVOKABLE QVariantList channels(quint64 nodeId) const;
    // rows as returned by channels(); unconnected slots are left untouched.
    Q_INVOKABLE bool apply(quint64 nodeId, const QVariantList &rows);

private:
    ClipGraphController *m_controller = nullptr;
};
