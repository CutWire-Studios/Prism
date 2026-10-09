#pragma once

#include <QWidget>
#include <QHash>
#include <QPixmap>
#include "core/project/ClipManager.h"
class QListWidget;
class QListWidgetItem;
class QLabel;
class QLineEdit;
class QToolButton;
class QHBoxLayout;
class QMenu;
class QActionGroup;

/// Media browser panel backed by a ClipManager. Imports files/folders into a virtual
/// folder tree and lets
/// the user drag or click an asset to add it to a deck (addAsClipRequested).
class AssetLibrary : public QWidget {
    Q_OBJECT

public:
    explicit AssetLibrary(ClipManager *clipManager, QWidget *parent = nullptr);

    bool addFiles(const QStringList &filePaths);
    bool addFolder(const QString &folderPath);
    void clear();
    void rebuild();

signals:
    void addAsClipRequested(const QString &path, const QPixmap &thumb);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onContextMenu(const QPoint &pos);
    void dismissHint();

private:
    enum class SortKey { Name, Type, Modified, Added };

    void populate();
    void updateBreadcrumb();
    void setCurrentFolder(const QString &folderId);
    void goUp();
    void openItem(QListWidgetItem *item);
    void createFolder();
    void renameItem(QListWidgetItem *item);
    void moveEntries(const QStringList &entries, const QString &targetFolderId);
    void addItemsAsClips(const QList<QListWidgetItem *> &items);
    void applyViewMode();
    void loadSettings();
    void saveSettings() const;
    QPixmap thumbFor(const QString &path);
    QPixmap padded(const QPixmap &raw) const;
    QString folderPathName(const QString &folderId) const;
    void removeItems(const QList<QListWidgetItem *> &items);
    void removeSelectedItems();
    void promptAddFiles();
    void updateEmptyState();
    bool warnUnlessImportOk(const ClipManager::ImportCheck &check);

    ClipManager  *m_clipManager   = nullptr;
    QListWidget  *m_list          = nullptr;
    QLabel       *m_emptyLabel    = nullptr;
    QWidget      *m_hintBanner    = nullptr;
    bool          m_hintDismissed = false;

    QString       m_currentFolder;
    bool          m_gridView      = true;
    SortKey       m_sortKey       = SortKey::Name;
    bool          m_sortAscending = true;
    QHash<QString, QPixmap> m_thumbCache;

    QToolButton  *m_upBtn         = nullptr;
    QToolButton  *m_viewBtn       = nullptr;
    QWidget      *m_breadcrumb    = nullptr;
    QHBoxLayout  *m_breadcrumbLayout = nullptr;
    QLineEdit    *m_search        = nullptr;
    QActionGroup *m_sortKeyGroup  = nullptr;
    QActionGroup *m_sortDirGroup  = nullptr;

    static constexpr int kThumbW = 110;
    static constexpr int kThumbH = 65;
};
