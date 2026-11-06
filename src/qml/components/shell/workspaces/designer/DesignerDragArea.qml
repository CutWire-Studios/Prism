import QtQuick
import Prism

// Press-and-move source for the designer's drag ghost. A press without movement is a tap;
// the right button asks for the block menu.
MouseArea {
    id: root

    required property var designer
    property var panels: []
    property bool dragging: false
    property point _origin

    signal tapped()
    signal contextRequested()

    acceptedButtons: Qt.LeftButton | Qt.RightButton
    // The designer sits inside the settings page Flickable, which would otherwise take the
    // press over as a scroll once the pointer moves.
    preventStealing: true
    cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor

    onPressed: (mouse) => {
        if (mouse.button === Qt.RightButton) {
            root.contextRequested()
            return
        }
        root._origin = Qt.point(mouse.x, mouse.y)
    }

    onPositionChanged: (mouse) => {
        if (!(mouse.buttons & Qt.LeftButton))
            return
        if (!root.dragging) {
            if (Math.hypot(mouse.x - root._origin.x, mouse.y - root._origin.y) < Qt.styleHints.startDragDistance)
                return
            root.dragging = true
            root.designer.beginDrag(root.panels)
        }
        root.designer.moveDrag(root, mouse.x, mouse.y)
    }

    onReleased: (mouse) => {
        if (mouse.button !== Qt.LeftButton)
            return
        if (root.dragging) {
            root.dragging = false
            root.designer.endDrag()
        } else {
            root.tapped()
        }
    }

    onCanceled: {
        if (root.dragging) {
            root.dragging = false
            root.designer.cancelDrag()
        }
    }
}
