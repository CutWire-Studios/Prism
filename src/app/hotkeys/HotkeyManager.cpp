#include "app/hotkeys/HotkeyManager.h"
#include "app/show/ShowManager.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QQuickItem>
#include <QSettings>
#include <QJsonDocument>
#include <QDebug>

namespace {
constexpr int kProfileVersion = 3;   // v3 binds scenes

// Shift turns the digit row into symbols (US layout), and QKeyEvent::key() reports the symbol.
Qt::Key unshiftedDigit(int key) {
    switch (key) {
    case Qt::Key_Exclam:      return Qt::Key_1;
    case Qt::Key_At:          return Qt::Key_2;
    case Qt::Key_NumberSign:  return Qt::Key_3;
    case Qt::Key_Dollar:      return Qt::Key_4;
    case Qt::Key_Percent:     return Qt::Key_5;
    case Qt::Key_AsciiCircum: return Qt::Key_6;
    case Qt::Key_Ampersand:   return Qt::Key_7;
    case Qt::Key_Asterisk:    return Qt::Key_8;
    case Qt::Key_ParenLeft:   return Qt::Key_9;
    case Qt::Key_ParenRight:  return Qt::Key_0;
    default:                  return Qt::Key_unknown;
    }
}

bool focusIsTextInput() {
    QObject *focus = QGuiApplication::focusObject();
    if (!focus)
        return false;
    QInputMethodQueryEvent query(Qt::ImEnabled);
    QCoreApplication::sendEvent(focus, &query);
    return query.value(Qt::ImEnabled).toBool();
}

bool focusReserves(int combo) {
    auto *item = qobject_cast<QQuickItem *>(QGuiApplication::focusObject());
    for (; item; item = item->parentItem()) {
        if (item->property("hotkeyCaptureAll").toBool())
            return true;
        const QVariant reserved = item->property("hotkeyReservedKeys");
        if (reserved.isValid())
            return HotkeyManager::isReserved(combo, reserved.toList());
    }
    return false;
}
}

HotkeyManager::HotkeyManager(ShowManager *show, QObject *parent)
    : QObject(parent), m_show(show)
{
    loadSettingsProfile();
    syncWithShow();
    if (qApp)
        qApp->installEventFilter(this);
}

bool HotkeyManager::eventFilter(QObject *watched, QEvent *event) {
    const QEvent::Type type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::ShortcutOverride)
        return false;
    if (m_suspended || !watched->isWindowType() || QGuiApplication::modalWindow())
        return false;

    auto *ke = static_cast<QKeyEvent *>(event);
    const Qt::KeyboardModifiers mods = ke->modifiers() & ~Qt::KeypadModifier;
    if (mods != Qt::NoModifier && mods != Qt::ShiftModifier)
        return false;
    const bool takeNow = mods == Qt::ShiftModifier;

    int key = ke->key();
    if (takeNow) {
        const Qt::Key plain = unshiftedDigit(key);
        if (plain != Qt::Key_unknown)
            key = plain;
    }
    const QString ref = m_keyToScene.value(static_cast<Qt::Key>(key));
    if (ref.isEmpty() || focusIsTextInput())
        return false;
    if (focusReserves(key | static_cast<int>(mods)))
        return false;

    if (type == QEvent::ShortcutOverride) {
        event->accept();
        return false;
    }
    emit sceneTriggered(ref, takeNow);
    return true;
}

