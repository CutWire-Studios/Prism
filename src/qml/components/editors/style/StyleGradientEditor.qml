pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

// Gradient paint editor: a stop bar (drag a handle to move it, click the bar to add a stop,
// right-click a handle to remove it, double-click to recolour), the selected stop's colour and
// position, and the gradient's type and mapping. Field edits go out as patches to the gradient
// object; stop edits as stop operations (StyleLayers).
ColumnLayout {
    id: root

    property var gradient: ({})
    property bool spaceVisible: false
    property int selectedStop: 0

    signal patched(var patch)
    signal stopAdded(real pos)
    signal stopRemoved(int index)
    signal stopEdited(int index, real pos, string color)

    readonly property var stops: gradient.stops || []
    readonly property var kindOptions: StyleLayers.options("gradientKind")
    readonly property var spaceOptions: StyleLayers.options("gradientSpace")
    readonly property string kind: gradient.kind || "linear"
    readonly property var center: gradient.center || [0.5, 0.5]
    readonly property int activeStop: Math.max(0, Math.min(selectedStop, stops.length - 1))

    function cssColor(c) {
        const col = Qt.color(c)
        return "rgba(" + Math.round(col.r * 255) + "," + Math.round(col.g * 255) + ","
               + Math.round(col.b * 255) + "," + col.a + ")"
    }

    function patchCenter(i, v) {
        const c = root.center.slice()
        c[i] = v
        root.patched({ center: c })
    }

    Layout.fillWidth: true
    spacing: Theme.spacingMd

    onStopsChanged: barCanvas.requestPaint()

    Item {
        id: bar

        Layout.fillWidth: true
        implicitHeight: 40

        readonly property real barLeft: 8
        readonly property real barWidth: width - 16

        function posAt(x) {
            return Math.max(0, Math.min(1, (x - bar.barLeft) / bar.barWidth))
        }

        Rectangle {
            id: track
            x: bar.barLeft
            y: 2
            width: bar.barWidth
            height: 22
            radius: 3
            color: "transparent"
            border.width: 1
            border.color: Theme.panelBorder
            clip: true

            Canvas {
                id: barCanvas
                anchors.fill: parent
                onWidthChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const g = ctx.createLinearGradient(0, 0, width, 0)
                    for (const s of root.stops)
                        g.addColorStop(Math.max(0, Math.min(1, s.pos)), root.cssColor(s.color))
                    ctx.fillStyle = g
                    ctx.fillRect(0, 0, width, height)
                }
            }
        }

        MouseArea {
            x: bar.barLeft
            y: 0
            width: bar.barWidth
            height: 40
            onClicked: function (mouse) { root.stopAdded(bar.posAt(bar.barLeft + mouse.x)) }
        }

        Repeater {
            // A count model: a stop edit must not rebuild the handle that is being dragged.
            model: root.stops.length

            delegate: Rectangle {
                id: handle

                required property int index

                readonly property var stop: root.stops[index]
                readonly property bool current: index === root.activeStop

                x: bar.barLeft + bar.barWidth * Math.max(0, Math.min(1, stop.pos)) - 6
                y: 25
                width: 12
                height: 13
                radius: 3
                color: stop.color
                border.width: current ? 2 : 1
                border.color: current ? Theme.accentOnPanel : Theme.panelMuted

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    onPressed: function (mouse) {
                        root.selectedStop = handle.index
                        if (mouse.button === Qt.RightButton)
                            root.stopRemoved(handle.index)
                    }
                    onPositionChanged: function (mouse) {
                        if (pressed && (mouse.buttons & Qt.LeftButton)) {
                            const p = mapToItem(bar, mouse.x, mouse.y)
                            root.stopEdited(handle.index, bar.posAt(p.x), String(handle.stop.color))
                        }
                    }
                    onDoubleClicked: {
                        root.selectedStop = handle.index
                        stopColorDialog.open()
                    }
                }
            }
        }
    }

    ThemedColorDialog {
        id: stopColorDialog
        selectedColor: root.stops.length > 0 ? root.stops[root.activeStop].color : "white"
        onAccepted: {
            if (root.stops.length > 0)
                root.stopEdited(root.activeStop, root.stops[root.activeStop].pos, String(stopColorDialog.selectedColor))
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        ThemedLabel {
            text: qsTr("Stop")
            Layout.preferredWidth: 78
        }
        StyleColorButton {
            swatch: root.stops.length > 0 ? root.stops[root.activeStop].color : "white"
            onPicked: function (c) {
                if (root.stops.length > 0)
                    root.stopEdited(root.activeStop, root.stops[root.activeStop].pos, String(c))
            }
        }
        ThemedNumberField {
            Layout.preferredWidth: 78
            from: 0
            to: 100
            step: 1
            decimals: 1
            unit: "%"
            value: root.stops.length > 0 ? root.stops[root.activeStop].pos * 100 : 0
            onEdited: function (v) {
                if (root.stops.length > 0)
                    root.stopEdited(root.activeStop, Math.max(0, Math.min(100, v)) / 100, root.stops[root.activeStop].color)
            }
        }
        Item { Layout.fillWidth: true }
        ThemedButton {
            text: qsTr("Remove")
            enabled: root.stops.length > 2
            onClicked: root.stopRemoved(root.activeStop)
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        label: qsTr("Type")

        ThemedComboBox {
            Layout.fillWidth: true
            model: root.kindOptions
            textRole: "label"
            valueRole: "value"
            currentIndex: indexOfValue(root.kind)
            onActivated: root.patched({ kind: currentValue })
        }
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.kind !== "radial"
        label: qsTr("Angle")
        from: -360
        to: 360
        step: 1
        decimals: 0
        unit: "°"
        value: root.gradient.angle || 0
        onEdited: function (v) { root.patched({ angle: v }) }
    }

    StyleSliderRow {
        Layout.fillWidth: true
        label: qsTr("Offset")
        from: -2
        to: 2
        step: 0.01
        decimals: 2
        value: root.gradient.offset || 0
        onEdited: function (v) { root.patched({ offset: v }) }
    }

    StyleSliderRow {
        Layout.fillWidth: true
        label: qsTr("Speed")
        from: -4
        to: 4
        step: 0.05
        decimals: 2
        unit: "/s"
        value: root.gradient.offsetSpeed || 0
        onEdited: function (v) { root.patched({ offsetSpeed: v }) }
    }

    StyleSliderRow {
        Layout.fillWidth: true
        label: qsTr("Scale")
        from: 0.1
        to: 8
        step: 0.05
        decimals: 2
        value: root.gradient.scale || 1
        onEdited: function (v) { root.patched({ scale: v }) }
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.kind !== "linear"
        label: qsTr("Center X")
        from: 0
        to: 1
        step: 0.01
        decimals: 2
        value: root.center[0]
        onEdited: function (v) { root.patchCenter(0, v) }
    }

    StyleSliderRow {
        Layout.fillWidth: true
        visible: root.kind !== "linear"
        label: qsTr("Center Y")
        from: 0
        to: 1
        step: 0.01
        decimals: 2
        value: root.center[1]
        onEdited: function (v) { root.patchCenter(1, v) }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingLg

        ThemedCheckBox {
            text: qsTr("Repeat")
            checked: root.gradient.repeat === true
            onClicked: root.patched({ repeat: checked })
        }
        ThemedCheckBox {
            text: qsTr("OKLab")
            checked: root.gradient.oklab === true
            onClicked: root.patched({ oklab: checked })
        }
        Item { Layout.fillWidth: true }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.spaceVisible
        label: qsTr("Map to")

        ThemedComboBox {
            Layout.fillWidth: true
            model: root.spaceOptions
            textRole: "label"
            valueRole: "value"
            currentIndex: indexOfValue(root.gradient.space || "block")
            onActivated: root.patched({ space: currentValue })
        }
    }
}
