#include "ui/common/AssetLibrary.h"
#include "ui/common/Icons.h"
#include "ui/mainwindow/MainWindowUtils.h"
#include "core/project/ClipManager.h"
#include "core/media/ThumbnailExtractor.h"
#include "core/media/MediaFormats.h"

#include <QActionGroup>
#include <QApplication>
#include <QCollator>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {

constexpr int kRoleFolder = Qt::UserRole + 1;
const QString kBinMime = QStringLiteral("application/x-prism-bin-items");

bool hasBinItems(const QMimeData *mime) {
    return mime->hasFormat(kBinMime);
}

QStringList binItems(const QMimeData *mime) {
    return QString::fromUtf8(mime->data(kBinMime)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

class DropButton : public QToolButton {
public:
    using QToolButton::QToolButton;

    std::function<void(const QStringList &)> onDrop;

protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (hasBinItems(e->mimeData()))
            e->setDropAction(Qt::CopyAction), e->accept();
    }
    void dropEvent(QDropEvent *e) override {
        if (!hasBinItems(e->mimeData()))
            return;
        e->setDropAction(Qt::CopyAction);
        e->accept();
        if (onDrop)
            onDrop(binItems(e->mimeData()));
    }
};

class AssetListWidget : public QListWidget {
public:
    explicit AssetListWidget(ClipManager *clipManager)
        : m_clipManager(clipManager) {
        setAcceptDrops(true);
        setDropIndicatorShown(false);
    }

    std::function<void(const QString &, const QStringList &)> onDropOnFolder;

    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override {
        QList<QUrl> urls;
        QStringList entries;
        for (QListWidgetItem *item : items) {
            const QString key = item->data(Qt::UserRole).toString();
            if (key.isEmpty())
                continue;
            if (item->data(kRoleFolder).toBool()) {
                entries << QLatin1String("F:") + key;
                for (const QString &path : m_clipManager->clipsUnder(key))
                    urls << QUrl::fromLocalFile(path);
            } else {
                entries << QLatin1String("C:") + key;
                urls << QUrl::fromLocalFile(key);
            }
        }

        auto *mime = new QMimeData();
        mime->setUrls(urls);
        mime->setData(kBinMime, entries.join(QLatin1Char('\n')).toUtf8());
        return mime;
    }

protected:
    QListWidgetItem *folderTarget(const QPoint &pos) const {
        QListWidgetItem *item = itemAt(pos);
        if (item && item->data(kRoleFolder).toBool() && !item->isSelected())
            return item;
        return nullptr;
    }

    void dragEnterEvent(QDragEnterEvent *e) override {
        if (hasBinItems(e->mimeData()) || e->mimeData()->hasUrls())
            e->setDropAction(Qt::CopyAction), e->accept();
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (hasBinItems(e->mimeData())) {
            if (folderTarget(e->position().toPoint()))
                e->setDropAction(Qt::CopyAction), e->accept();
            else
                e->ignore();
        } else if (e->mimeData()->hasUrls()) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
        }
    }
    void dropEvent(QDropEvent *e) override {
        if (!hasBinItems(e->mimeData())) {
            QCoreApplication::sendEvent(window(), e);
            return;
        }
        QListWidgetItem *target = folderTarget(e->position().toPoint());
        if (!target)
            return;
        e->setDropAction(Qt::CopyAction);
        e->accept();
        if (onDropOnFolder)
            onDropOnFolder(target->data(Qt::UserRole).toString(), binItems(e->mimeData()));
    }

private:
    ClipManager *m_clipManager;
};

QString typeLabel(const QString &path) {
    return QFileInfo(path).suffix().toUpper();
}

} // namespace

