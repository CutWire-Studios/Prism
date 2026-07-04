#pragma once

#include <QObject>
#include <QMap>
#include <QList>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <Qt>
#include "ui/nodes/ClipNodeEditor.h"

class QShortcut;
class QWidget;

/// Manages keyboard shortcut assignments (VJ hotkey grid) for the inputs of
/// the A/B Select nodes. Keys 1-0, Q-P, A-L, Z-M are auto-assigned to
/// connected switcher inputs in order. Bare key sends the input to Deck A,
/// Shift+key to Deck B.
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(QWidget *shortcutParent, ClipNodeEditor *editor,
                           QObject *parent = nullptr);

    /// Re-derive bindings from the node graph: release keys whose switcher
    /// input is gone and auto-assign keys to newly connected inputs.
    /// Connect to ClipNodeEditor::clipChainChanged.
    void syncWithGraph();

    /// Replace all bindings atomically (used by the hotkey editor).
    void applyBindings(const QMap<AbSlotRef, Qt::Key> &bindings);

    /// Assign a single binding; returns false if the key is taken by another input.
    bool setBinding(const AbSlotRef &slot, Qt::Key key);
    void clearBinding(const AbSlotRef &slot);

    Qt::Key   bindingForSlot(const AbSlotRef &slot) const;
    AbSlotRef slotForKey(Qt::Key key) const;

    /// Session persistence.
    QJsonArray hotkeysJson() const;
    /// Restore from a session; accepts both the current {abNode, slot, key}
    /// entries and legacy {nodeId, key} clip bindings (mapped to the switcher
    /// input the clip feeds). Unbound inputs are then auto-assigned.
    void restoreHotkeys(const QJsonArray &hotkeys);

    static const QList<Qt::Key> &hotkeySequence();
    static bool isBindableKey(Qt::Key key);
    static QString slotSettingsKey(const AbSlotRef &slot);

    /// Portable profile (switcher input → key), for import/export and QSettings.
    QJsonObject exportProfile() const;
    bool importProfile(const QJsonObject &profile, QStringList *warnings = nullptr);

    void saveSettingsProfile() const;
    void loadSettingsProfile();

signals:
    void bindingsChanged();

private:
    struct SlotShortcuts {
        QShortcut *deckA = nullptr;
        QShortcut *deckB = nullptr;
    };

    QWidget        *m_shortcutParent;
    ClipNodeEditor *m_editor;

    QMap<AbSlotRef, Qt::Key>       m_slotHotkeys;
    QMap<Qt::Key,   AbSlotRef>     m_keyToSlot;
    QMap<AbSlotRef, SlotShortcuts> m_slotShortcuts;
    QMap<QString,   Qt::Key>       m_settingsProfile;

    void bindKey(const AbSlotRef &slot, Qt::Key key);
    void releaseSlot(const AbSlotRef &slot);
    void refreshSlotLabels();
};
