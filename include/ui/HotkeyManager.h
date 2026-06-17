#pragma once

#include <QObject>
#include <QMap>
#include <QList>
#include <QJsonObject>
#include <QStringList>
#include <Qt>
#include "ui/ClipNodeEditor.h"

class QShortcut;
class QWidget;
class ClipNodeModel;

/// Manages keyboard shortcut assignments (VJ hotkey grid) for clip nodes.
/// Keys 1-0, Q-P, A-L, Z-M are auto-assigned in order. Each key maps to
/// a node: bare key → Deck A, Shift+key → Deck B.
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(QWidget *shortcutParent, ClipNodeEditor *editor,
                           QObject *parent = nullptr);

    void assignHotkeyToNode(NodeId nodeId);
    void releaseHotkeyForNode(NodeId nodeId);

    /// Replace all bindings atomically (used by the hotkey editor).
    void applyBindings(const QMap<NodeId, Qt::Key> &bindings);

    /// Assign a single binding; returns false if the key is already taken by another node.
    bool setBinding(NodeId nodeId, Qt::Key key);
    void clearBinding(NodeId nodeId);

    Qt::Key bindingForNode(NodeId nodeId) const;
    NodeId  nodeForKey(Qt::Key key) const;

    /// Restore hotkey bindings from a saved key→nodeId mapping.
    void restoreHotkeys(const QMap<Qt::Key, NodeId> &keyToNode);

    /// Serialize the current key→node mapping for session saving.
    QMap<NodeId, Qt::Key> nodeHotkeys() const { return m_nodeHotkeys; }

    static const QList<Qt::Key> &hotkeySequence();
    static QString clipBindingKey(const ClipNodeModel *node);
    static bool isBindableKey(Qt::Key key);

    /// Portable profile (clip path/name → key), for import/export and QSettings.
    QJsonObject exportProfile() const;
    bool importProfile(const QJsonObject &profile, QStringList *warnings = nullptr);

    void saveSettingsProfile() const;
    void loadSettingsProfile();

signals:
    void deckARequested(NodeId nodeId);
    void deckBRequested(NodeId nodeId);
    void bindingsChanged();

private:
    struct NodeShortcuts {
        QShortcut *deckA = nullptr;
        QShortcut *deckB = nullptr;
    };

    QWidget        *m_shortcutParent;
    ClipNodeEditor *m_editor;

    QMap<NodeId,   Qt::Key>        m_nodeHotkeys;
    QMap<Qt::Key,  NodeId>         m_keyToNode;
    QMap<NodeId,   NodeShortcuts>  m_nodeShortcuts;
    QMap<QString,  Qt::Key>        m_settingsProfile;

    void bindKey(NodeId nodeId, Qt::Key key);
    void unbindShortcutsForNode(NodeId nodeId);
    Qt::Key keyFromSettingsForNode(NodeId nodeId) const;
};
