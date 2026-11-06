#pragma once

#include <QMatrix4x4>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Backend for LayerLayoutEditor and the keystone preview. Layer edits are applied straight to
// the ClipGraph (live, like the old dialog); the editor reverts them on cancel.
class TransformTools : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit TransformTools(QObject *parent = nullptr);

    /// Topmost layer first: [{slot, name, visible, sourceNode, x, y, w, h}] with a normalized rect.
    Q_INVOKABLE QVariantList layerSlots(quint64 layerId) const;
    Q_INVOKABLE bool setSlotRect(quint64 layerId, int slot, qreal x, qreal y, qreal w, qreal h);
    Q_INVOKABLE bool setSlotVisible(quint64 layerId, int slot, bool visible);
    Q_INVOKABLE QSize canvasSize(quint64 layerId) const;
    Q_INVOKABLE bool setCanvasSize(quint64 layerId, int w, int h);
    Q_INVOKABLE bool canvasCustom(quint64 layerId) const;
    Q_INVOKABLE bool setCanvasCustom(quint64 layerId, bool custom);

    /// Projective map taking a w x h frame to the quad given by four normalized [x, y] corners.
    Q_INVOKABLE QMatrix4x4 quadMatrix(qreal w, qreal h, const QVariantList &corners) const;
};