AssetLibrary::AssetLibrary(ClipManager *clipManager, QWidget *parent)
    : QWidget(parent)
    , m_clipManager(clipManager)
{
    auto *list = new AssetListWidget(clipManager);
    list->onDropOnFolder = [this](const QString &folderId, const QStringList &entries) {
        moveEntries(entries, folderId);
    };
    m_list = list;
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->installEventFilter(this);
    m_list->viewport()->installEventFilter(this);

    auto *dropUp = new DropButton(this);
    dropUp->onDrop = [this](const QStringList &entries) {
        moveEntries(entries, m_clipManager->folder(m_currentFolder).parentId);
    };
    m_upBtn = dropUp;
    m_upBtn->setAcceptDrops(true);
    m_upBtn->setToolTip(tr("Up one folder (Backspace)"));
    Icons::setIconText(m_upBtn, Icons::Names::ArrowUp, 16);

    m_breadcrumb = new QWidget(this);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumb);
    m_breadcrumbLayout->setContentsMargins(0, 0, 0, 0);
    m_breadcrumbLayout->setSpacing(0);

    auto *newFolderBtn = new QToolButton(this);
    newFolderBtn->setToolTip(tr("New folder"));
    Icons::setIconText(newFolderBtn, Icons::Names::FolderPlus, 16);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);

    m_viewBtn = new QToolButton(this);

    auto *sortBtn = new QToolButton(this);
    sortBtn->setToolTip(tr("Sort"));
    sortBtn->setPopupMode(QToolButton::InstantPopup);
    Icons::setIconText(sortBtn, Icons::Names::Sort, 16);
    auto *sortMenu = new QMenu(sortBtn);
    m_sortKeyGroup = new QActionGroup(sortMenu);
    const std::pair<const char *, SortKey> keys[] = {
        {QT_TR_NOOP("Name"), SortKey::Name},
        {QT_TR_NOOP("Type"), SortKey::Type},
        {QT_TR_NOOP("Date modified"), SortKey::Modified},
        {QT_TR_NOOP("Date added"), SortKey::Added},
    };
    for (const auto &k : keys) {
        QAction *a = sortMenu->addAction(tr(k.first));
        a->setCheckable(true);
        a->setData(int(k.second));
        m_sortKeyGroup->addAction(a);
    }
    sortMenu->addSeparator();
    m_sortDirGroup = new QActionGroup(sortMenu);
    QAction *asc = sortMenu->addAction(tr("Ascending"));
    QAction *desc = sortMenu->addAction(tr("Descending"));
    for (QAction *a : {asc, desc}) {
        a->setCheckable(true);
        m_sortDirGroup->addAction(a);
    }
    asc->setData(true);
    desc->setData(false);
    sortBtn->setMenu(sortMenu);

    loadSettings();
    for (QAction *a : m_sortKeyGroup->actions())
        a->setChecked(a->data().toInt() == int(m_sortKey));
    asc->setChecked(m_sortAscending);
    desc->setChecked(!m_sortAscending);
    applyViewMode();

    auto *navRow = new QHBoxLayout();
    navRow->setSpacing(4);
    navRow->addWidget(m_upBtn);
    navRow->addWidget(m_breadcrumb, 1);
    navRow->addWidget(newFolderBtn);

    auto *toolRow = new QHBoxLayout();
    toolRow->setSpacing(4);
    toolRow->addWidget(m_search, 1);
    toolRow->addWidget(m_viewBtn);
    toolRow->addWidget(sortBtn);

    m_emptyLabel = new QLabel(
        tr("Use \"Media > Add Files\" or double-click to add files"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_emptyLabel->setProperty("role", "secondary");
    m_emptyLabel->setStyleSheet(QStringLiteral("font-size: 12px; padding: 12px;"));
    m_emptyLabel->installEventFilter(this);

    auto *listHost = new QWidget(this);
    auto *listGrid = new QGridLayout(listHost);
    listGrid->setContentsMargins(0, 0, 0, 0);
    listGrid->addWidget(m_list, 0, 0);
    listGrid->addWidget(m_emptyLabel, 0, 0);

    m_hintBanner = new QWidget(this);
    m_hintBanner->setObjectName(QStringLiteral("card"));
    m_hintBanner->setProperty("role", "panel");
    auto *hintLayout = new QHBoxLayout(m_hintBanner);
    hintLayout->setContentsMargins(8, 6, 4, 6);
    hintLayout->setSpacing(6);

    auto *hintLabel = new QLabel(
        tr("Drag and drop any clip to the canvas to get started"), m_hintBanner);
    hintLabel->setWordWrap(true);
    hintLabel->setProperty("role", "secondary");
    hintLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));

    auto *dismissBtn = new QPushButton(QStringLiteral("\u00d7"), m_hintBanner);
    dismissBtn->setFlat(true);
    dismissBtn->setFixedSize(20, 20);
    dismissBtn->setStyleSheet(
        QStringLiteral("font-size: 14px; font-weight: bold; border: none;"));
    dismissBtn->setToolTip(tr("Dismiss"));

    hintLayout->addWidget(hintLabel, 1);
    hintLayout->addWidget(dismissBtn, 0, Qt::AlignTop);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);
    root->addLayout(navRow);
    root->addLayout(toolRow);
    root->addWidget(listHost, 1);
    root->addWidget(m_hintBanner, 0);

    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &AssetLibrary::onContextMenu);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &AssetLibrary::openItem);
    connect(dismissBtn, &QPushButton::clicked, this, &AssetLibrary::dismissHint);
    connect(m_upBtn, &QToolButton::clicked, this, &AssetLibrary::goUp);
    connect(newFolderBtn, &QToolButton::clicked, this, &AssetLibrary::createFolder);
    connect(m_search, &QLineEdit::textChanged, this, [this] { populate(); });
    connect(m_viewBtn, &QToolButton::clicked, this, [this] {
        m_gridView = !m_gridView;
        applyViewMode();
        saveSettings();
        populate();
    });
    connect(m_sortKeyGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        m_sortKey = SortKey(a->data().toInt());
        saveSettings();
        populate();
    });
    connect(m_sortDirGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        m_sortAscending = a->data().toBool();
        saveSettings();
        populate();
    });

    populate();
}

