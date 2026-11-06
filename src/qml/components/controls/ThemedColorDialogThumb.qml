import QtQuick
import Prism

Rectangle {
    property color fill
    property bool active: false

    width: Theme.touchUi ? 24 : 16
    height: width
    radius: Theme.c.themedColorDialogThumb.radius
    color: fill
    border.width: Theme.c.themedColorDialogThumb.borderWidth
    border.color: Theme.c.themedColorDialogThumb.border
    scale: active ? 1.15 : 1

    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    // Outer hairline so the ring still reads on white and pale colours.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -Theme.c.themedColorDialogThumb.outlineWidth
        radius: Theme.c.themedColorDialogThumb.radius
        color: "transparent"
        border.width: Theme.c.themedColorDialogThumb.outlineWidth
        border.color: Theme.c.themedColorDialogThumb.outline
    }
}
