#pragma once

#include <QObject>
#include <QMap>
#include <QList>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <Qt>
#include "core/graph/ClipGraph.h"

/// Manages keyboard shortcut assignments (VJ hotkey grid) for the inputs of
/// the Switcher nodes. Keys 1-0, Q-P, A-L, Z-M are auto-assigned to
/// connected switcher inputs in order. Bare key stages the input,
/// Shift+key stages it and cuts to Live.
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(ClipGraph *graph, QObject *parent = nullptr);

    /// Re-derive bindings from the node graph: release keys whose switcher
    /// input is gone and auto-assign keys to newly connected inputs.
    /// Connect to ClipGraph::changed.
    void syncWithGraph();

    /// Replace all bindings atomically (used by the hotkey editor).
    void applyBindings(const QMap<ClipGraph::AbSlotRef, Qt::Key> &bindings);

    /// Assign a single binding; returns false if the key is taken by another input.
    bool setBinding(const ClipGraph::AbSlotRef &slot, Qt::Key key);
    void clearBinding(const ClipGraph::AbSlotRef &slot);

    Qt::Key   bindingForSlot(const ClipGraph::AbSlotRef &slot) const;
    ClipGraph::AbSlotRef slotForKey(Qt::Key key) const;

    /// Session persistence.
    QJsonArray hotkeysJson() const;
    /// Restore from a session; accepts both the current {abNode, slot, key}
    /// entries and legacy {nodeId, key} clip bindings (mapped to the switcher
    /// input the clip feeds). Unbound inputs are then auto-assigned.
    void restoreHotkeys(const QJsonArray &hotkeys);

    static const QList<Qt::Key> &hotkeySequence();
    static bool isBindableKey(Qt::Key key);
    static QString slotSettingsKey(const ClipGraph::AbSlotRef &slot);

    /// Portable profile (switcher input → key), for import/export and QSettings.
    QJsonObject exportProfile() const;
    bool importProfile(const QJsonObject &profile, QStringList *warnings = nullptr);

    void saveSettingsProfile() const;
    void loadSettingsProfile();

signals:
    void bindingsChanged();
    void slotTriggered(const ClipGraph::AbSlotRef &ref, bool cutToLive);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ClipGraph *m_graph;

    QMap<ClipGraph::AbSlotRef, Qt::Key>       m_slotHotkeys;
    QMap<Qt::Key,   ClipGraph::AbSlotRef>     m_keyToSlot;
    QMap<QString,   Qt::Key>       m_settingsProfile;

    void bindKey(const ClipGraph::AbSlotRef &slot, Qt::Key key);
    void releaseSlot(const ClipGraph::AbSlotRef &slot);
    void refreshSlotLabels();
};
