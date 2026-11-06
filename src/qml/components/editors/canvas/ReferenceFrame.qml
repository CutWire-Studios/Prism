import QtQuick
import Prism

// A reference frame fitted into the available area, with room around it (overscan) for handles
// that may leave the picture. Children are overlays that fill this item and read frameRect.
Item {
    id: root

    property var nodeId: 0
    property real overscan: 0
    property matrix4x4 warp
    default property alias overlay: overlayHost.data

    readonly property real aspect: view.imageAspect > 0 ? view.imageAspect : 16 / 9
    readonly property rect frameRect: Qt.rect(box.x, box.y, box.width, box.height)
    readonly property bool hasFrame: view.ready

    implicitWidth: 480
    implicitHeight: 270
    clip: true

    Rectangle {
        anchors.fill: parent
        color: Theme.previewLetterbox
    }

    Item {
        id: box
        readonly property real availW: Math.max(1, root.width - 2 * root.overscan)
        readonly property real availH: Math.max(1, root.height - 2 * root.overscan)
        width: Math.min(availW, availH * root.aspect)
        height: width / root.aspect
        x: (root.width - width) / 2
        y: (root.height - height) / 2

        ClipFrameView {
            id: view
            anchors.fill: parent
            nodeId: root.nodeId
            transform: Matrix4x4 { matrix: root.warp }
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: Theme.borderWidth
            border.color: Theme.guideMedium
        }
    }

    ThemedLabel {
        anchors.centerIn: parent
        visible: !view.ready
        text: qsTr("No preview")
    }

    Item {
        id: overlayHost
        anchors.fill: parent
    }
}
