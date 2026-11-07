#pragma once

#include <QObject>
#include <QMap>
#include <QList>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <Qt>

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

    /// Portable profile (scene id -> key), for import/export and QSettings.
    QJsonObject exportProfile() const;
    bool importProfile(const QJsonObject &profile, QStringList *warnings = nullptr);

    void saveSettingsProfile() const;
    void loadSettingsProfile();

signals:
    void bindingsChanged();
    void sceneTriggered(const QString &sceneId, bool takeNow);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ShowManager *m_show;

    QMap<QString, Qt::Key> m_sceneHotkeys;
    QMap<Qt::Key, QString> m_keyToScene;
    QMap<QString, Qt::Key> m_settingsProfile;

    void bindKey(const QString &sceneId, Qt::Key key);
    void releaseScene(const QString &sceneId);
};
