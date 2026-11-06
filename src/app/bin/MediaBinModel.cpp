#include "app/bin/MediaBinModel.h"

#include "app/AppController.h"
#include "app/bin/MediaMetaCache.h"
#include "app/graph/GraphRuntime.h"
#include "core/graph/ClipGraph.h"
#include "core/media/MediaFormats.h"
#include "core/project/ClipManager.h"
#include "core/sources/SourceDescriptor.h"

#include <QCollator>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QThreadPool>

#include <algorithm>
#include <cstdlib>
#include <functional>

namespace {

constexpr int kWalkVisitLimit = 20000;

int kindRank(const QString &path)
{
    if (MediaFormats::isImagePath(path))
        return 1;
    if (MediaFormats::isAudioPath(path))
        return 2;
    return 0;
}

QCollator makeCollator()
{
    QCollator c;
    c.setNumericMode(true);
    c.setCaseSensitivity(Qt::CaseInsensitive);
    return c;
}

} // namespace

MediaBinModel::MediaBinModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QSettings settings;
    m_viewMode = settings.value(QStringLiteral("mediaBin/viewMode"), QStringLiteral("grid")).toString();
    if (m_viewMode != QLatin1String("grid") && m_viewMode != QLatin1String("list")
        && m_viewMode != QLatin1String("tree"))
        m_viewMode = QStringLiteral("grid");
    m_sortMode = settings.value(QStringLiteral("mediaBin/sortMode"), QStringLiteral("name")).toString();
    if (m_sortMode != QLatin1String("name") && m_sortMode != QLatin1String("type")
        && m_sortMode != QLatin1String("added"))
        m_sortMode = QStringLiteral("name");

    m_poll.setInterval(250);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        ClipManager *cm = clips();
        if (cm && cm->revision() != m_seenRevision)
            refresh();
    });
    m_poll.start();

    m_rescan.setInterval(10000);
    connect(&m_rescan, &QTimer::timeout, this, &MediaBinModel::rescan);
    m_rescan.start();

    connect(MediaMetaCache::instance(), &MediaMetaCache::metaChanged, this, [this](const QString &path) {
        const int row = rowOfPath(path);
        if (row >= 0)
            emit dataChanged(index(row), index(row));
    });

    refresh();
}

ClipManager *MediaBinModel::clips() const
{
    auto *app = AppController::instance();
    return app ? app->clipManager() : nullptr;
}

int MediaBinModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> MediaBinModel::roleNames() const
{
    return {
        {KeyRole, "key"}, {PathRole, "path"}, {NameRole, "name"}, {IsFolderRole, "isFolder"},
        {KindRole, "kind"}, {DurationTextRole, "duration"}, {DurationRole, "durationSeconds"},
        {FormatRole, "format"}, {SuffixRole, "suffix"}, {FileSizeRole, "fileSize"},
        {ThumbKeyRole, "thumbKey"}, {ThumbRevRole, "thumbRev"}, {MissingRole, "missing"},
        {ProbingRole, "probing"}, {DepthRole, "depth"}, {ExpandedRole, "expanded"},
        {ExpandableRole, "expandable"}, {ItemCountRole, "itemCount"}, {UrlRole, "url"},
    };
}

