import QtQuick
import Prism

// Audio FX: a caption and its current effect label. The pedalboard opens from the inspector.
Item {
    id: root

    required property var node

    NodeLabel {
        x: 8
        y: 24
        width: root.width - 16
        height: 14
        muted: true
        text: root.node.params.bypass ? qsTr("BYPASSED") : qsTr("AUDIO FX")
        horizontalAlignment: Text.AlignHCenter
    }

    NodeLabel {
        x: 8
        y: root.height - 24
        width: root.width - 16
        height: 18
        opacity: root.node.params.bypass ? 0.5 : 1
        text: NodeCatalog.effectLabel(root.node.type, root.node.params)
        horizontalAlignment: Text.AlignHCenter
    }
}
