import QtQuick
import QtQuick.Controls.Basic
import Prism

SplitView {
    id: root

    property string workspaceId: ""
    property string windowId: "main"
    property var path: []

    anchors.fill: parent
    spacing: 0

    handle: PanelSplitHandle { view: root }

    // Written once a drag ends, as fractions of the split's extent. Persisting is silent
    // (no rebuild): the layout on screen already matches.
    onResizingChanged: {
        if (root.resizing)
            return
        const horizontal = root.orientation === Qt.Horizontal
        const extent = horizontal ? root.width : root.height
        if (extent <= 0)
            return
        const sizes = []
        for (let i = 0; i < root.count; ++i) {
            const cell = root.itemAt(i)
            cell.fraction = (horizontal ? cell.width : cell.height) / extent
            cell.rebind()
            sizes.push(cell.fraction)
        }
        Workspaces.setSizes(root.workspaceId, root.windowId, root.path, sizes)
    }
}
