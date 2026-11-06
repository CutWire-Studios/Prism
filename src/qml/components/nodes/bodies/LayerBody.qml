pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Layer compositor: one row per input slot with visibility eye, rename (double-click) and
// reorder arrows, then an "Edit Layout" button. The trailing unconnected slot is a placeholder.
Item {
    id: root

    required property var node
    readonly property var slots: node.params.slots ? node.params.slots : []
    readonly property real hdr: 20
    readonly property real row: 22
    property int editing: -1

    function connectedAt(i) {
        const p = root.node.inputPorts[i]
        return p ? !!p.connected : false
    }

    readonly property int connectedCount: {
        let n = 0
        for (let i = 0; i < node.inputPorts.length; ++i)
            if (node.inputPorts[i].connected)
                ++n
        return n
    }

    function patchSlot(i, patch) {
        const next = root.slots.slice()
        next[i] = Object.assign({}, next[i], patch)
        root.node.controller.setParams(root.node.nodeId, { slots: next })
    }

    Repeater {
        model: root.slots

        delegate: Item {
            id: slotRow

            required property var modelData
            required property int index
            readonly property bool connected: {
                void root.node.inputPorts
                return root.connectedAt(index)
            }

            y: root.hdr + index * root.row
            width: root.width
            height: root.row

            Rectangle {
                x: 4
                width: parent.width - 8
                height: 1
                color: Theme.panelBorder
            }

            NodeLabel {
                visible: !slotRow.connected
                x: 26
                width: parent.width - 32
                height: parent.height
                muted: true
                text: qsTr("+ connect input")
            }

            IconButton {
                visible: slotRow.connected
                x: 6
                y: 3
                buttonSize: 16
                focusPolicy: Qt.NoFocus
                iconSize: 14
                glyph: slotRow.modelData.visible ? "eye" : "eye-off"
                tooltip: qsTr("Show or hide this layer")
                onClicked: root.patchSlot(slotRow.index, { visible: !slotRow.modelData.visible })
            }

            NodeLabel {
                visible: slotRow.connected && root.editing !== slotRow.index
                x: 26
                width: parent.width - 70
                height: parent.height
                text: slotRow.modelData.name && slotRow.modelData.name.length > 0
                      ? slotRow.modelData.name : qsTr("in %1").arg(slotRow.index + 1)

                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: root.editing = slotRow.index
                }
            }

            ThemedTextField {
                id: nameField
                visible: root.editing === slotRow.index
                x: 24
                y: 2
                width: parent.width - 68
                height: 18
                topPadding: 0
                bottomPadding: 0
                leftPadding: Theme.spacingMd
                rightPadding: Theme.spacingMd
                font.pixelSize: Theme.fontSizeXs
                text: slotRow.modelData.name || ""
                onVisibleChanged: if (visible) {
                    forceActiveFocus()
                    selectAll()
                }
                onEditingFinished: {
                    if (root.editing !== slotRow.index)
                        return
                    root.editing = -1
                    root.patchSlot(slotRow.index, { name: text })
                }
                Keys.onEscapePressed: root.editing = -1
            }

            IconButton {
                visible: slotRow.connected && slotRow.index > 0 && root.connectedAt(slotRow.index - 1)
                x: parent.width - 40
                y: 3
                buttonSize: 16
                focusPolicy: Qt.NoFocus
                iconSize: 12
                glyph: "chevron-up"
                tooltip: qsTr("Move up")
                onClicked: root.node.controller.swapSlots(root.node.nodeId, slotRow.index, slotRow.index - 1)
            }

            IconButton {
                visible: slotRow.connected && slotRow.index < root.connectedCount - 1
                x: parent.width - 22
                y: 3
                buttonSize: 16
                focusPolicy: Qt.NoFocus
                iconSize: 12
                glyph: "chevron-down"
                tooltip: qsTr("Move down")
                onClicked: root.node.controller.swapSlots(root.node.nodeId, slotRow.index, slotRow.index + 1)
            }
        }
    }

    NodeButton {
        x: 6
        y: root.height - 23
        width: root.width - 12
        text: qsTr("Edit Layout")
        onClicked: root.node.requestEditor("layerLayout")
    }
}