void AssetLibrary::loadSettings() {
    QSettings s;
    m_gridView = s.value(QStringLiteral("assetLibrary/gridView"), true).toBool();
    const int key = s.value(QStringLiteral("assetLibrary/sortKey"), int(SortKey::Name)).toInt();
    m_sortKey = key >= int(SortKey::Name) && key <= int(SortKey::Added) ? SortKey(key) : SortKey::Name;
    m_sortAscending = s.value(QStringLiteral("assetLibrary/sortAscending"), true).toBool();
}

void AssetLibrary::saveSettings() const {
    QSettings s;
    s.setValue(QStringLiteral("assetLibrary/gridView"), m_gridView);
    s.setValue(QStringLiteral("assetLibrary/sortKey"), int(m_sortKey));
    s.setValue(QStringLiteral("assetLibrary/sortAscending"), m_sortAscending);
}

void AssetLibrary::applyViewMode() {
    if (m_gridView) {
        m_list->setViewMode(QListView::IconMode);
        m_list->setResizeMode(QListView::Adjust);
        m_list->setIconSize(QSize(kThumbW, kThumbH));
        m_list->setWordWrap(true);
        m_list->setSpacing(4);
        Icons::setIconText(m_viewBtn, Icons::Names::ListView, 16);
        m_viewBtn->setToolTip(tr("Switch to list view"));
    } else {
        m_list->setViewMode(QListView::ListMode);
        m_list->setIconSize(QSize(kThumbW / 2, kThumbH / 2));
        m_list->setWordWrap(false);
        m_list->setSpacing(2);
        Icons::setIconText(m_viewBtn, Icons::Names::GridView, 16);
        m_viewBtn->setToolTip(tr("Switch to grid view"));
    }
    m_list->setMovement(QListView::Static);
}

QPixmap AssetLibrary::thumbFor(const QString &path) {
    auto it = m_thumbCache.constFind(path);
    if (it != m_thumbCache.constEnd())
        return it.value();
    const QPixmap pix = ThumbnailExtractor::extract(path, kThumbW, kThumbH);
    m_thumbCache.insert(path, pix);
    return pix;
}

QPixmap AssetLibrary::padded(const QPixmap &raw) const {
    QPixmap canvas(kThumbW, kThumbH);
    canvas.fill(Qt::transparent);
    if (raw.isNull())
        return canvas;
    QPainter p(&canvas);
    p.drawPixmap((kThumbW - raw.width()) / 2, (kThumbH - raw.height()) / 2, raw);
    return canvas;
}

QString AssetLibrary::folderPathName(const QString &folderId) const {
    QStringList parts;
    QString cur = folderId;
    QSet<QString> seen;
    while (!cur.isEmpty() && !seen.contains(cur)) {
        seen.insert(cur);
        const ClipManager::BinFolder f = m_clipManager->folder(cur);
        parts.prepend(f.name);
        cur = f.parentId;
    }
    return parts.join(QLatin1Char('/'));
}

