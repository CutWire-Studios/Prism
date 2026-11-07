#include "app/show/SceneListModel.h"

#include "app/show/ShowManager.h"

SceneListModel::SceneListModel(ShowManager *show) : QAbstractListModel(show), m_show(show)
{
    auto reload = [this] {
        beginResetModel();
        endResetModel();
    };
    auto refresh = [this] { this->refresh(); };
    connect(show, &ShowManager::scenesReset, this, reload);
    connect(show, &ShowManager::sceneAdded, this, reload);
    connect(show, &ShowManager::liveSceneChanged, this, refresh);
    connect(show, &ShowManager::stagingSceneChanged, this, refresh);
    connect(show, &ShowManager::editedSceneChanged, this, refresh);
    connect(show, &ShowManager::changed, this, refresh);
    connect(show, &ShowManager::thumbnailChanged, this, refresh);
}

void SceneListModel::refresh()
{
    if (rowCount() > 0)
        emit dataChanged(index(0), index(rowCount() - 1));
}

int SceneListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_show->scenes().size());
}

QVariant SceneListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_show->scenes().size())
        return {};
    const Scene *scene = m_show->scenes().at(index.row());
    switch (role) {
    case IdRole: return scene->id();
    case NameRole: return scene->name();
    case ColorRole: return scene->color();
    case IsLiveRole: return scene->id() == m_show->liveSceneId();
    case IsStagingRole: return scene->id() == m_show->stagingSceneId();
    case IsEditedRole: return m_show->inScene() && scene->id() == m_show->editedSceneId();
    case EndActionRole: return Scene::endActionName(scene->endAction());
    case HasTransitionRole: return scene->incoming().has_value();
    case TransitionModeRole: return scene->incoming() ? scene->incoming()->mode : -1;
    case TransitionMsRole: return scene->incoming() ? scene->incoming()->durationMs : 0;
    case ThumbnailRole:
        return scene->thumbnail().isEmpty()
            ? QString()
            : QStringLiteral("image://scenethumb/%1?%2").arg(scene->id()).arg(scene->thumbnailRev());
    case HotkeyRole: return m_hotkey ? m_hotkey(scene->id()) : QString();
    case WarningRole: return m_warning ? m_warning(scene->id()) : QString();
    case ResidentRole: return scene->keepResident();
    case PostRole: return scene->id() == m_show->postSceneId();
    }
    return {};
}

QHash<int, QByteArray> SceneListModel::roleNames() const
{
    return {{IdRole, "sceneId"}, {NameRole, "name"}, {ColorRole, "color"},
            {IsLiveRole, "isLive"}, {IsStagingRole, "isStaging"}, {IsEditedRole, "isEdited"},
            {EndActionRole, "endAction"}, {HasTransitionRole, "hasTransition"},
            {TransitionModeRole, "transitionMode"}, {TransitionMsRole, "transitionMs"},
            {ThumbnailRole, "thumbnail"}, {HotkeyRole, "hotkey"}, {WarningRole, "warning"},
            {ResidentRole, "keepResident"}, {PostRole, "isPost"}};
}
