#pragma once

#include <QAbstractListModel>
#include <functional>

class ShowManager;

/// The show's cues as a list for QML.
class CueListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ColorRole,
        IsLiveRole,
        IsStagingRole,
        IsEditedRole,
        EndActionRole,
        HasTransitionRole,
        TransitionModeRole,
        TransitionMsRole,
        ThumbnailRole,
        HotkeyRole,
        WarningRole,
    };

    explicit CueListModel(ShowManager *show);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Display text of the key bound to a cue; empty when unbound.
    void setHotkeyLookup(std::function<QString(const QString &)> f) { m_hotkey = std::move(f); }
    /// Non-empty (a tooltip) when a cue has missing media or devices.
    void setWarningLookup(std::function<QString(const QString &)> f) { m_warning = std::move(f); }
    /// Re-reads every row (lookups changed).
    void refresh();

private:
    std::function<QString(const QString &)> m_hotkey, m_warning;
    ShowManager *m_show;
};
