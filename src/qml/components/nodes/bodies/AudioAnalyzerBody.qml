import QtQuick
import Prism

// Audio analyzer: analyser configuration summary.
Item {
    id: root

    required property var node
    readonly property var p: node.params

    NodeLabel {
        x: 4
        y: 32
        width: root.width - 8
        height: 40
        muted: true
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        horizontalAlignment: Text.AlignHCenter
        font.family: Theme.monoFontFamily
        text: qsTr("level · low · mid · high\nbeat · kick · tempo")
    }
}
