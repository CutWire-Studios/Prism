pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Master audio output: one row (and input port) per output device.
Item {
    id: root

    required property var node
    readonly property var devices: node.params.devicePorts ? node.params.devicePorts : []
    readonly property real hdr: 22
    readonly property real row: 20

    Repeater {
        model: root.devices

        delegate: Item {
            id: deviceRow

            required property var modelData
            required property int index

            y: root.hdr + index * root.row
            width: root.width
            height: root.row

            Rectangle {
                visible: deviceRow.index > 0
                x: 4
                width: parent.width - 8
                height: 1
                color: Theme.panelBorder
            }

            NodeLabel {
                x: 22
                width: parent.width - 26
                height: parent.height
                muted: true
                text: deviceRow.modelData.deviceLabel && deviceRow.modelData.deviceLabel.length > 0
                      ? deviceRow.modelData.deviceLabel : qsTr("Default")
            }
        }
    }
}
