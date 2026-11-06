import QtQuick
import Prism

// Output sink: port captions and the button that opens the program output window.
Item {
    id: root

    required property var node

    NodeLabel {
        x: 12
        y: 17.16 - 6
        height: 12
        muted: true
        font.pixelSize: Theme.fontSizeTiny
        text: qsTr("video")
    }

    NodeLabel {
        x: 12
        y: 34.84 - 6
        height: 12
        muted: true
        font.pixelSize: Theme.fontSizeTiny
        text: qsTr("Switcher")
    }

    NodeButton {
        x: 4
        y: root.height - 24
        width: root.width - 8
        text: qsTr("Open")
        onClicked: root.node.canvas.outputWindowRequested()
    }
}
