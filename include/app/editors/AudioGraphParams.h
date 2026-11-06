#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "core/graph/ClipGraphModels.h"

// Editors are opened with only a node id, and QML cannot read a node's params from the model by id.
class AudioGraphParams : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ClipGraphController *controller MEMBER m_controller)

public:
    explicit AudioGraphParams(QObject *parent = nullptr);

    Q_INVOKABLE QVariantMap params(quint64 nodeId) const;

private:
    ClipGraphController *m_controller = nullptr;
};