QVariant MediaBinModel::data(const QModelIndex &index, int role) const
{
    ClipManager *cm = clips();
    if (!cm || !index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());

    switch (role) {
    case KeyRole: return r.key;
    case IsFolderRole: return r.folder;
    case DepthRole: return r.depth;
    case ExpandedRole: return r.expanded;
    case ExpandableRole: return r.expandable;
    default: break;
    }

    if (r.folder) {
        switch (role) {
        case NameRole: return cm->folder(r.key).name;
        case KindRole: return QStringLiteral("folder");
        case ItemCountRole: return cm->clipsIn(r.key).size() + cm->foldersIn(r.key).size();
        case PathRole:
        case DurationTextRole:
        case FormatRole:
        case SuffixRole:
        case FileSizeRole:
        case ThumbKeyRole:
        case ThumbRevRole:
        case UrlRole: return QString();
        case DurationRole: return 0.0;
        case MissingRole:
        case ProbingRole: return false;
        }
        return {};
    }

    const QString &path = r.key;
    const MediaMeta meta = MediaMetaCache::instance()->meta(path);
    switch (role) {
    case PathRole: return path;
    case NameRole: return cm->displayName(path);
    case KindRole: return meta.kind;
    case DurationTextRole: return meta.durationText();
    case DurationRole: return meta.durationSeconds;
    case FormatRole: return meta.formatText();
    case SuffixRole: return QFileInfo(path).suffix().toUpper();
    case FileSizeRole: return meta.size > 0 ? QLocale().formattedDataSize(meta.size) : QString();
    case ThumbKeyRole:
        return meta.probed && meta.exists && meta.kind != QLatin1String("audio")
            ? MediaMetaCache::instance()->keyFor(path) : QString();
    case ThumbRevRole: return QString::number(meta.mtime);
    case MissingRole: return meta.probed && !meta.exists;
    case ProbingRole: return !meta.probed;
    case ItemCountRole: return 0;
    case UrlRole: return QUrl::fromLocalFile(path).toString();
    }
    return {};
}

QStringList MediaBinModel::sortedClips(QStringList paths) const
{
    ClipManager *cm = clips();
    const QCollator collator = makeCollator();
    const auto byName = [&](const QString &a, const QString &b) {
        return collator.compare(cm->displayName(a), cm->displayName(b)) < 0;
    };
    if (m_sortMode == QLatin1String("added")) {
        std::stable_sort(paths.begin(), paths.end(), [&](const QString &a, const QString &b) {
            return cm->addedOrder(a) > cm->addedOrder(b);
        });
    } else if (m_sortMode == QLatin1String("type")) {
        std::stable_sort(paths.begin(), paths.end(), [&](const QString &a, const QString &b) {
            const int ra = kindRank(a);
            const int rb = kindRank(b);
            return ra != rb ? ra < rb : byName(a, b);
        });
    } else {
        std::stable_sort(paths.begin(), paths.end(), byName);
    }
    return paths;
}

QList<MediaBinModel::Row> MediaBinModel::collect() const
{
    QList<Row> out;
    ClipManager *cm = clips();
    if (!cm)
        return out;

    const QString needle = m_search.trimmed();
    const QCollator collator = makeCollator();
    const auto sortedFolders = [&](const QString &parentId) {
        QList<ClipManager::BinFolder> list = cm->foldersIn(parentId);
        std::stable_sort(list.begin(), list.end(), [&](const auto &a, const auto &b) {
            return collator.compare(a.name, b.name) < 0;
        });
        return list;
    };
    const auto nameHit = [&](const QString &name) {
        return needle.isEmpty() || name.contains(needle, Qt::CaseInsensitive);
    };

    if (m_viewMode != QLatin1String("tree")) {
        for (const ClipManager::BinFolder &f : sortedFolders(m_folderId)) {
            if (nameHit(f.name))
                out.append({true, f.id, 0, false, false});
        }
        for (const QString &p : sortedClips(cm->clipsIn(m_folderId))) {
            if (nameHit(cm->displayName(p)))
                out.append({false, p, 0, false, false});
        }
        return out;
    }

    QHash<QString, QStringList> childClips;
    for (const QString &p : cm->getClips())
        childClips[cm->folderOf(p)].append(p);

    QHash<QString, bool> subtreeHit;
    std::function<bool(const QString &, QSet<QString> &)> subtreeMatches =
        [&](const QString &folderId, QSet<QString> &seen) {
            if (subtreeHit.contains(folderId))
                return subtreeHit.value(folderId);
            if (seen.contains(folderId))
                return false;
            seen.insert(folderId);
            bool hit = false;
            for (const QString &p : childClips.value(folderId)) {
                if (cm->displayName(p).contains(needle, Qt::CaseInsensitive)) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                for (const ClipManager::BinFolder &f : cm->foldersIn(folderId)) {
                    if (f.name.contains(needle, Qt::CaseInsensitive) || subtreeMatches(f.id, seen)) {
                        hit = true;
                        break;
                    }
                }
            }
            subtreeHit.insert(folderId, hit);
            return hit;
        };

    QSet<QString> walked;
    std::function<void(const QString &, int)> walk = [&](const QString &parentId, int depth) {
        if (walked.contains(parentId))
            return;
        walked.insert(parentId);
        for (const ClipManager::BinFolder &f : sortedFolders(parentId)) {
            QSet<QString> seen;
            const bool deepHit = !needle.isEmpty() && subtreeMatches(f.id, seen);
            if (!needle.isEmpty() && !f.name.contains(needle, Qt::CaseInsensitive) && !deepHit)
                continue;
            const bool hasChildren = !cm->foldersIn(f.id).isEmpty() || !childClips.value(f.id).isEmpty();
            const bool open = hasChildren && (needle.isEmpty() ? m_expanded.contains(f.id) : deepHit);
            out.append({true, f.id, depth, open, hasChildren});
            if (open)
                walk(f.id, depth + 1);
        }
        for (const QString &p : sortedClips(childClips.value(parentId))) {
            if (nameHit(cm->displayName(p)))
                out.append({false, p, depth, false, false});
        }
    };
    walk(m_folderId, 0);
    return out;
}

