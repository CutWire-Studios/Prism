#pragma once

#include <QObject>
#include <QMap>
#include <QList>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <Qt>

class ShowManager;

/// Manages keyboard shortcut assignments (VJ hotkey grid) for the cues of the show.
/// Keys 1-0, Q-P, A-L, Z-M are auto-assigned to cues in list order. Bare key stages
/// the cue, Shift+key stages it and takes it to Live.
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(ShowManager *show, QObject *parent = nullptr);

    /// Release keys whose cue is gone and auto-assign keys to new cues.
    void syncWithShow();

    /// Replace all bindings atomically (used by the hotkey editor).
    void applyBindings(const QMap<QString, Qt::Key> &bindings);

    /// Assign a single binding; returns false if the key is taken by another cue.
    bool setBinding(const QString &cueId, Qt::Key key);
    void clearBinding(const QString &cueId);

    Qt::Key bindingForCue(const QString &cueId) const;
    QString cueForKey(Qt::Key key) const;

    /// Session persistence.
    QJsonArray hotkeysJson() const;
    /// Restore from a session; unbound cues are then auto-assigned.
    void restoreHotkeys(const QJsonArray &hotkeys);

    static const QList<Qt::Key> &hotkeySequence();
    static bool isBindableKey(Qt::Key key);

    /// Portable profile (cue id -> key), for import/export and QSettings.
    QJsonObject exportProfile() const;
    bool importProfile(const QJsonObject &profile, QStringList *warnings = nullptr);

    void saveSettingsProfile() const;
    void loadSettingsProfile();

signals:
    void bindingsChanged();
    void cueTriggered(const QString &cueId, bool takeNow);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ShowManager *m_show;

    QMap<QString, Qt::Key> m_cueHotkeys;
    QMap<Qt::Key, QString> m_keyToCue;
    QMap<QString, Qt::Key> m_settingsProfile;

    void bindKey(const QString &cueId, Qt::Key key);
    void releaseCue(const QString &cueId);
};
