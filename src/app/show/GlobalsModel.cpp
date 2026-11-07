#include "app/show/GlobalsModel.h"

#include "app/show/ShowManager.h"

GlobalsModel::GlobalsModel(ShowManager *show) : QAbstractListModel(show), m_show(show)
{
    connect(show, &ShowManager::globalAboutToBeInserted, this, [this](int row) { beginInsertRows({}, row, row); });
    connect(show, &ShowManager::globalInserted, this, [this] { endInsertRows(); });
    connect(show, &ShowManager::globalAboutToBeRemoved, this, [this](int row) { beginRemoveRows({}, row, row); });
    connect(show, &ShowManager::globalRemoved, this, [this] { endRemoveRows(); });
    connect(show, &ShowManager::globalUpdated, this, [this](int row) { emit dataChanged(index(row), index(row)); });
    connect(show, &ShowManager::globalsReset, this, [this] {
        beginResetModel();
        endResetModel();
    });
}

int GlobalsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_show->globals().size());
}

QVariant GlobalsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_show->globals().size())
        return {};
    const ShowGlobal &g = m_show->globals().at(index.row());
    switch (role) {
    case NameRole: return g.name;
    case ValueRole: return g.value;
    case MinRole: return g.min;
    case MaxRole: return g.max;
    }
    return {};
}

QHash<int, QByteArray> GlobalsModel::roleNames() const
{
    return {{NameRole, "name"}, {ValueRole, "value"}, {MinRole, "min"}, {MaxRole, "max"}};
}
