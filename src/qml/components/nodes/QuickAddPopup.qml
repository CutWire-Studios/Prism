pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

// Type-to-add search over the whole node catalogue (NodeCatalog.quickAddEntries), opened by tapping
// Space on the canvas or by dropping a wire on empty space. In the wire case pendingKind is the
// dragged port's PortKind and only entries with a compatible port on the opposite side are listed;
// the pick is created at worldPos and wired to the dragged port in one undo step.
Popup {
    id: root

    property var canvas: null
    // -1 for a plain add; otherwise the PortKind of the dragged port.
    property int pendingKind: -1
    property bool pendingIsOutput: false
    // World coordinates the node is created at.
    property point worldPos: Qt.point(0, 0)
    property bool wasPicked: false

    property var rows: []
    property int highlighted: -1

    function openAt(sx, sy) {
        wasPicked = false
        search.text = ""
        rebuild()
        x = Math.max(8, Math.min(sx, (parent ? parent.width : sx) - width - 8))
        y = Math.max(8, Math.min(sy, (parent ? parent.height : sy) - height - 8))
        open()
    }

    function available(entry) {
        if (entry.unique && canvas && canvas.hasNodeOfType(entry.nodeType))
            return false
        if (pendingKind >= 0 && entry.source)
            return false
        if (entry.source === "sceneInput")
            return AppController.postSceneId.length > 0 && AppController.editedSceneId === AppController.postSceneId
        if (entry.source === "ndi")
            return SourcePrompts.ndiAvailable
        if (entry.source === "webrtc")
            return SourcePrompts.webrtcAvailable
        return true
    }

    function rebuild() {
        const q = search.text.trim()
        const entries = NodeCatalog.quickAddEntries(q, pendingKind, pendingIsOutput).filter(available)
        const out = []
        let lastCategory = ""
        for (const e of entries) {
            if (q.length === 0 && e.category !== lastCategory) {
                out.push({ header: true, name: e.category })
                lastCategory = e.category
            }
            out.push(e)
        }
        rows = out
        highlighted = -1
        step(1)
    }

    function step(delta) {
        let i = highlighted
        for (;;) {
            i += delta
            if (i < 0 || i >= rows.length)
                return
            if (!rows[i].header)
                break
        }
        highlighted = i
        list.positionViewAtIndex(i, ListView.Contain)
    }

    function pick(entry) {
        wasPicked = true
        close()
        if (entry.source) {
            if (canvas)
                canvas.cancelConnection()
            if (entry.source === "file")
                AppController.requestAddFiles(worldPos.x, worldPos.y - 28)
            else
                AppController.requestAddSource(entry.source, worldPos.x, worldPos.y - 28)
        } else if (canvas) {
            canvas.addNodeAt(entry.nodeType, entry.params, worldPos.x, worldPos.y, 0)
        }
    }

    function pickHighlighted() {
        if (highlighted >= 0)
            pick(rows[highlighted])
    }

    width: 340
    height: Math.min(420, spacer.height + list.contentHeight + padding * 2 + Theme.spacingSm)
    padding: Theme.spacingSm
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onOpened: search.forceActiveFocus()

    background: Rectangle {
        radius: Theme.c.themedContextMenu.radius
        color: Theme.c.themedContextMenu.background
        border.width: Theme.c.themedContextMenu.borderWidth
        border.color: Theme.c.themedContextMenu.border
    }

    contentItem: Item {
        ThemedTextField {
            id: search
            width: parent.width
            placeholderText: root.pendingKind >= 0 ? qsTr("Add and connect…") : qsTr("Search nodes…")
            onTextChanged: root.rebuild()
            Keys.onDownPressed: root.step(1)
            Keys.onUpPressed: root.step(-1)
            Keys.onTabPressed: (event) => event.accepted = true
            Keys.onReturnPressed: root.pickHighlighted()
            Keys.onEnterPressed: root.pickHighlighted()
        }

        Item {
            id: spacer
            width: parent.width
            height: search.height + Theme.spacingSm
        }

        NodeLabel {
            x: Theme.spacingLg
            y: spacer.height + Theme.spacingSm
            visible: root.rows.length === 0
            muted: true
            text: qsTr("No matching nodes")
        }

        ListView {
            id: list
            y: spacer.height
            width: parent.width
            height: parent.height - spacer.height
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: row

                required property var modelData
                required property int index

                width: ListView.view.width
                height: row.modelData.header ? Theme.controlHeightSm : Theme.controlHeightSm * 1.6

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.c.themedMenuItem.radius
                    color: !row.modelData.header && (area.containsMouse || root.highlighted === row.index)
                           ? Theme.c.themedMenuItem.backgroundHighlighted : "transparent"
                }

                Text {
                    id: title
                    x: Theme.spacingLg
                    y: row.modelData.header ? (parent.height - height) / 2 : Theme.spacingXs
                    width: parent.width - 2 * Theme.spacingLg - categoryLabel.width - Theme.spacingSm
                    text: row.modelData.name
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: row.modelData.header ? Font.DemiBold : Font.Normal
                    color: row.modelData.header ? Theme.c.themedMenuItem.sectionForeground
                                                : Theme.c.themedMenuItem.foreground
                }

                Text {
                    id: categoryLabel
                    anchors.top: title.top
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingLg
                    visible: !row.modelData.header
                    width: visible ? implicitWidth : 0
                    text: row.modelData.category || ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.c.themedMenuItem.sectionForeground
                }

                Text {
                    visible: !row.modelData.header
                    x: Theme.spacingLg
                    anchors.top: title.bottom
                    width: parent.width - 2 * Theme.spacingLg
                    text: row.modelData.description || ""
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.c.themedMenuItem.sectionForeground
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    enabled: !row.modelData.header
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: root.highlighted = row.index
                    onClicked: root.pick(row.modelData)
                }
            }
        }
    }
}
