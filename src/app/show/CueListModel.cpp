#include "app/show/CueListModel.h"

#include "app/show/ShowManager.h"

CueListModel::CueListModel(ShowManager *show) : QAbstractListModel(show), m_show(show)
{
    auto reload = [this] {
        beginResetModel();
        endResetModel();
    };
    auto refresh = [this] { this->refresh(); };
    connect(show, &ShowManager::cuesReset, this, reload);
    connect(show, &ShowManager::cueAdded, this, reload);
    connect(show, &ShowManager::liveCueChanged, this, refresh);
    connect(show, &ShowManager::stagingCueChanged, this, refresh);
    connect(show, &ShowManager::editedCueChanged, this, refresh);
    connect(show, &ShowManager::changed, this, refresh);
    connect(show, &ShowManager::thumbnailChanged, this, refresh);
}

void CueListModel::refresh()
{
    if (rowCount() > 0)
        emit dataChanged(index(0), index(rowCount() - 1));
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
    case TransitionModeRole: return cue->incoming() ? cue->incoming()->mode : -1;
    case TransitionMsRole: return cue->incoming() ? cue->incoming()->durationMs : 0;
    case ThumbnailRole:
        return cue->thumbnail().isEmpty()
            ? QString()
            : QStringLiteral("image://cuethumb/%1?%2").arg(cue->id()).arg(cue->thumbnailRev());
    case HotkeyRole: return m_hotkey ? m_hotkey(cue->id()) : QString();
    case WarningRole: return m_warning ? m_warning(cue->id()) : QString();
    }
    return {};
}

QHash<int, QByteArray> CueListModel::roleNames() const
{
    return {{IdRole, "cueId"}, {NameRole, "name"}, {ColorRole, "color"},
            {IsLiveRole, "isLive"}, {IsStagingRole, "isStaging"}, {IsEditedRole, "isEdited"},
            {EndActionRole, "endAction"}, {HasTransitionRole, "hasTransition"},
            {TransitionModeRole, "transitionMode"}, {TransitionMsRole, "transitionMs"},
            {ThumbnailRole, "thumbnail"}, {HotkeyRole, "hotkey"}, {WarningRole, "warning"}};
}
