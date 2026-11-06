#pragma once

#include <QAbstractListModel>

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
    };

    explicit CueListModel(ShowManager *show);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    ShowManager *m_show;
};