void AssetLibrary::populate() {
    if (!m_currentFolder.isEmpty() && !m_clipManager->hasFolder(m_currentFolder))
        m_currentFolder.clear();

    m_list->clear();

    const QString needle = m_search->text().trimmed();
    auto matches = [&](const QString &name) {
        return needle.isEmpty() || name.contains(needle, Qt::CaseInsensitive);
    };

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    const int dir = m_sortAscending ? 1 : -1;

    QList<ClipManager::BinFolder> folders;
    for (const ClipManager::BinFolder &f : m_clipManager->foldersIn(m_currentFolder)) {
        if (matches(f.name))
            folders.append(f);
    }
    std::stable_sort(folders.begin(), folders.end(),
                     [&](const ClipManager::BinFolder &a, const ClipManager::BinFolder &b) {
        return dir * collator.compare(a.name, b.name) < 0;
    });

    struct Entry {
        QString path;
        QString name;
        QString type;
        QFileInfo info;
        qint64 added;
    };
    QList<Entry> clips;
    for (const QString &path : m_clipManager->clipsIn(m_currentFolder)) {
        const QFileInfo info(path);
        if (!matches(info.fileName()))
            continue;
        clips.append({path, info.fileName(), typeLabel(path), info, m_clipManager->addedOrder(path)});
    }
    std::stable_sort(clips.begin(), clips.end(), [&](const Entry &a, const Entry &b) {
        int c = 0;
        switch (m_sortKey) {
        case SortKey::Name:
            break;
        case SortKey::Type:
            c = collator.compare(a.type, b.type);
            break;
        case SortKey::Modified: {
            const QDateTime ma = a.info.lastModified(), mb = b.info.lastModified();
            c = ma < mb ? -1 : (mb < ma ? 1 : 0);
            break;
        }
        case SortKey::Added:
            c = a.added < b.added ? -1 : (a.added > b.added ? 1 : 0);
            break;
        }
        if (c == 0)
            c = collator.compare(a.name, b.name);
        return dir * c < 0;
    });

    const QPixmap folderThumb = padded(Icons::pixmap(Icons::Names::Folder, 40));
    const QLocale locale;

    for (const ClipManager::BinFolder &f : folders) {
        auto *item = new QListWidgetItem(m_list);
        const int count = m_clipManager->clipsUnder(f.id).size();
        item->setText(m_gridView ? f.name
                                 : tr("%1\nFolder \u00b7 %n item(s)", nullptr, count).arg(f.name));
        item->setData(Qt::UserRole, f.id);
        item->setData(kRoleFolder, true);
        item->setToolTip(f.name);
        item->setIcon(QIcon(folderThumb));
        if (m_gridView)
            item->setSizeHint(QSize(kThumbW + 8, kThumbH + 24));
    }
    for (const Entry &e : clips) {
        auto *item = new QListWidgetItem(m_list);
        item->setText(m_gridView
            ? e.name
            : QStringLiteral("%1\n%2 \u00b7 %3").arg(e.name, e.type,
                                                   locale.formattedDataSize(e.info.size())));
        item->setData(Qt::UserRole, e.path);
        item->setData(kRoleFolder, false);
        item->setToolTip(e.path);
        item->setIcon(QIcon(padded(thumbFor(e.path))));
        if (m_gridView)
            item->setSizeHint(QSize(kThumbW + 8, kThumbH + 24));
    }

    updateBreadcrumb();
    updateEmptyState();
}

void AssetLibrary::updateBreadcrumb() {
    while (QLayoutItem *li = m_breadcrumbLayout->takeAt(0)) {
        delete li->widget();
        delete li;
    }

    QList<ClipManager::BinFolder> chain;
    QString cur = m_currentFolder;
    QSet<QString> seen;
    while (!cur.isEmpty() && !seen.contains(cur)) {
        seen.insert(cur);
        const ClipManager::BinFolder f = m_clipManager->folder(cur);
        chain.prepend(f);
        cur = f.parentId;
    }

    auto addSegment = [this](const QString &text, const QString &folderId) {
        auto *btn = new DropButton(m_breadcrumb);
        btn->setText(text);
        btn->setAutoRaise(true);
        btn->setAcceptDrops(true);
        btn->onDrop = [this, folderId](const QStringList &entries) {
            moveEntries(entries, folderId);
        };
        connect(btn, &QToolButton::clicked, this, [this, folderId] { setCurrentFolder(folderId); });
        m_breadcrumbLayout->addWidget(btn);
    };

    addSegment(tr("Library"), QString());
    for (const ClipManager::BinFolder &f : chain) {
        auto *sep = new QLabel(QStringLiteral("\u203a"), m_breadcrumb);
        sep->setProperty("role", "secondary");
        m_breadcrumbLayout->addWidget(sep);
        addSegment(f.name, f.id);
    }
    m_breadcrumbLayout->addStretch(1);
    m_upBtn->setEnabled(!m_currentFolder.isEmpty());
}

