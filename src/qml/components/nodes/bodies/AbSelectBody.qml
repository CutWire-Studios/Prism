pragma ComponentBehavior: Bound
import QtQuick
import Prism

// A/B deck select: per input slot a name (double-click to rename) and A / B buttons that send
// the slot's producer to that deck. The buttons only work while the node's output feeds the Output node.
Item {
    id: root

    required property var node
    readonly property var slots: node.params.slots ? node.params.slots : []
    readonly property real hdr: 20
    readonly property real row: 24
    property int editing: -1

    readonly property var ab: {
        void node.controller.revision
        return node.controller.abState(node.nodeId)
    }

    function connectedAt(i) {
        const p = root.node.inputPorts[i]
        return p ? !!p.connected : false
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
                x: 14
                width: parent.width - 18
                height: parent.height
                muted: true
                text: qsTr("+ connect input")
            }

            NodeLabel {
                visible: slotRow.connected && root.editing !== slotRow.index
                x: 14
                width: parent.width - 90
                height: parent.height
                text: slotRow.modelData.name && slotRow.modelData.name.length > 0
                      ? slotRow.modelData.name : qsTr("in %1").arg(slotRow.index + 1)

                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: root.editing = slotRow.index
                }
            }

            ThemedTextField {
                visible: root.editing === slotRow.index
                x: 12
                y: 3
                width: parent.width - 88
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

            NodeButton {
                visible: slotRow.connected
                enabled: root.ab.outputConnected
                x: parent.width - 52
                y: 3
                width: 22
                height: 18
                leftPadding: 0
                rightPadding: 0
                text: "A"
                variant: slotRow.index === root.ab.a ? "primary" : "secondary"
                tooltip: qsTr("Send to deck A")
                onClicked: root.node.controller.triggerAbSlot(root.node.nodeId, slotRow.index, true)
            }

            NodeButton {
                visible: slotRow.connected
                enabled: root.ab.outputConnected
                x: parent.width - 28
                y: 3
                width: 22
                height: 18
                leftPadding: 0
                rightPadding: 0
                text: "B"
                variant: slotRow.index === root.ab.b ? "primary" : "secondary"
                tooltip: qsTr("Send to deck B")
                onClicked: root.node.controller.triggerAbSlot(root.node.nodeId, slotRow.index, false)
            }
        }
    }
}
