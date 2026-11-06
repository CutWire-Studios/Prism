#include "app/show/CueListModel.h"

#include "app/show/ShowManager.h"

CueListModel::CueListModel(ShowManager *show) : QAbstractListModel(show), m_show(show)
{
    auto reload = [this] {
        beginResetModel();
        endResetModel();
    };
    auto refresh = [this] {
        if (rowCount() > 0)
            emit dataChanged(index(0), index(rowCount() - 1));
    };
    connect(show, &ShowManager::cuesReset, this, reload);
    connect(show, &ShowManager::cueAdded, this, reload);
    connect(show, &ShowManager::liveCueChanged, this, refresh);
    connect(show, &ShowManager::stagingCueChanged, this, refresh);
    connect(show, &ShowManager::editedCueChanged, this, refresh);
    connect(show, &ShowManager::changed, this, refresh);
}

int CueListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_show->cues().size());
}

QVariant CueListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_show->cues().size())
        return {};
    const Cue *cue = m_show->cues().at(index.row());
    switch (role) {
    case IdRole: return cue->id();
    case NameRole: return cue->name();
    case ColorRole: return cue->color();
    case IsLiveRole: return cue->id() == m_show->liveCueId();
    case IsStagingRole: return cue->id() == m_show->stagingCueId();
    case IsEditedRole: return m_show->inCue() && cue->id() == m_show->editedCueId();
    case EndActionRole: return Cue::endActionName(cue->endAction());
    case HasTransitionRole: return cue->incoming().has_value();
    }
    return {};
}

QHash<int, QByteArray> CueListModel::roleNames() const
{
    return {{IdRole, "cueId"}, {NameRole, "name"}, {ColorRole, "color"},
            {IsLiveRole, "isLive"}, {IsStagingRole, "isStaging"}, {IsEditedRole, "isEdited"},
            {EndActionRole, "endAction"}, {HasTransitionRole, "hasTransition"}};
}