void MediaBinModel::refresh()
{
    ClipManager *cm = clips();
    if (!cm)
        return;
    m_seenRevision = cm->revision();

    if (!m_folderId.isEmpty() && !cm->hasFolder(m_folderId)) {
        m_folderId.clear();
        emit folderIdChanged();
    }

    const QList<Row> rows = collect();
    if (rows == m_rows) {
        if (!rows.isEmpty())
            emit dataChanged(index(0), index(rows.size() - 1));
    } else {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    }
    emit changed();
}

void MediaBinModel::rescan()
{
    if (!m_rows.isEmpty())
        MediaMetaCache::instance()->rescan();
}

void MediaBinModel::touched()
{
    if (auto *app = AppController::instance())
        app->notifyLibraryChanged();
    refresh();
}

int MediaBinModel::rowOfPath(const QString &path) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!m_rows.at(i).folder && m_rows.at(i).key == path)
            return i;
    }
    return -1;
}

void MediaBinModel::setFolderId(const QString &id)
{
    ClipManager *cm = clips();
    const QString target = cm && cm->hasFolder(id) ? id : QString();
    if (m_folderId == target)
        return;
    m_folderId = target;
    emit folderIdChanged();
    refresh();
}

void MediaBinModel::setSearch(const QString &s)
{
    if (m_search == s)
        return;
    m_search = s;
    emit searchChanged();
    refresh();
}

void MediaBinModel::setViewMode(const QString &mode)
{
    if (m_viewMode == mode || (mode != QLatin1String("grid") && mode != QLatin1String("list")
                               && mode != QLatin1String("tree")))
        return;
    m_viewMode = mode;
    QSettings().setValue(QStringLiteral("mediaBin/viewMode"), mode);
    emit viewModeChanged();
    refresh();
}

void MediaBinModel::setSortMode(const QString &mode)
{
    if (m_sortMode == mode || (mode != QLatin1String("name") && mode != QLatin1String("type")
                               && mode != QLatin1String("added")))
        return;
    m_sortMode = mode;
    QSettings().setValue(QStringLiteral("mediaBin/sortMode"), mode);
    emit sortModeChanged();
    refresh();
}

QString MediaBinModel::folderName() const
{
    ClipManager *cm = clips();
    return cm && !m_folderId.isEmpty() ? cm->folder(m_folderId).name : QString();
}

