#pragma once

#include <QAbstractListModel>

class ShowManager;

/// The show's globals as a list for QML. Value edits only emit dataChanged, so delegates survive a slider drag.
class GlobalsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { NameRole = Qt::UserRole + 1, ValueRole, MinRole, MaxRole };

    explicit GlobalsModel(ShowManager *show);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    ShowManager *m_show;
};
