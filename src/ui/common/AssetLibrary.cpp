#include "ui/common/AssetLibrary.h"
#include "ui/mainwindow/MainWindowUtils.h"
#include "core/project/ClipManager.h"
#include "core/media/ThumbnailExtractor.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>

namespace {

class AssetListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;

    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override {
        QList<QUrl> urls;
        urls.reserve(items.size());
        for (QListWidgetItem *item : items) {
            const QString path = item->data(Qt::UserRole).toString();
            if (!path.isEmpty())
                urls << QUrl::fromLocalFile(path);
        }

        auto *mime = new QMimeData();
        mime->setUrls(urls);
        return mime;
    }
};

} // namespace

AssetLibrary::AssetLibrary(ClipManager *clipManager, QWidget *parent)
    : QWidget(parent)
    , m_clipManager(clipManager)
{
    m_list = new AssetListWidget();
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setIconSize(QSize(kThumbW, kThumbH));
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);
    m_list->setWordWrap(true);
    m_list->setSpacing(4);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->installEventFilter(this);
    m_list->viewport()->installEventFilter(this);

    m_emptyLabel = new QLabel(
        tr("Use \"Media > Add Files\" or double-click to add files"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 12px; padding: 12px;"));
    m_emptyLabel->installEventFilter(this);

    auto *listHost = new QWidget(this);
    auto *listGrid = new QGridLayout(listHost);
    listGrid->setContentsMargins(0, 0, 0, 0);
    listGrid->addWidget(m_list, 0, 0);
    listGrid->addWidget(m_emptyLabel, 0, 0);

    m_hintBanner = new QWidget(this);
    m_hintBanner->setStyleSheet(
        QStringLiteral("background-color: #2a2c30; border-radius: 6px;"));
    auto *hintLayout = new QHBoxLayout(m_hintBanner);
    hintLayout->setContentsMargins(8, 6, 4, 6);
    hintLayout->setSpacing(6);

    auto *hintLabel = new QLabel(
        tr("Drag and drop any clip to the canvas to get started"), m_hintBanner);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet(QStringLiteral("color: #aaa; font-size: 11px;"));

    auto *dismissBtn = new QPushButton(QStringLiteral("\u00d7"), m_hintBanner);
    dismissBtn->setFlat(true);
    dismissBtn->setFixedSize(20, 20);
    dismissBtn->setStyleSheet(
        QStringLiteral("color: #888; font-size: 14px; font-weight: bold; border: none;"));
    dismissBtn->setToolTip(tr("Dismiss"));

    hintLayout->addWidget(hintLabel, 1);
    hintLayout->addWidget(dismissBtn, 0, Qt::AlignTop);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);
    root->addWidget(listHost, 1);
    root->addWidget(m_hintBanner, 0);

    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &AssetLibrary::onContextMenu);
    connect(dismissBtn, &QPushButton::clicked, this, &AssetLibrary::dismissHint);

    updateEmptyState();
}

void AssetLibrary::appendItem(const QString &path) {
    auto *item = new QListWidgetItem(QFileInfo(path).fileName(), m_list);
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    item->setIcon(QIcon(ThumbnailExtractor::extract(path, kThumbW, kThumbH)));
    item->setSizeHint(QSize(kThumbW + 8, kThumbH + 24));
}

bool AssetLibrary::addFiles(const QStringList &filePaths) {
    const ClipManager::ImportCheck check = m_clipManager->checkFilesImport(filePaths);
    if (!warnUnlessImportOk(check))
        return false;

    const QStringList before = m_clipManager->getClips();
    m_clipManager->addFiles(filePaths);
    for (const QString &path : MainWindowUtils::diffNewItems(before, m_clipManager->getClips()))
        appendItem(path);
    updateEmptyState();
    return true;
}

bool AssetLibrary::addFolder(const QString &folderPath) {
    const ClipManager::ImportCheck check = m_clipManager->checkFolderImport(folderPath);
    if (!warnUnlessImportOk(check))
        return false;

    const QStringList before = m_clipManager->getClips();
    m_clipManager->addFolder(folderPath);
    for (const QString &path : MainWindowUtils::diffNewItems(before, m_clipManager->getClips()))
        appendItem(path);
    updateEmptyState();
    return true;
}