void AssetLibrary::setCurrentFolder(const QString &folderId) {
    m_currentFolder = folderId;
    populate();
}

void AssetLibrary::goUp() {
    if (m_currentFolder.isEmpty())
        return;
    setCurrentFolder(m_clipManager->folder(m_currentFolder).parentId);
}

void AssetLibrary::openItem(QListWidgetItem *item) {
    if (item && item->data(kRoleFolder).toBool())
        setCurrentFolder(item->data(Qt::UserRole).toString());
}

void AssetLibrary::createFolder() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, tr("New Folder"), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    m_clipManager->createFolder(name, m_currentFolder);
    populate();
}

void AssetLibrary::renameItem(QListWidgetItem *item) {
    if (!item || !item->data(kRoleFolder).toBool())
        return;
    const QString id = item->data(Qt::UserRole).toString();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, m_clipManager->folder(id).name,
                                               &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    m_clipManager->renameFolder(id, name);
    populate();
}

void AssetLibrary::moveEntries(const QStringList &entries, const QString &targetFolderId) {
    for (const QString &entry : entries) {
        const QString key = entry.mid(2);
        if (entry.startsWith(QLatin1String("F:")))
            m_clipManager->moveFolder(key, targetFolderId);
        else if (entry.startsWith(QLatin1String("C:")))
            m_clipManager->moveClip(key, targetFolderId);
    }
    populate();
}

void AssetLibrary::addItemsAsClips(const QList<QListWidgetItem *> &items) {
    QStringList paths;
    for (QListWidgetItem *item : items) {
        const QString key = item->data(Qt::UserRole).toString();
        if (item->data(kRoleFolder).toBool())
            paths += m_clipManager->clipsUnder(key);
        else
            paths << key;
    }
    for (const QString &path : paths)
        emit addAsClipRequested(path, thumbFor(path));
}

bool AssetLibrary::addFiles(const QStringList &filePaths) {
    const ClipManager::ImportCheck check = m_clipManager->checkFilesImport(filePaths);
    if (!warnUnlessImportOk(check))
        return false;

    const QStringList before = m_clipManager->getClips();
    m_clipManager->addFiles(filePaths);
    for (const QString &path : MainWindowUtils::diffNewItems(before, m_clipManager->getClips()))
        m_clipManager->moveClip(path, m_currentFolder);
    populate();
    return true;
}

bool AssetLibrary::addFolder(const QString &folderPath) {
    const ClipManager::ImportCheck check = m_clipManager->checkFolderImport(folderPath);
    if (!warnUnlessImportOk(check))
        return false;

    m_clipManager->addFolder(folderPath, m_currentFolder);
    populate();
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
    m_currentFolder.clear();
    m_thumbCache.clear();
    m_clipManager->clear();
    populate();
}

void AssetLibrary::rebuild() {
    m_currentFolder.clear();
    populate();
}

