#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class ScreenSource;

// Backend for the inspector and ProcessEditor: node lookups and the source
// pickers (cameras, NDI, screen capture portal) that the old ClipCard dialogs ran inline.
class ClipEditTools : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ClipEditTools(QObject *parent = nullptr);
    ~ClipEditTools() override;

    /// ClipGraph::NodeType value, -1 for an unknown id.
    Q_INVOKABLE int nodeType(quint64 id) const;
    Q_INVOKABLE QVariantMap nodeParams(quint64 id) const;
    /// The Input node feeding a Process node's input (its reference frame), 0 when unwired.
    Q_INVOKABLE quint64 referenceInput(quint64 processId) const;

    Q_INVOKABLE double mediaDuration(const QString &path) const;
    Q_INVOKABLE QStringList slideshowEffects() const;

    /// [{id, label, index, isDefault}], default camera first.
    Q_INVOKABLE QVariantList cameraDevices() const;
    Q_INVOKABLE bool ndiAvailable() const;
    /// Blocks up to two seconds while the network is browsed.
    Q_INVOKABLE QStringList ndiSources() const;

    /// Linux only: runs the portal picker and stores the new capture id on the node.
    Q_INVOKABLE bool captureSupported() const;
    Q_INVOKABLE void pickCapture(quint64 nodeId);

    /// Regenerates the node thumbnail for kinds whose look follows their descriptor (Canvas).
    Q_INVOKABLE void refreshThumbnail(quint64 nodeId);

signals:
    void captureFinished(quint64 nodeId, bool ok);

private:
    QPointer<ScreenSource> m_picker;
};
