#pragma once

#include <QString>
#include <QStringList>

class ClipManager {
public:
    ClipManager();

    // Replace all clips with the contents of one folder
    void loadFolder(const QString &folderPath);

    // Append another folder's contents (does NOT clear existing clips)
    void addFolder(const QString &folderPath);

    // Append individual files selected by the user
    void addFiles(const QStringList &filePaths);

    QStringList getClips() const { return clips; }
    QString     getClipPath(int index) const;
    int         getClipCount() const { return clips.count(); }
    bool        isEmpty() const { return clips.isEmpty(); }
    void        clear() { clips.clear(); }
    void        removeClip(int index) { if (index >= 0 && index < clips.size()) clips.removeAt(index); }

private:
    QStringList clips;
    bool isMediaFile(const QString &path) const;
    void appendFromFolder(const QString &folderPath);
};
