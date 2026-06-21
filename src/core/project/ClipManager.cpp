#include "core/project/ClipManager.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>

ClipManager::ClipManager() = default;

QStringList ClipManager::allMediaInFolder(const QString &folderPath) const {
    QDir dir(folderPath);
    if (!dir.exists())
        return {};

    QStringList filters = {"*.mp4","*.avi","*.mov","*.mkv","*.webm",
                           "*.png","*.jpg","*.jpeg","*.bmp","*.webp","*.gif"};
    dir.setNameFilters(filters);
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

ClipManager::ImportCheck ClipManager::checkFolderImport(const QString &folderPath) const {
    ImportCheck result;
    result.totalItems = allMediaInFolder(folderPath).size();
    result.newItems   = newMediaInFolder(folderPath).size();

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
        clips.append(found.at(i));
    std::sort(clips.begin(), clips.end());
}

void ClipManager::loadFolder(const QString &folderPath) {
    clips.clear();
    appendFromFolder(folderPath);
    qDebug() << "Loaded" << clips.count() << "clips from" << folderPath;
}

void ClipManager::addFolder(const QString &folderPath) {
    const int before = clips.count();
    appendFromFolder(folderPath);
    qDebug() << "Added" << (clips.count() - before) << "clips from" << folderPath
             << "(total:" << clips.count() << ")";
}

void ClipManager::addFiles(const QStringList &filePaths) {
    int added = 0;
    for (const QString &path : filePaths) {
        if (clips.size() >= MaxLibrarySize)
            break;
        if (isMediaFile(path) && !clips.contains(path)) {
            clips.append(path);
            ++added;
        }
    }
    qDebug() << "Added" << added << "files (total:" << clips.count() << ")";
}

QString ClipManager::getClipPath(int index) const {
    return (index >= 0 && index < clips.count()) ? clips.at(index) : QString();
}

bool ClipManager::isMediaPath(const QString &path) {
    const QString l = path.toLower();
    return l.endsWith(".mp4") || l.endsWith(".avi")  || l.endsWith(".mov")  ||
           l.endsWith(".mkv") || l.endsWith(".webm") || l.endsWith(".png")  ||
           l.endsWith(".jpg") || l.endsWith(".jpeg") || l.endsWith(".bmp")  ||
           l.endsWith(".webp") || l.endsWith(".gif");
}

bool ClipManager::isMediaFile(const QString &path) const {
    return isMediaPath(path);
}
