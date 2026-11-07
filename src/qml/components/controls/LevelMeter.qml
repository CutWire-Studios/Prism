import QtQuick
import Prism

// Peak bar for an audio node; level is 0..1 on a dB scale.
Item {
    id: root

    property real level: 0
    property bool vertical: false

    implicitWidth: vertical ? 4 : 60
    implicitHeight: vertical ? 40 : 4

    Rectangle {
        anchors.fill: parent
        radius: 1
        color: Theme.sliderTrack
    }

    Rectangle {
        radius: 1
        anchors.bottom: parent.bottom
        width: root.vertical ? parent.width : parent.width * root.level
        height: root.vertical ? parent.height * root.level : parent.height
        color: root.level > 0.92 ? Theme.destructive : root.level > 0.75 ? Theme.warning : Theme.constructive
    }
}
