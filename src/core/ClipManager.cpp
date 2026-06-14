#include "ClipManager.h"
#include <QDir>
#include <QDebug>

ClipManager::ClipManager() = default;

void ClipManager::loadFolder(const QString &folderPath) {
    clips.clear();
    currentFolder = folderPath;

    QDir dir(folderPath);
    if (!dir.exists()) {
        qWarning() << "Folder does not exist:" << folderPath;
        return;
    }

    QStringList filters = {"*.mp4", "*.avi", "*.mov", "*.mkv", "*.webm", "*.png", "*.jpg", "*.jpeg"};
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);

    QStringList files = dir.entryList();
    for (const QString &file : files) {
        if (isMediaFile(file)) {
            clips.append(dir.absoluteFilePath(file));
        }
    }

    std::sort(clips.begin(), clips.end());
    qDebug() << "Loaded" << clips.count() << "clips from" << folderPath;
}

QString ClipManager::getClipPath(int index) const {
    if (index >= 0 && index < clips.count()) {
        return clips.at(index);
    }
    return QString();
}

bool ClipManager::isMediaFile(const QString &filePath) const {
    QString lower = filePath.toLower();
    return lower.endsWith(".mp4") || lower.endsWith(".avi") ||
           lower.endsWith(".mov") || lower.endsWith(".mkv") ||
           lower.endsWith(".webm") || lower.endsWith(".png") ||
           lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}
