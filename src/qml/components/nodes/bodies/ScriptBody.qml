import QtQuick
import Prism

// Script: its trigger mode.
Item {
    id: root

    required property var node
    readonly property var p: node.params

    NodeLabel {
        x: 4
        y: 34
        width: root.width - 8
        height: 36
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        horizontalAlignment: Text.AlignHCenter
        font.family: Theme.monoFontFamily
        text: {
            switch (root.p.triggerMode) {
            case 0: return qsTr("Every %1ms").arg(root.p.intervalMs)
            case 1: return qsTr("On start")
            case 2: return qsTr("Manual")
            case 3: return qsTr("On input")
            }
            return ""
        }
    }
}
