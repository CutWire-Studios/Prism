#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Read-only facts the node editor view needs from the core: the effect registries, the port
// compatibility table and whether script nodes are compiled in.
class NodeCatalog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit NodeCatalog(QObject *parent = nullptr);

    /// [{id, name, menuLabel, hasEditor, editLabel}] for effects that are offered in menus.
    Q_INVOKABLE QVariantList processEffects() const;
    Q_INVOKABLE QVariantList audioEffects() const;

    /// Button text for a Process / AudioEffect node (dynamic label of its current params).
    /// nodeType is a ClipGraph::NodeType; params is the node's params map.
    Q_INVOKABLE QString effectLabel(int nodeType, const QVariantMap &params) const;
    Q_INVOKABLE bool effectHasEditor(int nodeType, int effect) const;

    /// ClipGraph::PortKind values.
    Q_INVOKABLE bool portsCompatible(int a, int b) const;
    Q_INVOKABLE bool scriptingAvailable() const;
};
