import QtQuick
import Prism

// Trigger: its timing mode and the edit button. A manual trigger is pulsed from the context menu.
Item {
    id: root

    required property var node
    readonly property var p: node.params

    NodeLabel {
        x: 4
        y: 32
        width: root.width - 8
        height: 40
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        horizontalAlignment: Text.AlignHCenter
        font.family: Theme.monoFontFamily
        text: {
            switch (root.p.trigMode) {
            case 0: return qsTr("Every\n%1 ms").arg(root.p.trigInterval)
            case 1: return qsTr("At\n%1").arg(String(root.p.trigTime).substring(0, 5))
            case 2: return qsTr("Manual\npulse")
            }
            return ""
        }
    }

    NodeButton {
        x: 4
        y: 78
        width: root.width - 8
        height: 24
        text: qsTr("Edit")
        onClicked: root.node.requestEditor("trigger")
    }
}
