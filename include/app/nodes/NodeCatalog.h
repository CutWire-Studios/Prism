#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

/// One thing the quick-add popup can create.
struct QuickAddEntry {
    QString id;            // stable, e.g. "process:blend", "source:camera", "modulator:lfo"
    QString name;
    QString category;
    QString description;   // one plain-language line
    QStringList keywords;
    int nodeType = 0;      // ClipGraph::NodeType
    QVariantMap params;    // initial node params (effect id, modulator kind)
    QList<int> inKinds;    // ClipGraph::PortKind of the ports a fresh node has
    QList<int> outKinds;
    QString source;        // non-empty for Input sources: the kind AppController::requestAddSource takes
    bool unique = false;   // at most one per graph
};

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
    /// Param descriptors [{name, label, type, min, max, step, def, choices}] of a data-driven shader
    /// effect (id >= 12); empty for other effects. type is float|int|bool|choice|color|point; def is
    /// a number, bool, [r,g,b,a] 0..1 or [x,y].
    Q_INVOKABLE QVariantList processEffectParams(int id) const;
    Q_INVOKABLE QVariantList audioEffects() const;

    /// Button text for a Process / AudioEffect node (dynamic label of its current params).
    /// nodeType is a ClipGraph::NodeType; params is the node's params map.
    Q_INVOKABLE QString effectLabel(int nodeType, const QVariantMap &params) const;
    /// Names of the top-level pedals of an Audio FX node's chain, a split counting as one entry.
    Q_INVOKABLE QStringList pedalChain(const QVariantMap &params) const;
    Q_INVOKABLE bool effectHasEditor(int nodeType, int effect) const;

    /// ClipGraph::PortKind values.
    Q_INVOKABLE bool portsCompatible(int a, int b) const;
    Q_INVOKABLE bool scriptingAvailable() const;

    /// Everything addable, in catalogue order. Built once; descriptions are translated at that point.
    static const QList<QuickAddEntry> &quickAddCatalogue();
    /// Rank of @p entry for @p query, -1 for no match: name prefix > word start in name > substring in
    /// name > category > keyword > description. Case-insensitive.
    static int quickAddScore(const QString &query, const QuickAddEntry &entry);
    /// Entries matching @p query ordered by score, then shorter name, then catalogue order. An empty
    /// query lists everything grouped by category. fromPortKind >= 0 keeps only entries with a port
    /// that can be wired to a dragged port of that kind (fromOutput: the dragged port is an output).
    static QList<QuickAddEntry> quickAddSearch(const QString &query, int fromPortKind = -1,
                                               bool fromOutput = false);
    /// quickAddSearch as [{id, name, category, description, nodeType, params, inKinds, outKinds,
    /// source, unique}] for QML.
    Q_INVOKABLE QVariantList quickAddEntries(const QString &query, int fromPortKind = -1,
                                             bool fromOutput = false) const;

    /// Keys of the last few entries picked in the add-node palette, newest first (QSettings).
    Q_INVOKABLE QStringList recentNodeKeys() const;
    Q_INVOKABLE void noteNodeAdded(const QString &key);
};
