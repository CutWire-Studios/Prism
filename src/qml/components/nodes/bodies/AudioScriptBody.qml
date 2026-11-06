import QtQuick
import Prism

// Audio script: analyser configuration summary and the FFT edit button.
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
        text: qsTr("FFT %1 · %2 bins\nlow/mid/high · beat").arg(root.p.fftSize).arg(root.p.binCount)
    }

    NodeButton {
        x: 4
        y: 78
        width: root.width - 8
        height: 24
        text: qsTr("Edit FFT")
        onClicked: root.node.requestEditor("audioScript")
    }
}
