import QtQuick
import Prism

// Duration chip painted on the bottom-left of a media thumbnail.
Rectangle {
    id: root

    property string label: ""

    visible: label.length > 0
    anchors.left: parent.left
    anchors.bottom: parent.bottom
    anchors.margins: Theme.spacingSm
    color: Theme.c.mediaAssetsDurationBadge.background
    radius: Theme.c.mediaAssetsDurationBadge.radius
    width: durationLabel.implicitWidth + Theme.spacingLg
    height: durationLabel.implicitHeight + Theme.spacingSm

    Text {
        id: durationLabel
        anchors.centerIn: parent
        text: root.label
        color: Theme.c.mediaAssetsDurationBadge.foreground
        font.pixelSize: Theme.fontSizeXs
        font.family: Theme.fontFamily
    }
}