QString MediaBinModel::parentFolderId() const
{
    ClipManager *cm = clips();
    return cm && !m_folderId.isEmpty() ? cm->folder(m_folderId).parentId : QString();
}

QVariantList MediaBinModel::breadcrumbs() const
{
    ClipManager *cm = clips();
    QVariantList chain;
    if (cm) {
        QSet<QString> visited;
        QString id = m_folderId;
        while (!id.isEmpty() && !visited.contains(id) && cm->hasFolder(id)) {
            visited.insert(id);
            const ClipManager::BinFolder f = cm->folder(id);
            chain.prepend(QVariantMap{{QStringLiteral("id"), f.id}, {QStringLiteral("name"), f.name}});
            id = f.parentId;
        }
    }
    chain.prepend(QVariantMap{{QStringLiteral("id"), QString()}, {QStringLiteral("name"), tr("Media")}});
    return chain;
}

int MediaBinModel::assetCount() const
{
    ClipManager *cm = clips();
    return cm ? cm->getClipCount() : 0;
}

int MediaBinModel::folderCount() const
{
    ClipManager *cm = clips();
    return cm ? cm->folders().size() : 0;
}

int MediaBinModel::libraryLimit() const
{
    return ClipManager::MaxLibrarySize;
}

QString MediaBinModel::createFolder(const QString &name)
{
    ClipManager *cm = clips();
    const QString trimmed = name.trimmed();
    if (!cm || trimmed.isEmpty())
        return {};
    const QString id = cm->createFolder(trimmed, m_folderId);
    touched();
    return id;
}

bool MediaBinModel::renameFolder(const QString &id, const QString &name)
{
    ClipManager *cm = clips();
    const QString trimmed = name.trimmed();
    const bool ok = cm && !trimmed.isEmpty() && cm->renameFolder(id, trimmed);
    if (ok)
        touched();
    return ok;
}

bool MediaBinModel::isFolderOrDescendant(const QString &candidateId, const QString &ancestorId) const
{
    ClipManager *cm = clips();
    QSet<QString> seen;
    QString id = candidateId;
    while (cm && !id.isEmpty() && !seen.contains(id)) {
        if (id == ancestorId)
            return true;
        seen.insert(id);
        id = cm->folder(id).parentId;
    }
    return false;
}

bool MediaBinModel::moveFolder(const QString &id, const QString &newParentId)
{
    ClipManager *cm = clips();
    const bool ok = cm && cm->folder(id).parentId != newParentId && cm->moveFolder(id, newParentId);
    if (ok)
        touched();
    return ok;
}

bool MediaBinModel::deleteFolder(const QString &id)
{
    ClipManager *cm = clips();
    if (!cm || !cm->hasFolder(id))
        return false;
    const QString parent = cm->folder(id).parentId;
    const bool viewingInside = isFolderOrDescendant(m_folderId, id);
    for (const QString &p : cm->clipsIn(id))
        cm->moveClip(p, parent);
    for (const ClipManager::BinFolder &f : cm->foldersIn(id))
        cm->moveFolder(f.id, parent);
    cm->removeFolder(id);
    if (viewingInside) {
        m_folderId = parent;
        emit folderIdChanged();
    }
    m_expanded.remove(id);
    touched();
    return true;
}

int MediaBinModel::moveItems(const QStringList &paths, const QString &folderId)
{
    ClipManager *cm = clips();
    if (!cm)
        return 0;
    int moved = 0;
    for (const QString &p : paths) {
        if (cm->folderOf(p) != folderId && cm->moveClip(p, folderId))
            ++moved;
    }
    if (moved > 0)
        touched();
    return moved;
}

int MediaBinModel::removeItems(const QStringList &paths)
{
    ClipManager *cm = clips();
    if (!cm)
        return 0;
    int removed = 0;
    for (const QString &p : paths) {
        const int i = cm->getClips().indexOf(p);
        if (i >= 0) {
            cm->removeClip(i);
            ++removed;
        }
    }
    if (removed > 0)
        touched();
    return removed;
}

