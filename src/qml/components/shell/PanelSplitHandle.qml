import QtQuick
import QtQuick.Templates as T
import Prism

// Grip for the editor's SplitViews.
//
// Handles were 1px and transparent at rest, so they were both undiscoverable and
// nearly impossible to hit. This is a real target with a visible grip that fades
// out once the pointer lands on it.
//
// The editor has three of these now (the outer, inner and workspace splits), so
// the delegate lives here rather than being copied per SplitView. Pass the split
// it belongs to as `view`: its orientation decides which way the grip runs and
// which resize cursor is shown.
Rectangle {
    id: root

    // Typed against the Templates base so the Basic SplitViews in Main.qml assign
    // cleanly and `orientation` resolves statically.
    property T.SplitView view

    readonly property bool horizontal: view && view.orientation === Qt.Horizontal
    // SplitView writes the SplitHandle attached properties on the delegate root
    // and nowhere else — a child that reads `SplitHandle.hovered` directly gets
    // its own attached object, which nothing ever updates. Aliased here so the
    // grip below reads the handle's real state.
    readonly property bool handleHovered: T.SplitHandle.hovered
    readonly property bool handlePressed: T.SplitHandle.pressed

    onHandlePressedChanged: {
        if (handlePressed)
            Haptics.pickUp()
        else
            Haptics.drop()
    }

    implicitWidth: horizontal ? Theme.panelGap : (view ? view.width : 0)
    implicitHeight: horizontal ? (view ? view.height : 0) : Theme.panelGap
    color: handlePressed ? Theme.c.panelSplitHandle.backgroundPressed
                         : (handleHovered ? Theme.c.panelSplitHandle.backgroundHover : "transparent")

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    // Grip dots: a resting affordance that reads as draggable. Laid out across
    // the handle's short axis, so they run down a vertical handle and along a
    // horizontal one.
    Grid {
        anchors.centerIn: parent
        rows: root.horizontal ? 3 : 1
        columns: root.horizontal ? 1 : 3
        rowSpacing: Theme.spacingSm
        columnSpacing: Theme.spacingSm
        opacity: root.handleHovered || root.handlePressed ? 0 : 1

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Repeater {
            model: 3
            Rectangle {
                width: 2
                height: 2
                radius: Theme.c.panelSplitHandle.gripRadius
                color: Theme.c.panelSplitHandle.grip
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
    }
}