bool AssetLibrary::warnUnlessImportOk(const ClipManager::ImportCheck &check) {
    switch (check.status) {
    case ClipManager::ImportLimit::Ok:
        return true;
    case ClipManager::ImportLimit::FolderTooLarge:
        QMessageBox::warning(
            this, tr("Too Many Files"),
            tr("This folder contains %1 media files.\n\n"
               "CutWire Prism can import at most %2 files from a folder at once.\n"
               "Use Media \u2192 Add Files to pick specific files instead.")
                .arg(check.totalItems)
                .arg(ClipManager::MaxBatchImport));
        return false;
    case ClipManager::ImportLimit::BatchTooLarge:
        QMessageBox::warning(
            this, tr("Too Many Files"),
            tr("You selected %1 media files.\n\n"
               "CutWire Prism can add at most %2 files at once.\n"
               "Please select a smaller batch.")
                .arg(check.newItems)
                .arg(ClipManager::MaxBatchImport));
        return false;
    case ClipManager::ImportLimit::LibraryFull:
        QMessageBox::warning(
            this, tr("Asset Library Full"),
            tr("The asset library is full (%1 items maximum).\n"
               "Remove some assets before adding more.")
                .arg(ClipManager::MaxLibrarySize));
        return false;
    case ClipManager::ImportLimit::LibraryPartial:
        QMessageBox::information(
            this, tr("Asset Library Almost Full"),
            tr("Only %1 of %2 files will be added (library maximum is %3 items).")
                .arg(check.importCount)
                .arg(check.newItems)
                .arg(ClipManager::MaxLibrarySize));
        return true;
    }
    return true;
}

void AssetLibrary::clear() {
    m_list->clear();
    m_clipManager->clear();
    updateEmptyState();
}

void AssetLibrary::rebuild() {
    m_list->clear();
    for (const QString &path : m_clipManager->getClips())
        appendItem(path);
    updateEmptyState();
}

void AssetLibrary::removeItems(const QList<QListWidgetItem *> &items) {
    if (items.isEmpty())
        return;

    QSet<QString> paths;
    paths.reserve(items.size());
    for (QListWidgetItem *item : items)
        paths.insert(item->data(Qt::UserRole).toString());

    const QStringList clips = m_clipManager->getClips();
    for (int i = clips.size() - 1; i >= 0; --i) {
        if (paths.contains(clips.at(i)))
            m_clipManager->removeClip(i);
    }

    for (QListWidgetItem *item : items)
        delete m_list->takeItem(m_list->row(item));

    updateEmptyState();
}

void AssetLibrary::removeSelectedItems() {
    removeItems(m_list->selectedItems());
}

void AssetLibrary::promptAddFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Files"), QString(),
        tr("Media Files (*.mp4 *.avi *.mov *.mkv *.webm *.png *.jpg *.jpeg *.bmp *.webp *.gif)"));
    if (!files.isEmpty())
        addFiles(files);
}

void AssetLibrary::updateEmptyState() {
    const bool empty = m_list->count() == 0;
    m_emptyLabel->setVisible(empty);
    if (empty)
        m_emptyLabel->raise();
    m_hintBanner->setVisible(!empty && !m_hintDismissed);
}

void AssetLibrary::dismissHint() {
    m_hintDismissed = true;
    m_hintBanner->hide();
}

void AssetLibrary::onContextMenu(const QPoint &pos) {
    QListWidgetItem *item = m_list->itemAt(pos);
    QList<QListWidgetItem *> targets = m_list->selectedItems();
    if (targets.isEmpty() && item)
        targets = {item};
    if (targets.isEmpty())
        return;

    QMenu menu(this);
    QAction *addClipAction = menu.addAction(
        targets.size() > 1 ? tr("Add as clips") : tr("Add as clip"));
    QAction *removeAction = menu.addAction(tr("Remove from library"));

    QAction *chosen = menu.exec(m_list->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == addClipAction) {
        for (QListWidgetItem *target : targets) {
            const QString path = target->data(Qt::UserRole).toString();
            emit addAsClipRequested(path, target->icon().pixmap(m_list->iconSize()));
        }
    } else if (chosen == removeAction) {
        removeItems(targets);
    }
}

bool AssetLibrary::eventFilter(QObject *watched, QEvent *event) {
    const bool isList = watched == m_list;
    const bool isViewport = watched == m_list->viewport();

    if (isList) {
        if (event->type() == QEvent::KeyPress) {
            const auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) {
                removeSelectedItems();
                return true;
            }
        }
    }

    if (isViewport || watched == m_emptyLabel) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            if (isViewport) {
                const auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (m_list->count() > 0 && m_list->itemAt(mouseEvent->pos()) != nullptr)
                    return QWidget::eventFilter(watched, event);
            }
            promptAddFiles();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}