void AssetLibrary::removeItems(const QList<QListWidgetItem *> &items) {
    if (items.isEmpty())
        return;

    for (QListWidgetItem *item : items) {
        if (!item->data(kRoleFolder).toBool())
            continue;
        const QString id = item->data(Qt::UserRole).toString();
        if (m_clipManager->clipsUnder(id).isEmpty() && m_clipManager->foldersIn(id).isEmpty())
            continue;
        if (QMessageBox::question(this, tr("Remove Folder"),
                tr("Remove the folder \"%1\" and everything in it from the bin?\n\n"
                   "Files on disk are not deleted.").arg(item->text()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        break;
    }

    QSet<QString> paths;
    for (QListWidgetItem *item : items) {
        const QString key = item->data(Qt::UserRole).toString();
        if (item->data(kRoleFolder).toBool())
            m_clipManager->removeFolder(key);
        else
            paths.insert(key);
    }

    const QStringList clips = m_clipManager->getClips();
    for (int i = clips.size() - 1; i >= 0; --i) {
        if (paths.contains(clips.at(i)))
            m_clipManager->removeClip(i);
    }

    for (const QString &path : paths)
        m_thumbCache.remove(path);

    populate();
}

void AssetLibrary::removeSelectedItems() {
    removeItems(m_list->selectedItems());
}

void AssetLibrary::promptAddFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Files"), QString(),
        tr("Media Files (%1)").arg(MediaFormats::globPattern(true, true, false)));
    if (!files.isEmpty())
        addFiles(files);
}

void AssetLibrary::updateEmptyState() {
    const bool empty = m_list->count() == 0;
    const bool libraryEmpty = m_clipManager->isEmpty() && m_clipManager->folders().isEmpty();
    m_emptyLabel->setText(libraryEmpty
        ? tr("Use \"Media > Add Files\" or double-click to add files")
        : (m_search->text().trimmed().isEmpty() ? tr("This folder is empty") : tr("No matches")));
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
    if (item && !item->isSelected()) {
        m_list->clearSelection();
        item->setSelected(true);
    }
    const QList<QListWidgetItem *> targets = m_list->selectedItems();

    QMenu menu(this);
    QAction *newFolderAction = menu.addAction(tr("New Folder"));
    QAction *renameAction = nullptr;
    QAction *addClipAction = nullptr;
    QAction *removeAction = nullptr;
    QMenu *moveMenu = nullptr;

    if (!targets.isEmpty()) {
        if (targets.size() == 1 && targets.first()->data(kRoleFolder).toBool()) {
            renameAction = menu.addAction(tr("Rename"));
            renameAction->setShortcut(Qt::Key_F2);
        }

        moveMenu = menu.addMenu(tr("Move to"));
        QAction *rootAction = moveMenu->addAction(tr("Library (root)"));
        rootAction->setData(QString());
        rootAction->setEnabled(!m_currentFolder.isEmpty());
        QList<QPair<QString, QString>> dests;
        for (const ClipManager::BinFolder &f : m_clipManager->folders())
            dests.append({folderPathName(f.id), f.id});
        std::sort(dests.begin(), dests.end());
        for (const auto &d : dests) {
            QAction *a = moveMenu->addAction(d.first);
            a->setData(d.second);
            a->setEnabled(d.second != m_currentFolder);
        }

        menu.addSeparator();
        addClipAction = menu.addAction(
            targets.size() > 1 ? tr("Add as clips") : tr("Add as clip"));
        removeAction = menu.addAction(tr("Remove from library"));
    }

    QAction *chosen = menu.exec(m_list->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == newFolderAction) {
        createFolder();
    } else if (chosen == renameAction) {
        renameItem(targets.first());
    } else if (chosen == addClipAction) {
        addItemsAsClips(targets);
    } else if (chosen == removeAction) {
        removeItems(targets);
    } else if (moveMenu && chosen->parent() == moveMenu) {
        QStringList entries;
        for (QListWidgetItem *t : targets) {
            entries << (t->data(kRoleFolder).toBool() ? QLatin1String("F:") : QLatin1String("C:"))
                           + t->data(Qt::UserRole).toString();
        }
        moveEntries(entries, chosen->data().toString());
    }
}

bool AssetLibrary::eventFilter(QObject *watched, QEvent *event) {
    const bool isList = watched == m_list;
    const bool isViewport = watched == m_list->viewport();

    if (isList) {
        if (event->type() == QEvent::KeyPress) {
            const auto *keyEvent = static_cast<QKeyEvent *>(event);
            switch (keyEvent->key()) {
            case Qt::Key_Delete:
                removeSelectedItems();
                return true;
            case Qt::Key_Backspace:
                goUp();
                return true;
            case Qt::Key_F2: {
                const auto sel = m_list->selectedItems();
                if (sel.size() == 1)
                    renameItem(sel.first());
                return true;
            }
            case Qt::Key_Return:
            case Qt::Key_Enter: {
                const auto sel = m_list->selectedItems();
                if (sel.size() == 1)
                    openItem(sel.first());
                return true;
            }
            default:
                break;
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
