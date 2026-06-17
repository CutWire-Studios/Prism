#pragma once

#include <QObject>
#include <QMap>
#include <QList>
#include <Qt>
#include "ui/ClipNodeEditor.h"

class QShortcut;
class QWidget;

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

    /// Restore hotkey bindings from a saved key→nodeId mapping.
    void restoreHotkeys(const QMap<Qt::Key, NodeId> &keyToNode);

    /// Serialize the current key→node mapping for session saving.
    QMap<NodeId, Qt::Key> nodeHotkeys() const { return m_nodeHotkeys; }

    static const QList<Qt::Key> &hotkeySequence();

signals:
    void deckARequested(NodeId nodeId);
    void deckBRequested(NodeId nodeId);

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

    void bindKey(NodeId nodeId, Qt::Key key);
};
