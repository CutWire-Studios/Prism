import QtQuick
import Prism

// Process effect: a caption and, when the effect has parameters, their current state
// (e.g. "Vertical"). Parameters are edited in the inspector.
Item {
    id: root

    required property var node

    NodeLabel {
        x: 8
        y: 24
        width: root.width - 16
        height: 14
        muted: true
        text: root.node.params.bypass ? qsTr("BYPASSED") : qsTr("PROCESS")
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