bool HotkeyManager::isReserved(int combo, const QVariantList &reserved) {
    for (const QVariant &r : reserved) {
        if (r.toInt() == combo)
            return true;
    }
    return false;
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

void HotkeyManager::bindKey(const QString &sceneId, Qt::Key key) {
    if (m_sceneHotkeys.contains(sceneId)) {
        const Qt::Key oldKey = m_sceneHotkeys.value(sceneId);
        if (oldKey != key)
            m_keyToScene.remove(oldKey);
    }

    m_sceneHotkeys[sceneId] = key;
    m_keyToScene[key]     = sceneId;
}

void HotkeyManager::releaseScene(const QString &sceneId) {
    auto hit = m_sceneHotkeys.find(sceneId);
    if (hit == m_sceneHotkeys.end())
        return;

    m_keyToScene.remove(hit.value());
    m_sceneHotkeys.erase(hit);
}

void HotkeyManager::syncWithShow() {
    QStringList alive;
    for (const Scene *scene : m_show->scenes())
        alive.append(scene->id());

    bool changed = false;
    const QStringList bound = m_sceneHotkeys.keys();
    for (const QString &ref : bound) {
        if (!alive.contains(ref)) {
            releaseScene(ref);
            changed = true;
        }
    }

    for (const QString &id : alive) {
        if (m_sceneHotkeys.contains(id))
            continue;

        Qt::Key chosen = m_settingsProfile.value(id, Qt::Key_unknown);
        if (chosen == Qt::Key_unknown || m_keyToScene.contains(chosen)) {
            chosen = Qt::Key_unknown;
            for (Qt::Key k : hotkeySequence()) {
                if (!m_keyToScene.contains(k)) {
                    chosen = k;
                    break;
                }
            }
        }
        if (chosen == Qt::Key_unknown)
            continue;

        bindKey(id, chosen);
        changed = true;
    }

    if (changed)
        emit bindingsChanged();
}

bool HotkeyManager::setBinding(const QString &sceneId, Qt::Key key) {
    if (key == Qt::Key_unknown) {
        clearBinding(sceneId);
        return true;
    }
    if (!isBindableKey(key))
        return false;

    if (m_keyToScene.contains(key) && m_keyToScene.value(key) != sceneId)
        return false;

    if (m_sceneHotkeys.value(sceneId, Qt::Key_unknown) == key)
        return true;

    bindKey(sceneId, key);
    emit bindingsChanged();
    return true;
}

void HotkeyManager::clearBinding(const QString &sceneId) {
    releaseScene(sceneId);
    emit bindingsChanged();
}

Qt::Key HotkeyManager::bindingForScene(const QString &sceneId) const {
    return m_sceneHotkeys.value(sceneId, Qt::Key_unknown);
}

QString HotkeyManager::sceneForKey(Qt::Key key) const {
    return m_keyToScene.value(key);
}

void HotkeyManager::applyBindings(const QMap<QString, Qt::Key> &bindings) {
    const QStringList existing = m_sceneHotkeys.keys();
    for (const QString &ref : existing)
        releaseScene(ref);

    QStringList alive;
    for (const Scene *scene : m_show->scenes())
        alive.append(scene->id());

    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        const Qt::Key key = it.value();
        if (key == Qt::Key_unknown || !isBindableKey(key))
            continue;
        if (m_keyToScene.contains(key))
            continue;
        if (!alive.contains(it.key()))
            continue;

        bindKey(it.key(), key);
    }

    saveSettingsProfile();
    emit bindingsChanged();
}

QJsonArray HotkeyManager::hotkeysJson() const {
    QJsonArray arr;
    for (auto it = m_sceneHotkeys.cbegin(); it != m_sceneHotkeys.cend(); ++it) {
        QJsonObject hk;
        hk["scene"] = it.key();
        hk["key"] = (int)it.value();
        arr.append(hk);
    }
    return arr;
}

void HotkeyManager::restoreHotkeys(const QJsonArray &hotkeys) {
    const QStringList existing = m_sceneHotkeys.keys();
    for (const QString &ref : existing)
        releaseScene(ref);

    QStringList alive;
    for (const Scene *scene : m_show->scenes())
        alive.append(scene->id());

    QStringList skipped;
    for (const auto &val : hotkeys) {
        const QJsonObject obj = val.toObject();
        const Qt::Key key = static_cast<Qt::Key>(obj["key"].toInt());
        if (!isBindableKey(key))
            continue;
        if (m_keyToScene.contains(key)) {
            skipped << QKeySequence(key).toString();
            continue;
        }

        const QString ref = obj["scene"].toString();
        if (!alive.contains(ref))
            continue;

        bindKey(ref, key);
    }

    if (!skipped.isEmpty())
        qWarning() << "HotkeyManager: skipped duplicate keys during restore:" << skipped;

    // Auto-assign whatever the session didn't cover.
    syncWithShow();
    emit bindingsChanged();
}

QJsonObject HotkeyManager::exportProfile() const {
    QJsonObject root;
    root.insert(QStringLiteral("version"), kProfileVersion);

    QJsonArray bindings;
    for (auto it = m_sceneHotkeys.cbegin(); it != m_sceneHotkeys.cend(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("slotKey"), it.key());
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

    QStringList alive;
    for (const Scene *scene : m_show->scenes())
        alive.append(scene->id());

    QMap<QString, Qt::Key> desired;
    const QJsonArray bindings = profile.value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &val : bindings) {
        const QJsonObject entry = val.toObject();
        const QString slotKey   = entry.value(QStringLiteral("slotKey")).toString();
        const Qt::Key key       = static_cast<Qt::Key>(entry.value(QStringLiteral("key")).toInt());
        if (!isBindableKey(key))
            continue;

        if (!alive.contains(slotKey)) {
            if (warnings)
                warnings->append(QObject::tr("No scene matched: %1").arg(slotKey));
            continue;
        }
        desired.insert(slotKey, key);
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
