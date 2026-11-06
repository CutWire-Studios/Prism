import QtQuick
import Prism

// Swatch button with the colour's hex value; opens the themed colour dialog with alpha.
// picked(color) fires only when the colour actually changed.
Rectangle {
    id: root

    property color swatch: "white"

    signal picked(color color)

    readonly property real _lightness: 0.299 * swatch.r + 0.587 * swatch.g + 0.114 * swatch.b

    function _hex2(v) {
        const h = Math.round(v * 255).toString(16)
        return h.length === 1 ? "0" + h : h
    }

    implicitWidth: 84
    implicitHeight: Theme.controlHeight
    radius: Theme.radiusSm
    color: swatch
    border.width: 1
    border.color: Theme.panelBorder

    Text {
        anchors.centerIn: parent
        text: swatch.a < 1 ? ("#" + root._hex2(swatch.a) + root._hex2(swatch.r) + root._hex2(swatch.g) + root._hex2(swatch.b)).toUpperCase()
                           : ("#" + root._hex2(swatch.r) + root._hex2(swatch.g) + root._hex2(swatch.b)).toUpperCase()
        color: root._lightness > 0.5 ? "#111111" : "#eeeeee"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: colorDialog.open()
    }

    ThemedColorDialog {
        id: colorDialog
        selectedColor: root.swatch
        onAccepted: {
            if (String(colorDialog.selectedColor) !== String(root.swatch))
                root.picked(colorDialog.selectedColor)
        }
    }
}
