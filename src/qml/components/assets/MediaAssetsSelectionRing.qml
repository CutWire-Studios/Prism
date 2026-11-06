import QtQuick
import Prism

// Painted above the thumbnail and busy scrim so it is never covered by them. Square corners on
// purpose: a rounded ring reads chunkier than the thumbnail's own radius at this border width.
Rectangle {
    id: root

    property bool selected: false

    z: 2
    anchors.fill: parent
    radius: Theme.c.mediaAssetsSelectionRing.radius
    color: "transparent"
    border.width: root.selected ? Theme.c.mediaAssetsSelectionRing.borderWidth : 0
    border.color: Theme.c.mediaAssetsSelectionRing.border

    Behavior on border.width {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
}
