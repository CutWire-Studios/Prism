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
        y: Math.min(root.height, 66) - 24
        width: root.width - 16
        height: 18
        opacity: root.node.params.bypass ? 0.5 : 1
        text: NodeCatalog.effectLabel(root.node.type, root.node.params)
        horizontalAlignment: Text.AlignHCenter
    }

    // Names of the extra input ports of a multi-input effect, beside their ports in the footer.
    Repeater {
        model: root.node.inputPorts

        delegate: NodeLabel {
            required property var modelData

            visible: modelData.index > 0
            x: 10
            y: modelData.y - 7
            width: parent.width - 20
            height: 14
            muted: true
            text: modelData.label
        }
    }
}