bool MediaBinModel::renameItem(const QString &path, const QString &name)
{
    ClipManager *cm = clips();
    if (!cm || name.trimmed().isEmpty() || !cm->getClips().contains(path))
        return false;
    cm->setDisplayName(path, name);
    touched();
    return true;
}

QString MediaBinModel::folderPath(const QString &id) const
{
    ClipManager *cm = clips();
    QStringList names;
    QSet<QString> seen;
    QString cur = id;
    while (cm && !cur.isEmpty() && !seen.contains(cur) && cm->hasFolder(cur)) {
        seen.insert(cur);
        const ClipManager::BinFolder f = cm->folder(cur);
        names.prepend(f.name);
        cur = f.parentId;
    }
    return names.join(QStringLiteral(" > "));
}

QVariantList MediaBinModel::moveTargets(const QStringList &paths, const QString &movingFolderId) const
{
    QVariantList out;
    ClipManager *cm = clips();
    if (!cm)
        return out;
    const auto add = [&](const QString &id, const QString &name, const QString &path) {
        out.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
                               {QStringLiteral("path"), path}});
    };
    const QString rootName = tr("Media");

    QList<ClipManager::BinFolder> folders = cm->folders();
    const QCollator collator = makeCollator();
    std::stable_sort(folders.begin(), folders.end(), [&](const auto &a, const auto &b) {
        return collator.compare(folderPath(a.id), folderPath(b.id)) < 0;
    });

    if (!movingFolderId.isEmpty()) {
        const QString currentParent = cm->folder(movingFolderId).parentId;
        if (!currentParent.isEmpty())
            add(QString(), rootName, rootName);
        for (const ClipManager::BinFolder &f : std::as_const(folders)) {
            if (f.id == currentParent || isFolderOrDescendant(f.id, movingFolderId))
                continue;
            add(f.id, f.name, folderPath(f.id));
        }
        return out;
    }

    QSet<QString> current;
    for (const QString &p : paths)
        current.insert(cm->folderOf(p));
    const bool single = current.size() == 1;
    const QString exclude = single ? *current.cbegin() : QString();
    if (!(single && exclude.isEmpty()))
        add(QString(), rootName, rootName);
    for (const ClipManager::BinFolder &f : std::as_const(folders)) {
        if (!(single && f.id == exclude))
            add(f.id, f.name, folderPath(f.id));
    }
    return out;
}

void MediaBinModel::toggleExpanded(const QString &folderId)
{
    if (!m_expanded.remove(folderId))
        m_expanded.insert(folderId);
    refresh();
}

void MediaBinModel::expandAll()
{
    if (ClipManager *cm = clips()) {
        for (const ClipManager::BinFolder &f : cm->folders())
            m_expanded.insert(f.id);
    }
    refresh();
}

void MediaBinModel::collapseAll()
{
    m_expanded.clear();
    refresh();
}

QStringList MediaBinModel::assetKeys() const
{
    QStringList keys;
    for (const Row &r : m_rows) {
        if (!r.folder)
            keys.append(r.key);
    }
    return keys;
}

QStringList MediaBinModel::rangeKeys(const QString &anchor, const QString &target) const
{
    const QStringList keys = assetKeys();
    const int a = keys.indexOf(anchor);
    const int t = keys.indexOf(target);
    if (a < 0 || t < 0)
        return {target};
    return keys.mid(std::min(a, t), std::abs(a - t) + 1);
}

