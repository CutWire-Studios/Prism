import QtQuick
import Prism

// Builds one window's split tree from a workspace and seats the pooled panels in it.
Item {
    id: root

    required property var pool
    property string workspaceId: ""
    property string windowId: "main"
    property real sideMargin: 0

    property var _content: null

    readonly property var _minWidths: ({ "assets": 200, "sourceLab.bin": 200, "properties": 240, "preview": 260, "sourceLab.viewer": 260, "sourceLab.process": 240 })
    readonly property var _minHeights: ({ "timeline": 140, "sourceLab.trim": 140 })

    Item {
        id: body
        anchors.fill: parent
        anchors.leftMargin: root.sideMargin
        anchors.rightMargin: root.sideMargin
        anchors.bottomMargin: root.sideMargin
        anchors.topMargin: root.sideMargin
    }

    Component { id: splitC; WorkspaceSplit {} }
    Component { id: cellC; WorkspaceCell {} }
    Component { id: slotC; WorkspaceSlot {} }

    function holdsPreview(node) {
        if (node.split !== undefined)
            return node.children.some(child => root.holdsPreview(child))
        return node.panels.some(entry => entry.type === "preview")
    }

    function minPx(node, horizontal) {
        if (node.split !== undefined)
            return 0
        const table = horizontal ? root._minWidths : root._minHeights
        let px = horizontal ? 160 : 100
        for (const entry of node.panels)
            px = Math.max(px, table[entry.type] ?? 0)
        return px
    }

    // The preview's branch absorbs the slack, as the preview pane always did; without one,
    // the biggest branch does.
    function fillIndex(node) {
        const previewAt = node.children.findIndex(child => root.holdsPreview(child))
        if (previewAt >= 0)
            return previewAt
        let best = 0
        for (let i = 1; i < node.children.length; ++i) {
            if ((node.sizes?.[i] ?? 0) > (node.sizes?.[best] ?? 0))
                best = i
        }
        return best
    }

    function makeContent(node, path, parent) {
        if (node.split === undefined)
            return slotC.createObject(parent, { node: node, pool: root.pool })

        const horizontal = node.split === "h"
        const count = node.children.length
        const fill = root.fillIndex(node)
        const split = splitC.createObject(parent, {
            orientation: horizontal ? Qt.Horizontal : Qt.Vertical,
            workspaceId: root.workspaceId,
            windowId: root.windowId,
            path: path
        })
        for (let i = 0; i < count; ++i) {
            const child = node.children[i]
            const cell = cellC.createObject(split, {
                view: split,
                fraction: node.sizes?.[i] ?? 1 / count,
                fill: i === fill,
                leaf: child.split === undefined,
                holdsPreview: root.holdsPreview(child),
                minPx: root.minPx(child, horizontal)
            })
            split.addItem(cell)
            root.makeContent(child, path.concat(i), cell)
        }
        return split
    }

    // Panels are never direct children of a SplitView, so they are detached by re-parenting
    // them to the pool before the old containers go. Destroying a container with a pooled
    // panel still inside would destroy the panel — and with the preview, its video sink and
    // GL renderer — so the order here is the whole point. (Container.removeItem() destroys
    // what it removes; takeItem() hands it back.) The caller parks every panel through the
    // pool first, and only then calls build(); with several hosts that has to happen for all
    // of them before any host builds, or one would steal what another is about to release.
    function clear() {
        if (root._content) {
            root._content.visible = false
            root._content.destroy()
            root._content = null
        }
    }

    function build() {
        root.clear()
        const ws = Workspaces.workspace(root.workspaceId)
        const window = (ws.windows ?? []).find(w => w.id === root.windowId)
        if (window)
            root._content = root.makeContent(window.root, [], body)
    }
}
