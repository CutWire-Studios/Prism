import QtQuick
import Prism

// Audio capture: selected device, level.
Item {
    id: root

    required property var node
    readonly property var p: node.params

    NodeLabel {
        x: 6
        y: 30
        width: root.width - 12
        height: 18
        text: root.p.deviceId && root.p.deviceId.length > 0 ? root.p.deviceLabel : qsTr("Default Output")
        horizontalAlignment: Text.AlignHCenter
    }

    NodeLabel {
        x: 6
        y: 50
        width: root.width - 12
        height: 16
        muted: true
        text: root.p.muted ? qsTr("Muted") : qsTr("Volume %1%").arg(root.p.volume)
        horizontalAlignment: Text.AlignHCenter
    }
}
