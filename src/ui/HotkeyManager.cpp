#include "ui/HotkeyManager.h"
#include "ui/ClipNodeModel.h"
#include "core/SourceDescriptor.h"
#include <QShortcut>
#include <QKeySequence>
#include <QSettings>
#include <QWidget>
#include <QJsonArray>
#include <QJsonDocument>

namespace {
constexpr int kProfileVersion = 1;
}

HotkeyManager::HotkeyManager(QWidget *shortcutParent, ClipNodeEditor *editor, QObject *parent)
    : QObject(parent), m_shortcutParent(shortcutParent), m_editor(editor)
{
    loadSettingsProfile();
}

const QList<Qt::Key> &HotkeyManager::hotkeySequence() {
    static const QList<Qt::Key> seq = {
        Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5,
        Qt::Key_6, Qt::Key_7, Qt::Key_8, Qt::Key_9, Qt::Key_0,
        Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T,
        Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P,
        Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G,
        Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L,
        Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B,
        Qt::Key_N, Qt::Key_M,
    };
    return seq;
}

QString HotkeyManager::clipBindingKey(const ClipNodeModel *node) {
    if (!node || !node->hasSource())
        return {};
    const SourceDescriptor &d = node->sourceDescriptor();
    if (!d.path.isEmpty())
        return d.path;
    if (!d.displayName.isEmpty())
        return d.displayName;
    return node->sourceName();
}

bool HotkeyManager::isBindableKey(Qt::Key key) {
    if (key == Qt::Key_unknown)
        return false;
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_Tab:
    case Qt::Key_Escape:
        return false;
    default:
        break;
    }
    return true;
}

void HotkeyManager::assignHotkeyToNode(NodeId nodeId) {
    ClipNodeModel *node = m_editor->nodeAt(nodeId);
    if (!node || node->isGroupMember())
        return;

    if (const Qt::Key saved = keyFromSettingsForNode(nodeId); saved != Qt::Key_unknown) {
        if (!m_keyToNode.contains(saved)) {
            bindKey(nodeId, saved);
            node->setHotkeyLabel(QKeySequence(saved).toString());
            emit bindingsChanged();
            return;
        }
    }

    Qt::Key chosen = Qt::Key_unknown;
    for (Qt::Key k : hotkeySequence()) {
        if (!m_keyToNode.contains(k)) {
            chosen = k;
            break;
        }
    }
    if (chosen == Qt::Key_unknown)
        return;

    bindKey(nodeId, chosen);
    node->setHotkeyLabel(QKeySequence(chosen).toString());
    emit bindingsChanged();
}

void HotkeyManager::unbindShortcutsForNode(NodeId nodeId) {
    auto sit = m_nodeShortcuts.find(nodeId);
    if (sit != m_nodeShortcuts.end()) {
        delete sit.value().deckA;
        delete sit.value().deckB;
        m_nodeShortcuts.erase(sit);
    }
}

void HotkeyManager::bindKey(NodeId nodeId, Qt::Key key) {
    if (m_nodeHotkeys.contains(nodeId)) {
        const Qt::Key oldKey = m_nodeHotkeys.value(nodeId);
        if (oldKey != key)
            m_keyToNode.remove(oldKey);
        unbindShortcutsForNode(nodeId);
    }

    m_nodeHotkeys[nodeId] = key;
    m_keyToNode[key]      = nodeId;

    auto *scA = new QShortcut(QKeySequence(key), m_shortcutParent);
    scA->setContext(Qt::ApplicationShortcut);
    connect(scA, &QShortcut::activated, this, [this, key]() {
        const NodeId id = m_keyToNode.value(key, 0);
        if (id)
            emit deckARequested(id);
    });

    auto *scB = new QShortcut(QKeySequence(Qt::SHIFT | key), m_shortcutParent);
    scB->setContext(Qt::ApplicationShortcut);
    connect(scB, &QShortcut::activated, this, [this, key]() {
        const NodeId id = m_keyToNode.value(key, 0);
        if (id)
            emit deckBRequested(id);
    });

    m_nodeShortcuts[nodeId] = {scA, scB};
}

bool HotkeyManager::setBinding(NodeId nodeId, Qt::Key key) {
    if (key == Qt::Key_unknown) {
        clearBinding(nodeId);
        return true;
    }
    if (!isBindableKey(key))
        return false;

    if (m_keyToNode.contains(key) && m_keyToNode.value(key) != nodeId)
        return false;

    if (m_nodeHotkeys.value(nodeId) == key)
        return true;

    bindKey(nodeId, key);
    if (ClipNodeModel *node = m_editor->nodeAt(nodeId))
        node->setHotkeyLabel(QKeySequence(key).toString());

    emit bindingsChanged();
    return true;
}

void HotkeyManager::clearBinding(NodeId nodeId) {
    releaseHotkeyForNode(nodeId);
    emit bindingsChanged();
}

Qt::Key HotkeyManager::bindingForNode(NodeId nodeId) const {
    return m_nodeHotkeys.value(nodeId, Qt::Key_unknown);
}

NodeId HotkeyManager::nodeForKey(Qt::Key key) const {
    return m_keyToNode.value(key, 0);
}

