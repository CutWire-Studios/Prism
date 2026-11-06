import QtQuick
import Prism

// Audio FX: a caption and, when the effect has a dialog, its edit button.
Item {
    id: root

    required property var node

    NodeLabel {
        x: 8
        y: 24
        width: root.width - 16
        height: 14
        muted: true
        text: qsTr("AUDIO FX")
        horizontalAlignment: Text.AlignHCenter
    }

    NodeButton {
        visible: root.node.hasEditor
        x: 8
        y: root.height - 24
        width: root.width - 16
        height: 18
        text: NodeCatalog.effectLabel(root.node.type, root.node.params)
        onClicked: root.node.requestEditor("audioEffect")
    }
}
