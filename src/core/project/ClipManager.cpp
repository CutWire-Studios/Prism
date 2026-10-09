#include "core/project/ClipManager.h"
#include "core/media/MediaFormats.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QSet>
#include <QUuid>
#include <algorithm>

ClipManager::ClipManager() = default;

QStringList ClipManager::allMediaInFolder(const QString &folderPath) const {
    QDir dir(folderPath);
    if (!dir.exists())
        return {};

    dir.setNameFilters(MediaFormats::nameFilters(true, true, true));
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);

    QStringList result;
    for (const QString &file : dir.entryList()) {
        if (isMediaFile(file))
            result.append(dir.absoluteFilePath(file));
    }
    std::sort(result.begin(), result.end());
    return result;
}

QStringList ClipManager::newMediaInFolder(const QString &folderPath) const {
    QStringList result;
    for (const QString &path : allMediaInFolder(folderPath)) {
        if (!clips.contains(path))
            result.append(path);
    }
    return result;
}

int ClipManager::countNewMedia(const QStringList &paths) const {
    int count = 0;
    for (const QString &path : paths) {
        if (isMediaFile(path) && !clips.contains(path))
            ++count;
    }
    return count;
}

QStringList ClipManager::allMediaRecursive(const QString &folderPath) const {
    QStringList result = allMediaInFolder(folderPath);
    QDir dir(folderPath);
    const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QString &sub : subs)
        result += allMediaRecursive(dir.absoluteFilePath(sub));
    return result;
}

ClipManager::ImportCheck ClipManager::checkFolderImport(const QString &folderPath) const {
    ImportCheck result;
    const QStringList all = allMediaRecursive(folderPath);
    result.totalItems = all.size();
    for (const QString &path : all) {
        if (!clips.contains(path))
            ++result.newItems;
    }

    if (result.totalItems > MaxBatchImport) {
        result.status = ImportLimit::FolderTooLarge;
        return result;
    }

    const int roomLeft = MaxLibrarySize - clips.size();
    if (roomLeft <= 0) {
        result.status = ImportLimit::LibraryFull;
        return result;
    }

    result.importCount = std::min(result.newItems, roomLeft);
    if (result.importCount < result.newItems)
        result.status = ImportLimit::LibraryPartial;
    return result;
}

ClipManager::ImportCheck ClipManager::checkFilesImport(const QStringList &filePaths) const {
    ImportCheck result;
    result.newItems = countNewMedia(filePaths);

    if (result.newItems > MaxBatchImport) {
        result.status = ImportLimit::BatchTooLarge;
        return result;
    }

    const int roomLeft = MaxLibrarySize - clips.size();
    if (roomLeft <= 0) {
        result.status = ImportLimit::LibraryFull;
        return result;
    }

    result.importCount = std::min(result.newItems, roomLeft);
    if (result.importCount < result.newItems)
        result.status = ImportLimit::LibraryPartial;
    return result;
}

void ClipManager::appendFromFolder(const QString &folderPath) {
    const QStringList found = newMediaInFolder(folderPath);
    const int cap = std::min<int>(found.size(), MaxLibrarySize - clips.size());
    for (int i = 0; i < cap; ++i)
        appendClip(found.at(i), {});
    std::sort(clips.begin(), clips.end());
}

void ClipManager::loadFolder(const QString &folderPath) {
    clear();
    appendFromFolder(folderPath);
    qDebug() << "Loaded" << clips.count() << "clips from" << folderPath;
}

void ClipManager::appendClip(const QString &path, const QString &folderId) {
    clips.append(path);
    m_added.insert(path, ++m_addCounter);
    if (!folderId.isEmpty())
        m_clipFolder.insert(path, folderId);
}

void ClipManager::importDir(const QString &dirPath, const QString &parentId, QString *outId) {
    const QStringList all = allMediaRecursive(dirPath);
    bool anyNew = false;
    for (const QString &path : all) {
        if (!clips.contains(path)) {
            anyNew = true;
            break;
        }
    }
    if (!anyNew || clips.size() >= MaxLibrarySize)
        return;

    const QDir dir(dirPath);
    QString name = dir.dirName();
    if (name.isEmpty())
        name = dirPath;
    const QString id = createFolder(name, parentId);
    if (outId)
        *outId = id;

    for (const QString &path : newMediaInFolder(dirPath)) {
        if (clips.size() >= MaxLibrarySize)
            break;
        appendClip(path, id);
    }
    const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QString &sub : subs)
        importDir(dir.absoluteFilePath(sub), id, nullptr);
}

QString ClipManager::addFolder(const QString &folderPath, const QString &parentId) {
    const int before = clips.count();
    QString id;
    importDir(QDir(folderPath).absolutePath(), hasFolder(parentId) ? parentId : QString(), &id);
    qDebug() << "Added" << (clips.count() - before) << "clips from" << folderPath
             << "(total:" << clips.count() << ")";
    return id;
}

