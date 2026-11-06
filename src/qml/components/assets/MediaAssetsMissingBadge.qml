import QtQuick
import Prism

// Marks a library entry whose file is gone from disk.
Rectangle {
    id: root

    property real size: 20
    property string tip: qsTr("Media file not found. Use Relink media… on this item to point it at the file again.")

    width: size
    height: size
    radius: Theme.c.missingMediaWarning.radius
    color: Theme.c.missingMediaWarning.background

    IconGlyph {
        anchors.centerIn: parent
        glyph: Theme.icons.warning
        iconSize: root.size * 0.65
        iconColor: Theme.c.missingMediaWarning.foreground
    }

    HoverHandler {
        id: hover
    }

    ThemedToolTip {
        text: root.tip
        visible: hover.hovered
    }
}
