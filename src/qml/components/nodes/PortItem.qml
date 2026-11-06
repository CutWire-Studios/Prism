pragma ComponentBehavior: Bound
import QtQuick
import Prism

// One connection port, centred on (x + radius, y + radius) of its node. Presses start or finish
// a connection through the canvas; the canvas does the hit-testing for the drop target.
Item {
    id: root

    required property var canvas
    required property var node          // NodeDelegate
    required property bool isOutput
    required property int portIndex
    required property int kind
    property bool connected: false
    property string label: ""

    readonly property real radius: 4.5
    readonly property string key: node.nodeId + (isOutput ? ":o:" : ":i:") + portIndex
    readonly property bool hovered: canvas.hoverPortKey === key
    // Only meaningful while a connection is pending: whether it could be completed here.
    readonly property bool compatible: {
        void root.canvas.controller.connecting
        void root.canvas.controller.revision
        return root.canvas.controller.connecting
            && root.canvas.controller.canConnect(root.node.nodeId, root.isOutput, root.portIndex)
    }
    readonly property bool incompatible: root.canvas.controller.connecting && !compatible
        && !(root.canvas.connectNode === root.node.nodeId && root.canvas.connectIsOutput === root.isOutput
             && root.canvas.connectPort === root.portIndex)
    readonly property color hue: NodePalette.portColor(kind)

    width: radius * 2
    height: radius * 2

    Rectangle {
        anchors.centerIn: parent
        width: root.hovered && root.compatible ? 14 : (root.compatible ? 12 : parent.width)
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: root.hue
        opacity: root.compatible ? 0.8 : 0
        Behavior on width { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.connected || root.hovered ? root.hue : Theme.panelAccent
        border.width: 1.5
        border.color: root.hue
        opacity: root.incompatible ? 0.3 : 1
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
    }

    MouseArea {
        id: area
        anchors.centerIn: parent
        width: 20
        height: 20
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        preventStealing: true

        function worldPos(mouse) {
            return mapToItem(root.canvas.world, mouse.x, mouse.y)
        }

        onPressed: (mouse) => root.canvas.portPressed(root, worldPos(mouse))
        onPositionChanged: (mouse) => {
            if (pressed)
                root.canvas.portDragged(worldPos(mouse))
        }
        onReleased: (mouse) => root.canvas.portReleased(root, worldPos(mouse))
        onCanceled: root.canvas.portDragCanceled()

        ThemedToolTip {
            visible: area.containsMouse && !area.pressed && !root.canvas.controller.connecting
            text: (root.label.length > 0 ? root.label + " - " : "") + NodePalette.portKindName(root.kind)
        }
    }
}