void ClipManager::addFiles(const QStringList &filePaths) {
    int added = 0;
    for (const QString &path : filePaths) {
        if (clips.size() >= MaxLibrarySize)
            break;
        if (isMediaFile(path) && !clips.contains(path)) {
            appendClip(path, {});
            ++added;
        }
    }
    qDebug() << "Added" << added << "files (total:" << clips.count() << ")";
}

QString ClipManager::getClipPath(int index) const {
    return (index >= 0 && index < clips.count()) ? clips.at(index) : QString();
}

bool ClipManager::isAudioPath(const QString &path) {
    return MediaFormats::isAudioPath(path);
}

bool ClipManager::isMediaPath(const QString &path) {
    return MediaFormats::isMediaPath(path);
}

bool ClipManager::isMediaFile(const QString &path) const {
    return isMediaPath(path);
}

void ClipManager::clear() {
    clips.clear();
    m_folders.clear();
    m_clipFolder.clear();
    m_added.clear();
}

void ClipManager::removeClip(int index) {
    if (index < 0 || index >= clips.size())
        return;
    const QString path = clips.takeAt(index);
    m_clipFolder.remove(path);
    m_added.remove(path);
}

QString ClipManager::createFolder(const QString &name, const QString &parentId) {
    BinFolder f;
    f.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    f.name = name;
    f.parentId = hasFolder(parentId) ? parentId : QString();
    m_folders.append(f);
    return f.id;
}

void ClipManager::restoreFolder(const BinFolder &folder) {
    if (folder.id.isEmpty() || hasFolder(folder.id))
        return;
    m_folders.append(folder);
}

bool ClipManager::renameFolder(const QString &id, const QString &name) {
    for (BinFolder &f : m_folders) {
        if (f.id == id) {
            f.name = name;
            return true;
        }
    }
    return false;
}

void ClipManager::removeFolder(const QString &id) {
    if (!hasFolder(id))
        return;
    QSet<QString> doomed{id};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const BinFolder &f : std::as_const(m_folders)) {
            if (!doomed.contains(f.id) && doomed.contains(f.parentId)) {
                doomed.insert(f.id);
                grew = true;
            }
        }
    }
    for (int i = clips.size() - 1; i >= 0; --i) {
        if (doomed.contains(m_clipFolder.value(clips.at(i))))
            removeClip(i);
    }
    m_folders.removeIf([&](const BinFolder &f) { return doomed.contains(f.id); });
}

bool ClipManager::moveClip(const QString &path, const QString &folderId) {
    if (!clips.contains(path) || (!folderId.isEmpty() && !hasFolder(folderId)))
        return false;
    if (folderId.isEmpty())
        m_clipFolder.remove(path);
    else
        m_clipFolder.insert(path, folderId);
    return true;
}

bool ClipManager::isDescendant(const QString &id, const QString &ancestorId) const {
    QString cur = id;
    QSet<QString> seen;
    while (!cur.isEmpty() && !seen.contains(cur)) {
        if (cur == ancestorId)
            return true;
        seen.insert(cur);
        cur = folder(cur).parentId;
    }
    return false;
}

bool ClipManager::moveFolder(const QString &id, const QString &newParentId) {
    if (!hasFolder(id) || (!newParentId.isEmpty() && !hasFolder(newParentId)))
        return false;
    if (isDescendant(newParentId, id))
        return false;
    for (BinFolder &f : m_folders) {
        if (f.id == id) {
            f.parentId = newParentId;
            return true;
        }
    }
    return false;
}

QList<ClipManager::BinFolder> ClipManager::foldersIn(const QString &parentId) const {
    QList<BinFolder> result;
    for (const BinFolder &f : m_folders) {
        if (f.parentId == parentId)
            result.append(f);
    }
    return result;
}

QStringList ClipManager::clipsIn(const QString &folderId) const {
    QStringList result;
    for (const QString &path : clips) {
        if (m_clipFolder.value(path) == folderId)
            result.append(path);
    }
    return result;
}

QStringList ClipManager::clipsUnder(const QString &folderId) const {
    QStringList result = clipsIn(folderId);
    for (const BinFolder &f : foldersIn(folderId))
        result += clipsUnder(f.id);
    return result;
}

bool ClipManager::hasFolder(const QString &id) const {
    if (id.isEmpty())
        return false;
    for (const BinFolder &f : m_folders) {
        if (f.id == id)
            return true;
    }
    return false;
}

ClipManager::BinFolder ClipManager::folder(const QString &id) const {
    for (const BinFolder &f : m_folders) {
        if (f.id == id)
            return f;
    }
    return {};
}
