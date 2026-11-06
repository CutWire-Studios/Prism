pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Audio mixer: per input slot a name (double-click to rename) with its level.
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
                text: qsTr("+ connect stream")
            }

            NodeLabel {
                visible: slotRow.connected && root.editing !== slotRow.index
                x: 26
                width: parent.width - 32
                height: parent.height
                font.family: Theme.monoFontFamily
                text: (slotRow.modelData.name && slotRow.modelData.name.length > 0
                       ? slotRow.modelData.name : qsTr("in %1").arg(slotRow.index + 1))
                      + "  " + (slotRow.modelData.muted ? qsTr("Muted") : slotRow.modelData.volume + "%")

                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: root.editing = slotRow.index
                }
            }

            ThemedTextField {
                visible: root.editing === slotRow.index
                x: 24
                y: 2
                width: parent.width - 30
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
        }
    }
}
