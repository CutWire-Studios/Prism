#pragma once

#include <QString>
#include <QStringList>

/// Owns the flat list of media file paths in the asset library and enforces
/// import limits (per-folder, per-batch, and overall library size).
class ClipManager {
public:
    static constexpr int MaxLibrarySize = 300;
    static constexpr int MaxBatchImport = 100;

    enum class ImportLimit {
        Ok,
        FolderTooLarge,
        BatchTooLarge,
        LibraryFull,
        LibraryPartial,
    };

    struct ImportCheck {
        ImportLimit status = ImportLimit::Ok;
        int totalItems  = 0;
        int newItems    = 0;
        int importCount = 0;
    };

    ClipManager();

    // Replace all clips with the contents of one folder
    void loadFolder(const QString &folderPath);

    // Append another folder's contents (does NOT clear existing clips)
    void addFolder(const QString &folderPath);

    // Append individual files selected by the user
    void addFiles(const QStringList &filePaths);

    ImportCheck checkFolderImport(const QString &folderPath) const;
    ImportCheck checkFilesImport(const QStringList &filePaths) const;

    QStringList getClips() const { return clips; }
    QString     getClipPath(int index) const;
    int         getClipCount() const { return clips.count(); }
    bool        isEmpty() const { return clips.isEmpty(); }
    void        clear() { clips.clear(); }
    void        removeClip(int index) {
        if (index >= 0 && index < clips.size()) {
            clips.removeAt(index);
        }
    }

    static bool isMediaPath(const QString &path);
    static bool isAudioPath(const QString &path);

private:
    QStringList clips;
    bool isMediaFile(const QString &path) const;
    QStringList newMediaInFolder(const QString &folderPath) const;
    QStringList allMediaInFolder(const QString &folderPath) const;
    int countNewMedia(const QStringList &paths) const;
    void appendFromFolder(const QString &folderPath);
};