void MediaBinModel::importUrls(const QList<QUrl> &urls)
{
    ClipManager *cm = clips();
    if (!cm)
        return;
    QStringList files;
    int skipped = 0;
    for (const QUrl &url : urls) {
        const QString p = url.toLocalFile();
        if (p.isEmpty())
            continue;
        if (QFileInfo(p).isDir())
            importFolder(url);
        else if (MediaFormats::isMediaPath(p))
            files.append(p);
        else
            ++skipped;
    }
    if (files.isEmpty()) {
        if (skipped > 0)
            emit importFinished(0, skipped, false);
        return;
    }

    const QStringList existing = cm->getClips();
    int fresh = 0;
    QSet<QString> seen;
    for (const QString &f : std::as_const(files)) {
        if (!existing.contains(f) && !seen.contains(f))
            ++fresh;
        seen.insert(f);
    }
    const int before = cm->getClipCount();
    cm->addFiles(files, m_folderId);
    const int added = cm->getClipCount() - before;
    touched();
    emit importFinished(added, skipped, added < fresh);
}

bool MediaBinModel::importFolder(const QUrl &url)
{
    ClipManager *cm = clips();
    const QString root = url.toLocalFile();
    if (!cm || m_importingFolder || root.isEmpty() || !QFileInfo(root).isDir())
        return false;

    m_importingFolder = true;
    emit importingFolderChanged();
    const int budget = std::max(0, ClipManager::MaxLibrarySize - cm->getClipCount());
    QPointer<MediaBinModel> self(this);

    QThreadPool::globalInstance()->start([self, root, budget] {
        int remaining = budget;
        int skipped = 0;
        int visited = 0;
        bool truncated = false;
        std::function<DirNode(const QString &)> walk = [&](const QString &dirPath) {
            DirNode node;
            const QDir dir(dirPath);
            node.name = dir.dirName().isEmpty() ? dirPath : dir.dirName();
            for (const QString &f : dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
                if (++visited > kWalkVisitLimit) {
                    truncated = true;
                    break;
                }
                const QString abs = dir.absoluteFilePath(f);
                if (!MediaFormats::isMediaPath(abs))
                    ++skipped;
                else if (remaining > 0) {
                    node.files.append(abs);
                    --remaining;
                } else {
                    truncated = true;
                }
            }
            for (const QString &sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
                if (++visited > kWalkVisitLimit) {
                    truncated = true;
                    break;
                }
                node.subs.push_back(walk(dir.absoluteFilePath(sub)));
            }
            return node;
        };
        const DirNode tree = walk(root);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, tree, skipped, truncated] {
            if (self)
                self->finishFolderImport(tree, skipped, truncated);
        }, Qt::QueuedConnection);
    });
    return true;
}

void MediaBinModel::applyDirTree(const DirNode &node, const QString &parentId, int &folders, int &files)
{
    ClipManager *cm = clips();
    const QString id = cm->createFolder(node.name, parentId);
    ++folders;
    const int before = cm->getClipCount();
    cm->addFiles(node.files, id);
    files += cm->getClipCount() - before;
    for (const DirNode &sub : node.subs)
        applyDirTree(sub, id, folders, files);
}

void MediaBinModel::finishFolderImport(const DirNode &root, int skipped, bool truncated)
{
    int folders = 0;
    int files = 0;
    if (clips())
        applyDirTree(root, m_folderId, folders, files);
    m_importingFolder = false;
    emit importingFolderChanged();
    touched();
    emit folderImportFinished(folders, files, skipped, truncated);
}

bool MediaBinModel::applyRelink(const QString &oldPath, const QString &newPath)
{
    ClipManager *cm = clips();
    if (!cm || !cm->relinkClip(oldPath, newPath))
        return false;
    if (auto *app = AppController::instance()) {
        if (ClipGraph *graph = app->clipGraph()) {
            for (const NodeId id : graph->nodeIdsOfType(ClipGraph::NodeType::Input)) {
                SourceDescriptor desc = graph->sourceDescriptor(id);
                const bool fileKind = desc.kind == SourceDescriptor::Kind::VideoFile
                    || desc.kind == SourceDescriptor::Kind::Image
                    || desc.kind == SourceDescriptor::Kind::AudioFile;
                if (fileKind && desc.path == oldPath) {
                    desc.path = newPath;
                    graph->setSourceDescriptor(id, desc);
                }
            }
        }
    }
    MediaMetaCache::instance()->invalidate(newPath);
    return true;
}

