import QtQuick
import QtQuick.Controls.impl
import Prism

// Renders a Lucide icon from resources/icons/<name>.svg. IconImage applies its
// color property directly to the SVG icon.
// `glyph` is the Lucide file name without extension.
Item {
    id: root

    property string glyph: ""
    property real iconSize: 16
    property color iconColor: Theme.c.iconGlyph.icon
    // Set for loader glyphs, which otherwise render as a frozen circle.
    property bool spinning: false

    implicitWidth: iconSize
    implicitHeight: iconSize

    IconImage {
        id: iconImage
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        source: root.glyph.length > 0
                ? "qrc:/qt/qml/Prism/resources/icons/" + root.glyph + ".svg"
                : ""

        fillMode: Image.PreserveAspectFit
        color: root.iconColor

        RotationAnimator {
            target: iconImage
            from: 0
            to: 360
            duration: 1100          // matches CircularProgress.qml
            loops: Animation.Infinite
            running: root.spinning && root.visible
        }
    }
}
