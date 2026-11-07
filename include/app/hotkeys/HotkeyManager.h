#pragma once

#include <QObject>
#include <QMap>
#include <QList>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <Qt>

class AppShortcuts;
class ShowManager;

/// Manages keyboard shortcut assignments (VJ hotkey grid) for the scenes of the show.
/// Keys 1-0, Q-P, A-L, Z-M are auto-assigned to scenes in list order. Bare key stages
/// the scene, Shift+key stages it and takes it to Live.
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(ShowManager *show, QObject *parent = nullptr);

    /// Release keys whose scene is gone and auto-assign keys to new scenes.
    void syncWithShow();

    /// Replace all bindings atomically (used by the hotkey editor).
    void applyBindings(const QMap<QString, Qt::Key> &bindings);

    /// Assign a single binding; returns false if the key is taken by another scene.
    bool setBinding(const QString &sceneId, Qt::Key key);
    void clearBinding(const QString &sceneId);

    Qt::Key bindingForScene(const QString &sceneId) const;
    QString sceneForKey(Qt::Key key) const;

    /// Session persistence.
    QJsonArray hotkeysJson() const;
    /// Restore from a session; unbound scenes are then auto-assigned.
    void restoreHotkeys(const QJsonArray &hotkeys);

    static const QList<Qt::Key> &hotkeySequence();
    static bool isBindableKey(Qt::Key key);

    /// True if `combo` (Qt::Key | modifiers) is in a focused item's `hotkeyReservedKeys` list.
    static bool isReserved(int combo, const QVariantList &reserved);

    /// Global app shortcuts (Main.qml). They win over scene hotkeys: a scene cannot bind a key an
    /// app action uses, and legacy scene bindings on such keys are dropped on load.
    AppShortcuts *appShortcuts() const { return m_actions; }
    /// Rebinds an action to a single chord. Returns an error message, empty on success.
    QString setActionSequence(const QString &id, const QKeySequence &seq);
    void clearActionSequence(const QString &id);
    /// Returns an error message if the default chord is taken now, empty on success.
    QString resetAction(const QString &id);
    void resetAllActions();

    /// Portable profile (scene id -> key, app action overrides), for import/export and QSettings.
    QJsonObject exportProfile() const;
    bool importProfile(const QJsonObject &profile, QStringList *warnings = nullptr);

    /// While suspended (modal UI open) the filter never fires scene hotkeys.
    void setSuspended(bool suspended) { m_suspended = suspended; }
    bool suspended() const { return m_suspended; }

    void saveSettingsProfile() const;
    void loadSettingsProfile();

signals:
    void bindingsChanged();
    void sceneTriggered(const QString &sceneId, bool takeNow);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ShowManager *m_show;
    AppShortcuts *m_actions;
    bool m_suspended = false;

    QMap<QString, Qt::Key> m_sceneHotkeys;
    QMap<Qt::Key, QString> m_keyToScene;
    QMap<QString, Qt::Key> m_settingsProfile;

    void bindKey(const QString &sceneId, Qt::Key key);
    void releaseScene(const QString &sceneId);
    /// Drops scene bindings on keys an app action uses, then re-assigns those scenes.
    void evictScenesUsingActionKeys();
    QString sceneConflictMessage(const QKeySequence &seq) const;
};
