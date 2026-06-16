#include "ui/HotkeyManager.h"
#include <QShortcut>
#include <QKeySequence>
#include <QWidget>

HotkeyManager::HotkeyManager(QWidget *shortcutParent, ClipNodeEditor *editor, QObject *parent)
    : QObject(parent), m_shortcutParent(shortcutParent), m_editor(editor)
{
}

const QList<Qt::Key> &HotkeyManager::hotkeySequence() {
    static const QList<Qt::Key> seq = {
        // Row 1: number row
        Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5,
        Qt::Key_6, Qt::Key_7, Qt::Key_8, Qt::Key_9, Qt::Key_0,
        // Row 2: QWERTY top
        Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T,
        Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P,
        // Row 3: home row
        Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G,
        Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L,
        // Row 4: bottom row
        Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B,
        Qt::Key_N, Qt::Key_M,
    };
    return seq;
}

void HotkeyManager::assignHotkeyToNode(NodeId nodeId) {
    ClipNodeModel *node = m_editor->nodeAt(nodeId);
    if (!node) return;

    // Find the first unoccupied slot in the VJ grid sequence.
    Qt::Key chosen = Qt::Key_unknown;
    for (Qt::Key k : hotkeySequence()) {
        if (!m_keyToNode.contains(k)) { chosen = k; break; }
    }
    if (chosen == Qt::Key_unknown) return; // All 36 slots occupied

    bindKey(nodeId, chosen);

    // Show the badge on the card.
    node->setHotkeyLabel(QKeySequence(chosen).toString());
}

void HotkeyManager::bindKey(NodeId nodeId, Qt::Key key) {
    m_nodeHotkeys[nodeId] = key;
    m_keyToNode[key]      = nodeId;

    // Deck-A shortcut: bare key press.
    auto *scA = new QShortcut(QKeySequence(key), m_shortcutParent);
    scA->setContext(Qt::ApplicationShortcut);
    connect(scA, &QShortcut::activated, this, [this, key]() {
        NodeId id = m_keyToNode.value(key, 0);
        if (id) emit deckARequested(id);
    });

    // Deck-B shortcut: Shift + key.
    auto *scB = new QShortcut(QKeySequence(Qt::SHIFT | key), m_shortcutParent);
    scB->setContext(Qt::ApplicationShortcut);
    connect(scB, &QShortcut::activated, this, [this, key]() {
        NodeId id = m_keyToNode.value(key, 0);
        if (id) emit deckBRequested(id);
    });

    m_nodeShortcuts[nodeId] = {scA, scB};
}

void HotkeyManager::releaseHotkeyForNode(NodeId nodeId) {
    auto hit = m_nodeHotkeys.find(nodeId);
    if (hit == m_nodeHotkeys.end()) return;

    Qt::Key key = hit.value();
    m_nodeHotkeys.erase(hit);
    m_keyToNode.remove(key);

    auto sit = m_nodeShortcuts.find(nodeId);
    if (sit != m_nodeShortcuts.end()) {
        delete sit.value().deckA;
        delete sit.value().deckB;
        m_nodeShortcuts.erase(sit);
    }
    // Clear the badge on the card if still alive.
    if (ClipNodeModel *node = m_editor->nodeAt(nodeId))
        node->setHotkeyLabel({});
}

void HotkeyManager::restoreHotkeys(const QMap<Qt::Key, NodeId> &keyToNode) {
    // Clear stale mappings first.
    for (NodeId id : m_nodeHotkeys.keys())
        releaseHotkeyForNode(id);

    for (auto it = keyToNode.cbegin(); it != keyToNode.cend(); ++it) {
        const Qt::Key key    = it.key();
        const NodeId  nodeId = it.value();

        ClipNodeModel *node = m_editor->nodeAt(nodeId);
        if (!node) continue;
        if (m_keyToNode.contains(key)) continue; // already taken

        bindKey(nodeId, key);
        node->setHotkeyLabel(QKeySequence(key).toString());
    }
}
