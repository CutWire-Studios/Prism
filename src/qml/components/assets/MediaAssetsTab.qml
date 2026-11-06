import QtQuick
import QtQuick.Controls.Basic
import Prism
// The media browser: search, view toggles, breadcrumb and the grid/list/tree of one bin folder.
// The parent owns the dialogs (rename, relink, folder picker) and answers the request signals.
Item {
    id: root

    required property var bin

    // Delete/Backspace belongs to the surface that owns keyboard focus; the selection may stay
    // highlighted while the user works elsewhere, so this is not a global shortcut.
    focus: false

    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)
                && root.selectedKeys.length > 0) {
            root.removeSelected()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape && root.selectedKeys.length > 0) {
            root.clearSelection()
            event.accepted = true
        } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            root.selectedKeys = root.bin.assetKeys()
            event.accepted = true
        }
    }

    readonly property bool gridMode: bin.viewMode === "grid"
    readonly property bool treeMode: bin.viewMode === "tree"
    readonly property bool libraryEmpty: bin.assetCount === 0 && bin.folderCount === 0
    readonly property string query: search.text.trim()

    signal renameRequested(string path)
    signal relinkRequested(string path)
    signal saveCopyRequested(string path)
    signal moveToFolderRequested(var paths)
    signal folderRenameRequested(string folderId, string folderName)
    signal folderMoveRequested(string folderId)
    signal importRequested()
    signal importFolderRequested()

    // Selected library paths, in the order they were picked. Folders are never selectable.
    property var selectedKeys: []
    // Anchor for Shift-click ranges: the last plain or Ctrl click. A Shift-click does not move it.
    property string selectionAnchor: ""

    function isAssetSelected(key) {
        return key.length > 0 && root.selectedKeys.indexOf(key) >= 0
    }
    function selectOnly(key) {
        root.selectedKeys = [key]
    }
    function toggleSelection(key) {
        const at = root.selectedKeys.indexOf(key)
        if (at < 0) {
            root.selectedKeys = root.selectedKeys.concat([key])
        } else {
            const next = root.selectedKeys.slice()
            next.splice(at, 1)
            root.selectedKeys = next
        }
    }
    // Menus and drags act on the whole selection when the item is part of it, else replace it.
    function ensureAssetSelected(key) {
        if (!root.isAssetSelected(key)) {
            root.selectOnly(key)
            root.selectionAnchor = key
        }
    }
    function clickAsset(key, modifiers) {
        if ((modifiers & Qt.ShiftModifier) !== 0) {
            const range = root.bin.rangeKeys(root.selectionAnchor || key, key)
            const next = root.selectedKeys.slice()
            for (const k of range) {
                if (next.indexOf(k) < 0)
                    next.push(k)
            }
            root.selectedKeys = next
        } else if ((modifiers & Qt.ControlModifier) !== 0) {
            root.toggleSelection(key)
            root.selectionAnchor = key
        } else {
            root.selectOnly(key)
            root.selectionAnchor = key
        }
    }
    function clearSelection() {
        root.selectedKeys = []
        root.selectionAnchor = ""
    }

    // Drops ids that left view: removed, moved to another folder, filtered out by search, or the
    // folder was navigated away from.
    function pruneSelection() {
        if (root.selectedKeys.length === 0 && root.selectionAnchor.length === 0)
            return
        const present = {}
        for (const k of root.bin.assetKeys())
            present[k] = true
        const pruned = root.selectedKeys.filter(k => present[k] === true)
        if (pruned.length !== root.selectedKeys.length)
            root.selectedKeys = pruned
        if (root.selectionAnchor.length > 0 && present[root.selectionAnchor] !== true)
            root.selectionAnchor = ""
    }
    Connections {
        target: root.bin
        function onChanged() { root.pruneSelection() }
    }

    function dragMimeData(key) {
        const paths = root.isAssetSelected(key) ? root.selectedKeys : [key]
        return {
            "text/uri-list": root.bin.uriList(paths),
            "application/x-prism-asset": paths.join("\n")
        }
    }

    function dropOnFolder(folderId, payload) {
        const paths = payload.split("\n").filter(p => p.length > 0)
        if (paths.length > 0)
            root.bin.moveItems(paths, folderId)
    }

    function addToGraph(paths) {
        if (paths.length === 0)
            return
        if (root.bin.addToGraph(paths) === 0)
            Toasts.warning(qsTr("Couldn’t add the media to the graph."))
    }

    function removeSelected() {
        const keys = root.selectedKeys
        root.clearSelection()
        root.bin.removeItems(keys)
    }

    MediaAssetsEmptyState {
        shown: root.libraryEmpty && !root.bin.importingFolder
        onImportRequested: root.importRequested()
        onImportFolderRequested: root.importFolderRequested()
    }

    ThemedTextField {
        id: search
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding + viewControls.width + Theme.spacingMd
        visible: !root.libraryEmpty
        placeholderText: qsTr("Search media")
        font.family: Theme.fontFamily
        text: root.bin.search
        onTextChanged: root.bin.search = text
    }

    MediaAssetsViewToolbar {
        id: viewControls
        bin: root.bin
        anchors.right: parent.right
        anchors.rightMargin: Theme.pagePadding
        anchors.verticalCenter: search.verticalCenter
        visible: search.visible
    }

    Item {
        id: binNavRow
        anchors.top: search.visible ? search.bottom : parent.top
        anchors.topMargin: visible ? Theme.spacingSm : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding

        readonly property bool showBreadcrumb: root.bin.folderCount > 0 && root.bin.folderId !== ""
        readonly property bool showTreeActions: root.treeMode && root.bin.folderCount > 0

        visible: showBreadcrumb || showTreeActions
        height: visible ? Math.max(showBreadcrumb ? breadcrumb.implicitHeight : 0,
                                   showTreeActions ? treeActions.implicitHeight : 0)
                        : 0

        BinBreadcrumb {
            id: breadcrumb
            visible: binNavRow.showBreadcrumb
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            bin: root.bin
            currentFolderId: root.bin.folderId
            onNavigate: (folderId) => root.bin.folderId = folderId
            onItemsDropped: (folderId, payload) => root.dropOnFolder(folderId, payload)
        }

        Row {
            id: treeActions
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm
            visible: binNavRow.showTreeActions

            IconButton {
                glyph: Theme.icons.listChevronsUpDown
                variant: "ghost"
                tooltip: qsTr("Expand all")
                onClicked: root.bin.expandAll()
            }

            IconButton {
                glyph: Theme.icons.listChevronsDownUp
                variant: "ghost"
                tooltip: qsTr("Collapse all")
                onClicked: root.bin.collapseAll()
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: root.query.length > 0 && root.bin.visibleCount === 0
        compact: true
        glyph: Theme.icons.search
        title: qsTr("No media match “%1”").arg(root.query)
        hint: qsTr("Try a different name.")
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: root.query.length === 0 && root.bin.visibleCount === 0 && !root.libraryEmpty
        compact: true
        glyph: Theme.icons.folder
        title: qsTr("This folder is empty")
        hint: qsTr("Drag media here, or import more.")
    }

    MediaAssetsJobStatus {
        id: importStatusRow
        anchors.top: binNavRow.bottom
        anchors.topMargin: visible ? Theme.spacingSm : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        visible: root.bin.importingFolder && !root.libraryEmpty
        indeterminate: true
        label: qsTr("Importing folder…")
    }

    GridView {
        id: grid
        readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
        readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
        visible: root.gridMode && root.bin.visibleCount > 0

        anchors.top: importStatusRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        anchors.margins: Theme.pagePadding
        anchors.topMargin: Theme.spacingMd
        anchors.rightMargin: Theme.pagePadding - Theme.assetCardGap

        cellWidth: Math.floor(width / columnCount)
        cellHeight: (cardSize * 9 / 16) + Theme.spacing3xl + Theme.assetCardGap

        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        // A mouse drag on a card drags the asset out; the wheel and touchpad scroll.
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }

        model: root.gridMode ? root.bin : null
        delegate: MediaAssetsGridCard {
            host: root
            cardSize: grid.cardSize
        }
    }

    ListView {
        id: listColumn
        visible: !root.gridMode && root.bin.visibleCount > 0

        anchors.top: importStatusRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        anchors.margins: Theme.pagePadding
        anchors.topMargin: Theme.spacingMd

        spacing: Theme.spacingMd

        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }

        model: root.gridMode ? null : root.bin
        delegate: MediaAssetsListRow {
            host: root
        }
    }
}
