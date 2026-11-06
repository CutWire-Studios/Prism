#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

class ClipManager;

/// The media bin as the Assets panel shows it: the entries of one bin folder (sub-folders first,
/// then files), or the whole expanded tree. Folders, display names and ordering live in
/// ClipManager (and so in the session file); this model adds search, sorting, async metadata and
/// the file operations. ClipManager has no change signal, so its revision counter is polled.
class MediaBinModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString folderId READ folderId WRITE setFolderId NOTIFY folderIdChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(QString folderName READ folderName NOTIFY changed)
    Q_PROPERTY(QString parentFolderId READ parentFolderId NOTIFY changed)
    Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY changed)
    Q_PROPERTY(int assetCount READ assetCount NOTIFY changed)
    Q_PROPERTY(int folderCount READ folderCount NOTIFY changed)
    Q_PROPERTY(int visibleCount READ rowCount NOTIFY changed)
    Q_PROPERTY(bool importingFolder READ importingFolder NOTIFY importingFolderChanged)
    Q_PROPERTY(int libraryLimit READ libraryLimit CONSTANT)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        PathRole,
        NameRole,
        IsFolderRole,
        KindRole,
        DurationTextRole,
        DurationRole,
        FormatRole,
        SuffixRole,
        FileSizeRole,
        ThumbKeyRole,
        ThumbRevRole,
        MissingRole,
        ProbingRole,
        DepthRole,
        ExpandedRole,
        ExpandableRole,
        ItemCountRole,
        UrlRole,
    };

    explicit MediaBinModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString folderId() const { return m_folderId; }
    void setFolderId(const QString &id);
    QString search() const { return m_search; }
    void setSearch(const QString &s);
    QString viewMode() const { return m_viewMode; }
    void setViewMode(const QString &mode);
    QString sortMode() const { return m_sortMode; }
    void setSortMode(const QString &mode);
    QString folderName() const;
    QString parentFolderId() const;
    QVariantList breadcrumbs() const;
    int assetCount() const;
    int folderCount() const;
    bool importingFolder() const { return m_importingFolder; }
    int libraryLimit() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void rescan();

    Q_INVOKABLE QString createFolder(const QString &name);
    Q_INVOKABLE bool renameFolder(const QString &id, const QString &name);
    Q_INVOKABLE bool moveFolder(const QString &id, const QString &newParentId);
    // Moves the folder's contents up to its own parent, then removes it. Never deletes media.
    Q_INVOKABLE bool deleteFolder(const QString &id);
    Q_INVOKABLE int moveItems(const QStringList &paths, const QString &folderId);
    Q_INVOKABLE int removeItems(const QStringList &paths);
    Q_INVOKABLE bool renameItem(const QString &path, const QString &name);
    // Destinations for "Move to folder": [{id, name, path}], root first. `movingFolderId` set =
    // choosing a new parent for that folder.
    Q_INVOKABLE QVariantList moveTargets(const QStringList &paths, const QString &movingFolderId) const;
    Q_INVOKABLE QString folderPath(const QString &id) const;

    Q_INVOKABLE void toggleExpanded(const QString &folderId);
    Q_INVOKABLE void expandAll();
    Q_INVOKABLE void collapseAll();

    Q_INVOKABLE QStringList rangeKeys(const QString &anchor, const QString &target) const;
    Q_INVOKABLE QStringList assetKeys() const;

    // Imports into the current bin folder. Directories are mirrored as bin folders.
    Q_INVOKABLE void importUrls(const QList<QUrl> &urls);
    Q_INVOKABLE bool importFolder(const QUrl &url);
    // Points a (usually missing) entry at another file; other missing files from the same
    // folder are offered through siblingRelinkAvailable.
    Q_INVOKABLE bool relink(const QString &path, const QUrl &url);
    Q_INVOKABLE void relinkSiblings();
    Q_INVOKABLE bool saveCopy(const QString &path, const QUrl &url);
    Q_INVOKABLE void revealInFileManager(const QString &path) const;
    Q_INVOKABLE int addToGraph(const QStringList &paths);

    Q_INVOKABLE QString uriList(const QStringList &paths) const;
    Q_INVOKABLE QStringList nameFilters() const;
    Q_INVOKABLE QString displayNameOf(const QString &path) const;
    Q_INVOKABLE QUrl suggestedCopyUrl(const QString &path) const;

signals:
    void folderIdChanged();
    void searchChanged();
    void viewModeChanged();
    void sortModeChanged();
    void changed();
    void importingFolderChanged();
    void importFinished(int added, int skipped, bool limitReached);
    void folderImportFinished(int folders, int files, int skipped, bool truncated);
    void siblingRelinkAvailable(int count, const QString &folder);
    void saveFinished(bool ok, const QString &name);

private:
    struct Row {
        bool folder = false;
        QString key;
        int depth = 0;
        bool expanded = false;
        bool expandable = false;

        bool operator==(const Row &o) const
        {
            return folder == o.folder && key == o.key && depth == o.depth
                && expanded == o.expanded && expandable == o.expandable;
        }
    };

    struct DirNode {
        QString name;
        QStringList files;
        std::vector<DirNode> subs;
    };

    ClipManager *clips() const;
    QList<Row> collect() const;
    QStringList sortedClips(QStringList paths) const;
    void touched();
    int rowOfPath(const QString &path) const;
    void applyDirTree(const DirNode &node, const QString &parentId, int &folders, int &files);
    void finishFolderImport(const DirNode &root, int skipped, bool truncated);
    bool applyRelink(const QString &oldPath, const QString &newPath);
    bool isFolderOrDescendant(const QString &candidateId, const QString &ancestorId) const;

    QList<Row> m_rows;
    QString m_folderId;
    QString m_search;
    QString m_viewMode;
    QString m_sortMode;
    QSet<QString> m_expanded;
    quint64 m_seenRevision = ~quint64(0);
    bool m_importingFolder = false;
    QHash<QString, QString> m_siblingRelinks;
    QTimer m_poll;
    QTimer m_rescan;
};
