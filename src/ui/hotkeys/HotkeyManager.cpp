#include "ui/hotkeys/HotkeyManager.h"
#include <QShortcut>
#include <QKeySequence>
#include <QSettings>
#include <QWidget>
#include <QJsonDocument>
#include <QDebug>

namespace {
constexpr int kProfileVersion = 2;   // v1 bound clips; v2 binds A/B switcher inputs
}

HotkeyManager::HotkeyManager(QWidget *shortcutParent, ClipNodeEditor *editor, QObject *parent)
    : QObject(parent), m_shortcutParent(shortcutParent), m_editor(editor)
{
    loadSettingsProfile();
    syncWithGraph();
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

QString HotkeyManager::slotSettingsKey(const AbSlotRef &slot) {
    return QStringLiteral("%1/%2").arg(slot.abNodeId).arg(slot.slot);
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

void HotkeyManager::bindKey(const AbSlotRef &slot, Qt::Key key) {
    if (m_slotHotkeys.contains(slot)) {
        const Qt::Key oldKey = m_slotHotkeys.value(slot);
        if (oldKey != key)
            m_keyToSlot.remove(oldKey);
        auto sit = m_slotShortcuts.find(slot);
        if (sit != m_slotShortcuts.end()) {
            delete sit.value().deckA;
            delete sit.value().deckB;
            m_slotShortcuts.erase(sit);
        }
    }

    m_slotHotkeys[slot] = key;
    m_keyToSlot[key]    = slot;

    auto *scA = new QShortcut(QKeySequence(key), m_shortcutParent);
    scA->setContext(Qt::ApplicationShortcut);
    connect(scA, &QShortcut::activated, this, [this, key]() {
        const AbSlotRef ref = m_keyToSlot.value(key);
        if (ref.isValid())
            m_editor->triggerAbSlot(ref, true);
    });

    auto *scB = new QShortcut(QKeySequence(Qt::SHIFT | key), m_shortcutParent);
    scB->setContext(Qt::ApplicationShortcut);
    connect(scB, &QShortcut::activated, this, [this, key]() {
        const AbSlotRef ref = m_keyToSlot.value(key);
        if (ref.isValid())
            m_editor->triggerAbSlot(ref, false);
    });

    m_slotShortcuts[slot] = {scA, scB};
    m_editor->setAbSlotHotkeyLabel(slot, QKeySequence(key).toString());
}

void HotkeyManager::releaseSlot(const AbSlotRef &slot) {
    auto hit = m_slotHotkeys.find(slot);
    if (hit == m_slotHotkeys.end())
        return;

    m_keyToSlot.remove(hit.value());
    m_slotHotkeys.erase(hit);

    auto sit = m_slotShortcuts.find(slot);
    if (sit != m_slotShortcuts.end()) {
        delete sit.value().deckA;
        delete sit.value().deckB;
        m_slotShortcuts.erase(sit);
    }
    m_editor->setAbSlotHotkeyLabel(slot, {});
}

void HotkeyManager::refreshSlotLabels() {
    m_editor->clearAbSlotHotkeyLabels();
    for (auto it = m_slotHotkeys.cbegin(); it != m_slotHotkeys.cend(); ++it)
        m_editor->setAbSlotHotkeyLabel(it.key(), QKeySequence(it.value()).toString());
}

void HotkeyManager::syncWithGraph() {
    const QVector<AbSlotInfo> inputs = m_editor->abSelectInputs();

    QList<AbSlotRef> alive;
    alive.reserve(inputs.size());
    for (const AbSlotInfo &info : inputs)
        alive.append(info.ref);

    bool changed = false;
    const QList<AbSlotRef> bound = m_slotHotkeys.keys();
    for (const AbSlotRef &ref : bound) {
        if (!alive.contains(ref)) {
            releaseSlot(ref);
            changed = true;
        }
    }

    for (const AbSlotInfo &info : inputs) {
        if (m_slotHotkeys.contains(info.ref))
            continue;

        Qt::Key chosen = m_settingsProfile.value(slotSettingsKey(info.ref), Qt::Key_unknown);
        if (chosen == Qt::Key_unknown || m_keyToSlot.contains(chosen)) {
            chosen = Qt::Key_unknown;
            for (Qt::Key k : hotkeySequence()) {
                if (!m_keyToSlot.contains(k)) {
                    chosen = k;
                    break;
                }
            }
        }
        if (chosen == Qt::Key_unknown)
            continue;

        bindKey(info.ref, chosen);
        changed = true;
    }

    refreshSlotLabels();
    if (changed)
        emit bindingsChanged();
}

bool HotkeyManager::setBinding(const AbSlotRef &slot, Qt::Key key) {
    if (key == Qt::Key_unknown) {
        clearBinding(slot);
        return true;
    }
    if (!isBindableKey(key))
        return false;

    if (m_keyToSlot.contains(key) && !(m_keyToSlot.value(key) == slot))
        return false;

    if (m_slotHotkeys.value(slot, Qt::Key_unknown) == key)
        return true;

    bindKey(slot, key);
    refreshSlotLabels();
    emit bindingsChanged();
    return true;
}

void HotkeyManager::clearBinding(const AbSlotRef &slot) {
    releaseSlot(slot);
    refreshSlotLabels();
    emit bindingsChanged();
}

Qt::Key HotkeyManager::bindingForSlot(const AbSlotRef &slot) const {
    return m_slotHotkeys.value(slot, Qt::Key_unknown);
}

AbSlotRef HotkeyManager::slotForKey(Qt::Key key) const {
    return m_keyToSlot.value(key);
}

void HotkeyManager::applyBindings(const QMap<AbSlotRef, Qt::Key> &bindings) {
    const QList<AbSlotRef> existing = m_slotHotkeys.keys();
    for (const AbSlotRef &ref : existing)
        releaseSlot(ref);

    QList<AbSlotRef> alive;
    for (const AbSlotInfo &info : m_editor->abSelectInputs())
        alive.append(info.ref);

    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        const AbSlotRef &slot = it.key();
        const Qt::Key    key  = it.value();
        if (key == Qt::Key_unknown || !isBindableKey(key))
            continue;
        if (m_keyToSlot.contains(key))
            continue;
        if (!alive.contains(slot))
            continue;

        bindKey(slot, key);
    }

    refreshSlotLabels();
    saveSettingsProfile();
    emit bindingsChanged();
}

QJsonArray HotkeyManager::hotkeysJson() const {
    QJsonArray arr;
    for (auto it = m_slotHotkeys.cbegin(); it != m_slotHotkeys.cend(); ++it) {
        QJsonObject hk;
        hk["abNode"] = (qint64)it.key().abNodeId;
        hk["slot"]   = it.key().slot;
        hk["key"]    = (int)it.value();
        arr.append(hk);
    }
    return arr;
}

void HotkeyManager::restoreHotkeys(const QJsonArray &hotkeys) {
    const QList<AbSlotRef> existing = m_slotHotkeys.keys();
    for (const AbSlotRef &ref : existing)
        releaseSlot(ref);

    const QVector<AbSlotInfo> inputs = m_editor->abSelectInputs();

    QStringList skipped;
    for (const auto &val : hotkeys) {
        const QJsonObject obj = val.toObject();
        const Qt::Key key = static_cast<Qt::Key>(obj["key"].toInt());
        if (!isBindableKey(key))
            continue;
        if (m_keyToSlot.contains(key)) {
            skipped << QKeySequence(key).toString();
            continue;
        }

        AbSlotRef ref;
        if (obj.contains("abNode")) {
            ref = {(NodeId)obj["abNode"].toInteger(), obj["slot"].toInt(-1)};
        } else {
            // Legacy clip binding: map to the switcher input the clip feeds.
            const NodeId clipId = (NodeId)obj["nodeId"].toInteger();
            for (const AbSlotInfo &info : inputs) {
                if (info.producer == clipId) {
                    ref = info.ref;
                    break;
                }
            }
        }

        bool alive = false;
        for (const AbSlotInfo &info : inputs)
            if (info.ref == ref) { alive = true; break; }
        if (!alive)
            continue;

        bindKey(ref, key);
    }

    if (!skipped.isEmpty())
        qWarning() << "HotkeyManager: skipped duplicate keys during restore:" << skipped;

    // Auto-assign whatever the session didn't cover, and repaint badges.
    syncWithGraph();
    emit bindingsChanged();
}

QJsonObject HotkeyManager::exportProfile() const {
    QJsonObject root;
    root.insert(QStringLiteral("version"), kProfileVersion);

    QJsonArray bindings;
    for (auto it = m_slotHotkeys.cbegin(); it != m_slotHotkeys.cend(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("slotKey"), slotSettingsKey(it.key()));
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

    QList<AbSlotRef> alive;
    for (const AbSlotInfo &info : m_editor->abSelectInputs())
        alive.append(info.ref);

    QMap<AbSlotRef, Qt::Key> desired;
    const QJsonArray bindings = profile.value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &val : bindings) {
        const QJsonObject entry = val.toObject();
        const QString slotKey   = entry.value(QStringLiteral("slotKey")).toString();
        const Qt::Key key       = static_cast<Qt::Key>(entry.value(QStringLiteral("key")).toInt());
        const QStringList parts = slotKey.split(QLatin1Char('/'));
        if (parts.size() != 2 || !isBindableKey(key))
            continue;

        const AbSlotRef ref{parts[0].toULongLong(), parts[1].toInt()};
        if (!alive.contains(ref)) {
            if (warnings)
                warnings->append(QObject::tr("No switcher input matched: %1").arg(slotKey));
            continue;
        }
        desired.insert(ref, key);
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
    if (doc.object().value(QStringLiteral("version")).toInt(0) != kProfileVersion)
        return;   // discard old clip-based profiles

    const QJsonArray bindings = doc.object().value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &val : bindings) {
        const QJsonObject entry = val.toObject();
        const QString slotKey   = entry.value(QStringLiteral("slotKey")).toString();
        const Qt::Key key       = static_cast<Qt::Key>(entry.value(QStringLiteral("key")).toInt());
        if (!slotKey.isEmpty() && isBindableKey(key))
            m_settingsProfile.insert(slotKey, key);
    }
}
