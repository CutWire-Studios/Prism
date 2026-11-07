import QtQuick
import Prism

// Modulator: kind and a live bar of its current output.
Item {
    id: root

    required property var node
    readonly property var p: node.params
    readonly property var kindNames: [qsTr("LFO"), qsTr("Envelope"), qsTr("Map range"), qsTr("Smooth"),
                                      qsTr("Random"), qsTr("Trigger"), qsTr("Counter"), qsTr("Toggle"), qsTr("Math")]
    property real value: 0

    Timer {
        interval: 50
        repeat: true
        running: root.visible
        onTriggered: root.value = AppController.modulatorValue(root.node.nodeId)
    }

    NodeLabel {
        x: 4
        y: 30
        width: root.width - 8
        muted: true
        horizontalAlignment: Text.AlignHCenter
        text: root.kindNames[root.p.kind] || ""
    }

    Rectangle {
        x: 8
        y: 56
        width: root.width - 16
        height: 8
        radius: 4
        color: Theme.sliderTrack

        Rectangle {
            readonly property real lo: root.p.kind === 0 || root.p.kind === 4 ? root.p.min : (root.p.kind === 2 ? root.p.outMin : 0)
            readonly property real hi: root.p.kind === 0 || root.p.kind === 4 ? root.p.max : (root.p.kind === 2 ? root.p.outMax : 1)
            height: parent.height
            radius: parent.radius
            color: NodePalette.scriptHue
            width: hi === lo ? 0 : parent.width * Math.max(0, Math.min(1, (root.value - lo) / (hi - lo)))
        }
    }

    NodeLabel {
        x: 4
        y: 70
        width: root.width - 8
        horizontalAlignment: Text.AlignHCenter
        font.family: Theme.monoFontFamily
        text: root.value.toFixed(3)
    }
}
