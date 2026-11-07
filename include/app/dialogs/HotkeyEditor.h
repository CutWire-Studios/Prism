#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class HotkeyManager;
class ShowManager;

/// QML-facing view of HotkeyManager's scene key bindings. Edits apply and persist
/// immediately; a key already used by another scene is refused.
class HotkeyEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Rows {label, chord, cutChord}; chord (the bare key, stages) is empty when unbound.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)

    /// Action id -> list of sequence strings; bind Shortcut.sequences to `appSequences["id"]` so rebinding applies live.
    Q_PROPERTY(QVariantMap appSequences READ appSequences NOTIFY appRowsChanged)
    /// Rows {id, label, category, chord, overridden} in registry order; chord is the primary sequence.
    Q_PROPERTY(QVariantList appRows READ appRows NOTIFY appRowsChanged)

public:
    explicit HotkeyEditor(QObject *parent = nullptr);

    QVariantList rows() const { return m_rows; }
    QVariantMap appSequences() const { return m_appSequences; }
    QVariantList appRows() const { return m_appRows; }

    /// Same contract as setChord, for app shortcut `id`; modifiers are allowed.
    Q_INVOKABLE QVariantMap setAppChord(const QString &id, const QString &chord);
    Q_INVOKABLE void clearAppChord(const QString &id);
    Q_INVOKABLE QVariantMap resetAppChord(const QString &id);
    Q_INVOKABLE void resetAllAppChords();

    /// Returns {ok, message}. Only unmodified keys are bindable (Shift+key stages and cuts to Live).
    Q_INVOKABLE QVariantMap setChord(int row, const QString &chord);
    Q_INVOKABLE void clearRow(int row);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE QVariantMap importProfile(const QString &path);
    Q_INVOKABLE QVariantMap exportProfile(const QString &path);
    Q_INVOKABLE void refresh();

signals:
    void rowsChanged();
    void appRowsChanged();

private:
    HotkeyManager *manager() const;
    ShowManager *show() const;

    QStringList m_sceneIds;
    QVariantList m_rows;
    QVariantMap m_appSequences;
    QVariantList m_appRows;
    bool m_connected = false;

    void refreshApp();
};
