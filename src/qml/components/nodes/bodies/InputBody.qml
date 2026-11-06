pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Source card: thumbnail, name, loop toggle and edit button, plus the audio strip when the
// source carries audio. Geometry matches ClipGraph's Input node (20.5 header, 175 card, 54 strip).
Item {
    id: root

    required property var node
    readonly property var p: node.params
    readonly property var source: p.source ? p.source : ({})
    readonly property bool hasAudio: !!p.hasAudio
    readonly property real cardTop: 20.5
    readonly property real stripTop: cardTop + 175

    // SourceDescriptor::Kind order.
    readonly property var kindIcons: ["film", "image", "layout-grid", "camera", "monitor",
        "square-dashed", "app-window", "sparkles", "film", "radio", "smartphone", "type",
        "music", "shapes", "shapes"]

    // Bumped when the source changes so the image provider is asked for a fresh thumbnail.
    property int thumbVersion: 0
    readonly property string sourceKey: JSON.stringify(source)
    onSourceKeyChanged: ++thumbVersion

    Rectangle {
        id: thumb
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
            glyph: root.kindIcons[root.source.kind] || "film"
            iconSize: Theme.iconSizeXl
            iconColor: Theme.mutedForeground
            visible: image.status !== Image.Ready
        }

        Image {
            id: image
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            source: "image://nodethumb/" + root.node.nodeId + "?v=" + root.thumbVersion
        }
    }

    NodeLabel {
        x: 6
        y: thumb.y + thumb.height + 4
        width: root.width - 12
        height: 16
        text: root.node.model.title
        horizontalAlignment: Text.AlignHCenter
    }

    NodeButton {
        x: 6
        y: thumb.y + thumb.height + 24
        width: root.width - 12
        glyph: "repeat"
        text: qsTr("Loop")
        variant: root.p.repeat ? "primary" : "secondary"
        tooltip: qsTr("Loop clip when it reaches the end")
        onClicked: root.node.controller.setParams(root.node.nodeId, { repeat: !root.p.repeat })
    }

    NodeButton {
        x: 6
        y: thumb.y + thumb.height + 48
        width: root.width - 12
        text: qsTr("Edit")
        tooltip: qsTr("Edit trim, crop and source settings")
        onClicked: root.node.requestEditor("source")
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
            x: 4
            y: parent.height - 22
            width: parent.width - 8
            height: 18
            text: qsTr("Edit Audio")
            onClicked: root.node.requestEditor("audio")
        }
    }
}