bool MediaBinModel::relink(const QString &path, const QUrl &url)
{
    ClipManager *cm = clips();
    const QString target = url.toLocalFile();
    if (!cm || target.isEmpty() || !QFileInfo(target).isFile() || !MediaFormats::isMediaPath(target))
        return false;

    const bool wasMissing = !QFileInfo::exists(path);
    const QString oldDir = QFileInfo(path).absolutePath();
    const QString newDir = QFileInfo(target).absolutePath();
    if (!applyRelink(path, target))
        return false;

    m_siblingRelinks.clear();
    if (wasMissing) {
        const QStringList all = cm->getClips();
        for (const QString &p : all) {
            if (p == target || QFileInfo::exists(p) || QFileInfo(p).absolutePath() != oldDir)
                continue;
            const QString candidate = QDir(newDir).absoluteFilePath(QFileInfo(p).fileName());
            if (QFileInfo(candidate).isFile() && !all.contains(candidate))
                m_siblingRelinks.insert(p, candidate);
        }
    }
    touched();
    if (!m_siblingRelinks.isEmpty())
        emit siblingRelinkAvailable(m_siblingRelinks.size(), newDir);
    return true;
}

void MediaBinModel::relinkSiblings()
{
    for (auto it = m_siblingRelinks.cbegin(); it != m_siblingRelinks.cend(); ++it)
        applyRelink(it.key(), it.value());
    m_siblingRelinks.clear();
    touched();
}

bool MediaBinModel::saveCopy(const QString &path, const QUrl &url)
{
    const QString dest = url.toLocalFile();
    if (dest.isEmpty() || !QFileInfo(path).isFile())
        return false;
    QPointer<MediaBinModel> self(this);
    QThreadPool::globalInstance()->start([self, path, dest] {
        if (QFile::exists(dest))
            QFile::remove(dest);
        const bool ok = QFile::copy(path, dest);
        const QString name = QFileInfo(dest).fileName();
        if (self) {
            QMetaObject::invokeMethod(self.data(), [self, ok, name] {
                if (self)
                    emit self->saveFinished(ok, name);
            }, Qt::QueuedConnection);
        }
    });
    return true;
}

void MediaBinModel::revealInFileManager(const QString &path) const
{
    const QFileInfo fi(path);
    const QString dir = fi.exists() ? fi.absolutePath() : QFileInfo(fi.absolutePath()).exists()
        ? fi.absolutePath() : QString();
#if defined(Q_OS_WIN)
    if (fi.exists()) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(fi.absoluteFilePath())});
        return;
    }
#elif defined(Q_OS_MACOS)
    if (fi.exists()) {
        QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), fi.absoluteFilePath()});
        return;
    }
#endif
    if (!dir.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

int MediaBinModel::addToGraph(const QStringList &paths)
{
    auto *app = AppController::instance();
    if (!app || !app->graphRuntime())
        return 0;
    int added = 0;
    for (const QString &p : paths) {
        if (app->graphRuntime()->addClipNode(p))
            ++added;
    }
    return added;
}

QString MediaBinModel::uriList(const QStringList &paths) const
{
    QStringList urls;
    for (const QString &p : paths)
        urls.append(QUrl::fromLocalFile(p).toString());
    return urls.join(QStringLiteral("\r\n"));
}

QStringList MediaBinModel::nameFilters() const
{
    return {tr("Media Files (%1)").arg(MediaFormats::globPattern(true, true, true)),
            tr("All Files (*)")};
}

QString MediaBinModel::displayNameOf(const QString &path) const
{
    ClipManager *cm = clips();
    return cm ? cm->displayName(path) : QFileInfo(path).fileName();
}

QUrl MediaBinModel::suggestedCopyUrl(const QString &path) const
{
    const QFileInfo fi(path);
    const QString suffix = fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix();
    return QUrl::fromLocalFile(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
                               + tr(" copy") + suffix);
}
