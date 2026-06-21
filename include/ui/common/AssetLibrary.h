#pragma once

#include <QWidget>
#include <QPixmap>
#include "core/project/ClipManager.h"
class QListWidget;
class QListWidgetItem;
class QLabel;

/// Media browser panel backed by a ClipManager. Imports files/folders and lets
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
    void appendItem(const QString &path);
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

    static constexpr int kThumbW = 110;
    static constexpr int kThumbH = 65;
};
