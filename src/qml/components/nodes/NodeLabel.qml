import QtQuick
import Prism

// Small single-line text for node bodies.
Text {
    property bool muted: false

    color: muted ? Theme.mutedForeground : Theme.panelForeground
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeXs
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
    maximumLineCount: 1
}
