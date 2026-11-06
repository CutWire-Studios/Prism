import QtQuick
import Prism

// Poster image for a library entry: skeleton while the probe or decode runs, a kind glyph when
// there is no picture to show (audio, unreadable or missing files).
Item {
    id: root

    property bool folder: false
    property string kind: ""
    property string thumbKey: ""
    property string thumbRev: ""
    property bool probing: false
    property real iconSize: Theme.spacing3xl
    property color iconColor: Theme.c.mediaAssetsGridFrame.icon
    property int fillMode: Image.PreserveAspectCrop

    readonly property bool imageReady: image.status === Image.Ready

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusXs
        color: Theme.skeletonColor
        visible: !root.folder && (root.probing || image.status === Image.Loading)
        opacity: 0.6

        SequentialAnimation on opacity {
            running: parent.visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 700; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.6; duration: 700; easing.type: Easing.InOutSine }
        }
    }

    Image {
        id: image
        anchors.fill: parent
        visible: status === Image.Ready
        source: !root.folder && root.thumbKey.length > 0
                ? "image://mediabin/" + root.thumbKey + "?v=" + root.thumbRev : ""
        fillMode: root.fillMode
        asynchronous: true
        opacity: status === Image.Ready ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
        }
    }

    IconGlyph {
        anchors.centerIn: parent
        visible: root.folder || (!root.probing && image.status !== Image.Ready
                                 && image.status !== Image.Loading)
        glyph: root.folder ? Theme.icons.folder
               : root.kind === "audio" ? Theme.icons.music
               : root.kind === "image" ? Theme.icons.image
               : Theme.icons.film
        iconSize: root.iconSize
        iconColor: root.iconColor
    }
}