void HotkeyManager::applyBindings(const QMap<NodeId, Qt::Key> &bindings) {
    const QList<NodeId> existing = m_nodeHotkeys.keys();
    for (NodeId id : existing)
        releaseHotkeyForNode(id);

    QMap<Qt::Key, NodeId> used;
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        const NodeId  nodeId = it.key();
        const Qt::Key key    = it.value();
        if (key == Qt::Key_unknown || !isBindableKey(key))
            continue;
        if (used.contains(key))
            continue;
        if (!m_editor->nodeAt(nodeId))
            continue;

        used.insert(key, nodeId);
        bindKey(nodeId, key);
        if (ClipNodeModel *node = m_editor->nodeAt(nodeId))
            node->setHotkeyLabel(QKeySequence(key).toString());
    }

    saveSettingsProfile();
    emit bindingsChanged();
}

void HotkeyManager::releaseHotkeyForNode(NodeId nodeId) {
    auto hit = m_nodeHotkeys.find(nodeId);
    if (hit == m_nodeHotkeys.end())
        return;

    const Qt::Key key = hit.value();
    m_nodeHotkeys.erase(hit);
    m_keyToNode.remove(key);
    unbindShortcutsForNode(nodeId);

    if (ClipNodeModel *node = m_editor->nodeAt(nodeId))
        node->setHotkeyLabel({});
}

void HotkeyManager::restoreHotkeys(const QMap<Qt::Key, NodeId> &keyToNode) {
    for (NodeId id : m_nodeHotkeys.keys())
        releaseHotkeyForNode(id);

    QStringList skipped;
    for (auto it = keyToNode.cbegin(); it != keyToNode.cend(); ++it) {
        const Qt::Key key    = it.key();
        const NodeId  nodeId = it.value();

        ClipNodeModel *node = m_editor->nodeAt(nodeId);
        if (!node)
            continue;
        if (m_keyToNode.contains(key)) {
            skipped << QKeySequence(key).toString();
            continue;
        }

        bindKey(nodeId, key);
        node->setHotkeyLabel(QKeySequence(key).toString());
    }

    if (!skipped.isEmpty())
        qWarning() << "HotkeyManager: skipped duplicate keys during restore:" << skipped;

    emit bindingsChanged();
}

QJsonObject HotkeyManager::exportProfile() const {
    QJsonObject root;
    root.insert(QStringLiteral("version"), kProfileVersion);

    QJsonArray bindings;
    for (auto it = m_nodeHotkeys.cbegin(); it != m_nodeHotkeys.cend(); ++it) {
        ClipNodeModel *node = m_editor->nodeAt(it.key());
        if (!node)
            continue;
        const QString clipKey = clipBindingKey(node);
        if (clipKey.isEmpty())
            continue;

        QJsonObject entry;
        entry.insert(QStringLiteral("clipKey"), clipKey);
        entry.insert(QStringLiteral("key"), static_cast<int>(it.value()));
        bindings.append(entry);
    }
    root.insert(QStringLiteral("bindings"), bindings);
    return root;
}

bool HotkeyManager::importProfile(const QJsonObject &profile, QStringList *warnings) {
    if (profile.value(QStringLiteral("version")).toInt(0) != kProfileVersion) {
        if (warnings)
            warnings->append(QObject::tr("Unsupported hotkey profile version."));
        return false;
    }

    QMap<NodeId, Qt::Key> desired;
    const QJsonArray bindings = profile.value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &val : bindings) {
        const QJsonObject entry = val.toObject();
        const QString clipKey   = entry.value(QStringLiteral("clipKey")).toString();
        const Qt::Key key       = static_cast<Qt::Key>(entry.value(QStringLiteral("key")).toInt());
        if (clipKey.isEmpty() || !isBindableKey(key))
            continue;

        NodeId matched = 0;
        for (ClipNodeModel *node : m_editor->allNodes()) {
            if (!node || !node->hasSource() || node->isGroupMember())
                continue;
            if (clipBindingKey(node) == clipKey) {
                matched = node->nodeId();
                break;
            }
        }
        if (!matched) {
            if (warnings)
                warnings->append(QObject::tr("No clip matched: %1").arg(clipKey));
            continue;
        }
        desired.insert(matched, key);
    }

    applyBindings(desired);
    return true;
}

void HotkeyManager::saveSettingsProfile() const {
    QSettings settings;
    settings.beginGroup(QStringLiteral("hotkeys"));
    settings.setValue(QStringLiteral("profile"),
                      QJsonDocument(exportProfile()).toJson(QJsonDocument::Compact));
    settings.endGroup();
}

void HotkeyManager::loadSettingsProfile() {
    m_settingsProfile.clear();
    QSettings settings;
    settings.beginGroup(QStringLiteral("hotkeys"));
    const QByteArray raw = settings.value(QStringLiteral("profile")).toByteArray();
    settings.endGroup();
    if (raw.isEmpty())
        return;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonArray bindings = doc.object().value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &val : bindings) {
        const QJsonObject entry = val.toObject();
        const QString clipKey   = entry.value(QStringLiteral("clipKey")).toString();
        const Qt::Key key       = static_cast<Qt::Key>(entry.value(QStringLiteral("key")).toInt());
        if (!clipKey.isEmpty() && isBindableKey(key))
            m_settingsProfile.insert(clipKey, key);
    }
}

Qt::Key HotkeyManager::keyFromSettingsForNode(NodeId nodeId) const {
    ClipNodeModel *node = m_editor->nodeAt(nodeId);
    if (!node)
        return Qt::Key_unknown;
    return m_settingsProfile.value(clipBindingKey(node), Qt::Key_unknown);
}
