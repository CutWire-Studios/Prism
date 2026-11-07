pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Scene source card: the referenced scene's thumbnail and name. The scene is picked in the
// inspector. Geometry matches ClipGraph's Input node (20.5 header, 105 card).
Item {
    id: root

    required property var node
    readonly property var source: node.params.source ? node.params.source : ({})
    readonly property string targetId: source.sceneId || ""
    property var info: ({})

    function refresh() {
        info = targetId ? AppController.sceneInfo(targetId) : ({})
    }

    onTargetIdChanged: refresh()
    Component.onCompleted: refresh()

    Connections {
        target: AppController.scenes
        function onDataChanged() { root.refresh() }
        function onModelReset() { root.refresh() }
    }

    Rectangle {
        x: 6
        y: 26.5
        width: root.width - 12
        height: Math.round(width * 9 / 16)
        radius: Theme.radiusSm
        color: Theme.panelBackground
        border.width: 1
        border.color: Theme.panelBorder
        clip: true

        IconGlyph {
            anchors.centerIn: parent
            glyph: NodePalette.sourceIcon(root.source)
            iconSize: Theme.iconSizeXl
            iconColor: Theme.mutedForeground
            visible: !root.info.thumbnail || image.status !== Image.Ready
        }

        Image {
            id: image
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            source: root.info.thumbnail || ""
        }
    }

    NodeLabel {
        x: 6
        y: 26.5 + Math.round((root.width - 12) * 9 / 16) + 4
        width: root.width - 12
        height: 14
        muted: !root.info.name
        text: root.info.name ? root.info.name : (root.targetId ? qsTr("Missing scene") : qsTr("No scene"))
        horizontalAlignment: Text.AlignHCenter
    }
}
