#pragma once

#include <QString>
#include <QStringList>

class ClipManager {
public:
    ClipManager();

    void loadFolder(const QString &folderPath);
    QStringList getClips() const { return clips; }
    QString getClipPath(int index) const;

    int getClipCount() const { return clips.count(); }
    bool isEmpty() const { return clips.isEmpty(); }

    void clear() { clips.clear(); }

private:
    QStringList clips;
    QString currentFolder;

    bool isMediaFile(const QString &filePath) const;
};
