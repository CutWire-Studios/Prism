import QtQuick
import Prism

// Script: its run-on mode and a Run button.
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
            switch (root.p.runOn) {
            case 0: return qsTr("On input")
            case 1: return qsTr("Every %1ms").arg(root.p.intervalMs)
            case 2: return qsTr("At %1").arg(String(root.p.runTime).substring(0, 5))
            case 3: return qsTr("Manual")
            }
            return ""
        }
    }

    NodeButton {
        x: 4
        y: root.height - 24
        width: root.width - 8
        text: qsTr("Run")
        onClicked: AppController.runScript(root.node.nodeId)
    }
}
