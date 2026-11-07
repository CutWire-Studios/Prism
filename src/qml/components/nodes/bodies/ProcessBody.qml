import QtQuick
import Prism

// Process effect: a live thumbnail of its output and, when the effect has parameters, their
// current state (e.g. "Vertical"). Geometry matches ClipGraph's Process node (20 header, 104 card).
// Parameters are edited in the inspector.
Item {
    id: root

    required property var node

    property int thumbRevision: AppController.nodeThumbnailRevision(node.nodeId)

    Connections {
        target: AppController
        function onNodeThumbnailChanged(id) {
            if (id === 0 || Number(id) === Number(root.node.nodeId))
                root.thumbRevision = AppController.nodeThumbnailRevision(root.node.nodeId)
        }
    }

    // Live post-effect output, refreshed round-robin by the runtime while the node is on screen.
    Rectangle {
        x: 6
        y: 24
        width: root.width - 12
        height: Math.round(width * 9 / 16)
        radius: Theme.radiusSm
        color: Theme.panelBackground
        border.width: 1
        border.color: Theme.panelBorder
        clip: true
        opacity: root.node.params.bypass ? 0.5 : 1

        Image {
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            retainWhileLoading: true
            source: root.thumbRevision > 0
                    ? "image://nodethumb/" + root.node.nodeId + "?v=" + root.thumbRevision : ""
        }

        NodeLabel {
            anchors.fill: parent
            visible: !!root.node.params.bypass
            muted: true
            text: qsTr("BYPASSED")
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    NodeLabel {
        x: 8
        y: 84
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

            visible: modelData.index > 0 && modelData.kind !== NodePalette.portKind.ParamIn
            x: 10
            y: modelData.y - 7
            width: parent.width - 20
            height: 14
            muted: true
            text: modelData.label
        }
    }
}
