#include "app/panels/AssetLibraryModel.h"

#include "app/AppController.h"
#include "app/graph/GraphRuntime.h"
#include "core/project/ClipManager.h"

#include <QFileInfo>

AssetLibraryModel::AssetLibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_poll.setInterval(400);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (signature() != m_signature)
            refresh();
    });
    m_poll.start();
    refresh();
}

ClipManager *AssetLibraryModel::clips() const
{
    auto *app = AppController::instance();
    return app ? app->clipManager() : nullptr;
}

QString AssetLibraryModel::signature() const
{
    ClipManager *cm = clips();
    if (!cm)
        return {};
    QString sig = m_folderId + QLatin1Char('|') + m_search + QLatin1Char('|');
    for (const QString &c : cm->getClips())
        sig += c + QLatin1Char('\n') + cm->folderOf(c) + QLatin1Char('\n');
    for (const ClipManager::BinFolder &f : cm->folders())
        sig += f.id + QLatin1Char(':') + f.name + QLatin1Char(':') + f.parentId + QLatin1Char('\n');
    return sig;
}

QList<AssetLibraryModel::Entry> AssetLibraryModel::collect() const
{
    QList<Entry> out;
    ClipManager *cm = clips();
    if (!cm)
        return out;
    const QString needle = m_search.trimmed();
    if (needle.isEmpty()) {
        for (const ClipManager::BinFolder &f : cm->foldersIn(m_folderId))
            out.append({f.id, f.name, true});
        for (const QString &p : cm->clipsIn(m_folderId))
            out.append({p, QFileInfo(p).fileName(), false});
    } else {
        for (const QString &p : cm->getClips()) {
            const QString name = QFileInfo(p).fileName();
            if (name.contains(needle, Qt::CaseInsensitive))
                out.append({p, name, false});
        }
    }
    return out;
}

void AssetLibraryModel::refresh()
{
    beginResetModel();
    m_entries = collect();
    m_signature = signature();
    endResetModel();
    emit changed();
}

int AssetLibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant AssetLibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case KeyRole: return e.key;
    case NameRole: return e.name;
    case IsFolderRole: return e.folder;
    case SuffixRole: return e.folder ? QString() : QFileInfo(e.key).suffix().toUpper();
    case IsAudioRole: return !e.folder && ClipManager::isAudioPath(e.key);
    }
    return {};
}

QHash<int, QByteArray> AssetLibraryModel::roleNames() const
{
    return {{KeyRole, "key"}, {NameRole, "name"}, {IsFolderRole, "isFolder"},
            {SuffixRole, "suffix"}, {IsAudioRole, "isAudio"}};
}

void AssetLibraryModel::setFolderId(const QString &id)
{
    if (m_folderId == id)
        return;
    m_folderId = id;
    emit folderIdChanged();
    refresh();
}

void AssetLibraryModel::setSearch(const QString &s)
{
    if (m_search == s)
        return;
    m_search = s;
    emit searchChanged();
    refresh();
}

QString AssetLibraryModel::folderName() const
{
    ClipManager *cm = clips();
    return cm && !m_folderId.isEmpty() ? cm->folder(m_folderId).name : QString();
}

QString AssetLibraryModel::parentFolderId() const
{
    ClipManager *cm = clips();
    return cm && !m_folderId.isEmpty() ? cm->folder(m_folderId).parentId : QString();
}

int AssetLibraryModel::clipCount() const
{
    ClipManager *cm = clips();
    return cm ? cm->getClipCount() : 0;
}

QString AssetLibraryModel::createFolder(const QString &name)
{
    ClipManager *cm = clips();
    if (!cm || name.trimmed().isEmpty())
        return {};
    const QString id = cm->createFolder(name.trimmed(), m_folderId);
    refresh();
    return id;
}

bool AssetLibraryModel::renameFolder(const QString &id, const QString &name)
{
    ClipManager *cm = clips();
    const bool ok = cm && cm->renameFolder(id, name.trimmed());
    refresh();
    return ok;
}

void AssetLibraryModel::removeFolder(const QString &id)
{
    if (ClipManager *cm = clips())
        cm->removeFolder(id);
    refresh();
}

void AssetLibraryModel::removeClip(const QString &path)
{
    if (ClipManager *cm = clips()) {
        const int i = cm->getClips().indexOf(path);
        if (i >= 0)
            cm->removeClip(i);
    }
    refresh();
}

bool AssetLibraryModel::moveClip(const QString &path, const QString &folderId)
{
    ClipManager *cm = clips();
    const bool ok = cm && cm->moveClip(path, folderId);
    refresh();
    return ok;
}

quint64 AssetLibraryModel::addToGraph(const QString &path)
{
    auto *app = AppController::instance();
    if (!app || !app->graphRuntime())
        return 0;
    return app->graphRuntime()->addClipNode(path);
}
