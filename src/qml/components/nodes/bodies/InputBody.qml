pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Source card: thumbnail and a loop toggle where the source can repeat, plus the audio strip
// when it carries audio; audio files get the strip only. Geometry matches ClipGraph's Input node
// (20.5 header, 105 card, 54 strip). Everything else is edited in the inspector.
Item {
    id: root

    required property var node
    readonly property var p: node.params
    readonly property var source: p.source ? p.source : ({})
    readonly property bool hasAudio: !!p.hasAudio
    readonly property real cardTop: 20.5
    readonly property bool audioOnly: !!p.audioOnly
    readonly property real stripTop: audioOnly ? cardTop : cardTop + 105

    property int thumbRevision: AppController.nodeThumbnailRevision(node.nodeId)

    Connections {
        target: AppController
        function onNodeThumbnailChanged(id) {
            if (id === 0 || Number(id) === Number(root.node.nodeId))
                root.thumbRevision = AppController.nodeThumbnailRevision(root.node.nodeId)
        }
    }

    Rectangle {
        id: thumb
        visible: !root.audioOnly
        x: 6
        y: root.cardTop + 6
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
            visible: root.thumbRevision === 0 || image.status !== Image.Ready
        }

        Image {
            id: image
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            source: root.thumbRevision > 0
                    ? "image://nodethumb/" + root.node.nodeId + "?v=" + root.thumbRevision : ""
        }
    }

    NodeButton {
        visible: !!root.p.repeatable && !root.audioOnly
        x: 6
        y: thumb.y + thumb.height + 6
        width: root.width - 12
        glyph: "repeat"
        text: qsTr("Loop")
        variant: root.p.repeat ? "primary" : "secondary"
        tooltip: qsTr("Loop clip when it reaches the end")
        onClicked: root.node.controller.setParams(root.node.nodeId, { repeat: !root.p.repeat })
    }

    Rectangle {
        visible: root.hasAudio
        x: 2
        y: root.stripTop
        width: root.width - 4
        height: 52
        radius: Theme.radiusMd
        color: Theme.panelBackground
        border.width: 1
        border.color: Theme.panelBorder

        NodeLabel {
            x: 6
            y: 3
            width: parent.width - 12
            height: 14
            text: qsTr("Audio")
            color: NodePalette.mixedHue
            horizontalAlignment: Text.AlignHCenter
        }
        NodeLabel {
            x: 6
            y: 17
            width: parent.width - 12
            height: 14
            muted: true
            text: (root.p.audioPlaybackMode === 1 ? qsTr("On Active") : qsTr("Always"))
                  + "  " + qsTr("Delay:%1ms").arg(root.p.audioDelayMs || 0)
            horizontalAlignment: Text.AlignHCenter
        }
        NodeButton {
            visible: !!root.p.repeatable && root.audioOnly
            x: 4
            y: parent.height - 22
            width: parent.width - 8
            height: 18
            glyph: "repeat"
            text: qsTr("Loop")
            variant: root.p.repeat ? "primary" : "secondary"
            tooltip: qsTr("Loop audio when it reaches the end")
            onClicked: root.node.controller.setParams(root.node.nodeId, { repeat: !root.p.repeat })
        }
    }
}
