#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

/// Owns the flat list of media file paths in the asset library, a virtual
/// folder tree over them, and enforces import limits (per-folder, per-batch,
/// and overall library size). Folders exist only in the bin, never on disk.
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

    struct BinFolder {
        QString id;
        QString name;
        QString parentId;
    };

    ClipManager();

    // Replace all clips with the contents of one folder
    void loadFolder(const QString &folderPath);

    // Append another folder's contents recursively (does NOT clear existing
    // clips), mirroring the directory tree as virtual folders under parentId.
    // Returns the id of the folder created for folderPath (empty if none).
    QString addFolder(const QString &folderPath, const QString &parentId = {});

    // Append individual files selected by the user, optionally straight into a bin folder.
    void addFiles(const QStringList &filePaths, const QString &folderId = {});

    ImportCheck checkFolderImport(const QString &folderPath) const;
    ImportCheck checkFilesImport(const QStringList &filePaths) const;

    QStringList getClips() const { return clips; }
    QString     getClipPath(int index) const;
    int         getClipCount() const { return clips.count(); }
    bool        isEmpty() const { return clips.isEmpty(); }
    void        clear();
    void        removeClip(int index);

    QString createFolder(const QString &name, const QString &parentId = {});
    // Re-creates a folder with a known id (session restore).
    void    restoreFolder(const BinFolder &folder);
    bool    renameFolder(const QString &id, const QString &name);
    // Removes the folder, its subfolders and their clips from the bin only.
    void    removeFolder(const QString &id);
    bool    moveClip(const QString &path, const QString &folderId);
    bool    moveFolder(const QString &id, const QString &newParentId);

    // Points a library entry at another file, keeping its folder, position and name.
    // False when the old path is unknown or the new one is already in the library.
    bool    relinkClip(const QString &oldPath, const QString &newPath);
    // Label shown in the bin; empty or equal to the file name clears the override.
    void    setDisplayName(const QString &path, const QString &name);
    QString displayName(const QString &path) const;
    bool    hasCustomName(const QString &path) const { return m_names.contains(path); }
    // Bumped by every change to the clip list, folders, assignments or names.
    quint64 revision() const { return m_revision; }

    QList<BinFolder> folders() const { return m_folders; }
    QList<BinFolder> foldersIn(const QString &parentId) const;
    QStringList      clipsIn(const QString &folderId) const;
    // Clips in the folder and all of its descendants.
    QStringList      clipsUnder(const QString &folderId) const;
    QString          folderOf(const QString &path) const { return m_clipFolder.value(path); }
    bool             hasFolder(const QString &id) const;
    BinFolder        folder(const QString &id) const;
    // Monotonic insertion order of a clip (for "date added" sorting).
    qint64           addedOrder(const QString &path) const { return m_added.value(path, 0); }

    static bool isMediaPath(const QString &path);
    static bool isAudioPath(const QString &path);

private:
    QStringList clips;
    QList<BinFolder> m_folders;
    QHash<QString, QString> m_clipFolder;
    QHash<QString, qint64> m_added;
    QHash<QString, QString> m_names;
    qint64 m_addCounter = 0;
    quint64 m_revision = 0;
    void appendClip(const QString &path, const QString &folderId);
    bool isDescendant(const QString &id, const QString &ancestorId) const;
    QStringList allMediaRecursive(const QString &folderPath) const;
    void importDir(const QString &dirPath, const QString &parentId, QString *outId);
    bool isMediaFile(const QString &path) const;
    QStringList newMediaInFolder(const QString &folderPath) const;
    QStringList allMediaInFolder(const QString &folderPath) const;
    int countNewMedia(const QStringList &paths) const;
    void appendFromFolder(const QString &folderPath);
};
