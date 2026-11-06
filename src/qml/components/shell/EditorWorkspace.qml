import QtQuick
import Prism

// Seats the active workspace's main window in this item and opens its secondary windows.
Item {
    id: root

    required property var pool

    property var _windows: []
    property var _closed: ({})
    property bool _ready: false
    property bool _forceReapply: false
    property string _appliedSignature: ""

    // Sizes and geometry are written back quietly while the user drags, so they must not
    // count as a change here or every drag would end in a rebuild.
    function _signature(id) {
        return JSON.stringify(Workspaces.workspace(id).windows ?? [],
                              (key, value) => key === "sizes" || key === "geometry" ? undefined : value)
    }

    function _reapply() {
        const force = root._forceReapply
        root._forceReapply = false
        if (force || root._signature(Workspaces.activeId) !== root._appliedSignature)
            root.restoreWindows()
    }

    Component { id: windowC; WorkspaceWindow {} }

    function _dropWindows() {
        for (const win of root._windows) {
            win.host.clear()
            win.destroy()
        }
        root._windows = []
    }

    // Every panel is parked before any host builds: a panel belongs to one window at a time,
    // so a host building first could take a panel a later host was still holding.
    function applyWorkspace() {
        const id = Workspaces.activeId
        root._appliedSignature = root._signature(id)
        root.pool.parkAll()
        mainHost.clear()
        root._dropWindows()

        mainHost.workspaceId = id
        mainHost.build()

        const windows = Workspaces.workspace(id).windows ?? []
        const opened = []
        for (const entry of windows.slice(1)) {
            if (root._closed[entry.id])
                continue
            const win = windowC.createObject(root, {
                pool: root.pool,
                workspaceId: id,
                windowId: entry.id,
                entry: entry
            })
            win.closedByUser.connect(root._windowClosed)
            win.build()
            opened.push(win)
        }
        root._windows = opened
    }

    function _windowClosed(windowId) {
        const closed = Object.assign({}, root._closed)
        closed[windowId] = true
        root._closed = closed
        Qt.callLater(root.applyWorkspace)
    }

    function restoreWindows() {
        root._closed = ({})
        root.applyWorkspace()
    }

    WorkspaceHost {
        id: mainHost
        anchors.fill: parent
        pool: root.pool
        windowId: "main"
        sideMargin: Theme.panelGap
    }

    Component.onCompleted: {
        root._ready = true
        root.applyWorkspace()
    }

    Connections {
        target: Workspaces
        function onActiveIdChanged() { if (root._ready) root.restoreWindows() }
        function onActiveWorkspaceEdited() {
            root._forceReapply = true
            Qt.callLater(root._reapply)
        }
        function onWorkspacesChanged() { Qt.callLater(root._reapply) }
    }
}
