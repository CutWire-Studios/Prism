#pragma once

#include <QAbstractListModel>
#include <functional>

class ShowManager;

/// The show's scenes as a list for QML.
class SceneListModel : public QAbstractListModel {
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

    explicit SceneListModel(ShowManager *show);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Display text of the key bound to a scene; empty when unbound.
    void setHotkeyLookup(std::function<QString(const QString &)> f) { m_hotkey = std::move(f); }
    /// Non-empty (a tooltip) when a scene has missing media or devices.
    void setWarningLookup(std::function<QString(const QString &)> f) { m_warning = std::move(f); }
    /// Re-reads every row (lookups changed).
    void refresh();

private:
    std::function<QString(const QString &)> m_hotkey, m_warning;
    ShowManager *m_show;
};
