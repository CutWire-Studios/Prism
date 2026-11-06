pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// Where each layer of a Layer node sits on its canvas. Changes reach the output while editing;
// Cancel puts every rect, visibility and the canvas size back.
ThemedDialog {
    id: root

    property var nodeId: 0
    property var slots: []
    property var original: []
    property int canvasW: 1920
    property int canvasH: 1080
    property int origW: 1920
    property int origH: 1080
    property bool canvasCustom: false
    property bool origCustom: false
    property int selected: -1
    property bool confirmed: false

    readonly property var current: {
        for (const s of slots)
            if (s.slot === selected)
                return s
        return null
    }
    readonly property var presets: [
        { label: qsTr("Preset…"), w: 0, h: 0 },
        { label: "1280 × 720", w: 1280, h: 720 },
        { label: "1920 × 1080", w: 1920, h: 1080 },
        { label: "3840 × 2160", w: 3840, h: 2160 },
        { label: "1080 × 1920", w: 1080, h: 1920 },
        { label: "1024 × 768", w: 1024, h: 768 }
    ]

    title: qsTr("Layer Layout")
    preferredWidth: 1200
    acceptText: qsTr("Done")
    acceptOnReturn: false

    function openFor(id, kind) {
        const size = TransformTools.canvasSize(id)
        nodeId = id
        canvasW = origW = size.width
        canvasH = origH = size.height
        canvasCustom = origCustom = TransformTools.canvasCustom(id)
        slots = TransformTools.layerSlots(id)
        original = slots.map(s => Object.assign({}, s))
        selected = slots.length > 0 ? slots[0].slot : -1
        confirmed = false
        open()
    }

    function clampRect(r) {
        const w = Math.min(1, Math.max(0.02, r.w))
        const h = Math.min(1, Math.max(0.02, r.h))
        return {
            x: Math.min(Math.max(0, r.x), Math.max(0, 1 - w)),
            y: Math.min(Math.max(0, r.y), Math.max(0, 1 - h)),
            w: w,
            h: h
        }
    }

    function setRect(slot, rect) {
        const c = clampRect(rect)
        slots = slots.map(s => s.slot === slot ? Object.assign({}, s, c) : s)
        TransformTools.setSlotRect(nodeId, slot, c.x, c.y, c.w, c.h)
    }

    function setVisible(slot, on) {
        slots = slots.map(s => s.slot === slot ? Object.assign({}, s, { visible: on }) : s)
        TransformTools.setSlotVisible(nodeId, slot, on)
    }

    function setCanvas(w, h) {
        canvasW = w
        canvasH = h
        TransformTools.setCanvasSize(nodeId, w, h)
        canvasCustom = true
    }

    function setCanvasCustom(on) {
        if (on) {
            setCanvas(canvasW, canvasH)
            return
        }
        TransformTools.setCanvasCustom(nodeId, false)
        canvasCustom = false
        const size = TransformTools.canvasSize(nodeId)
        canvasW = size.width
        canvasH = size.height
    }

    function fit() {
        const s = current
        if (!s)
            return
        const canvasAr = canvasW / canvasH
        const srcAr = probe.implicitHeight > 0 && probe.implicitWidth > 0
                      ? probe.implicitWidth / probe.implicitHeight : canvasAr
        let w = 1, h = 1
        if (srcAr > canvasAr)
            h = canvasAr / srcAr
        else
            w = srcAr / canvasAr
        setRect(s.slot, { x: (1 - w) / 2, y: (1 - h) / 2, w: w, h: h })
    }

    function revert() {
        TransformTools.setCanvasSize(nodeId, origW, origH)
        TransformTools.setCanvasCustom(nodeId, origCustom)
        for (const s of original) {
            TransformTools.setSlotRect(nodeId, s.slot, s.x, s.y, s.w, s.h)
            TransformTools.setSlotVisible(nodeId, s.slot, s.visible)
        }
    }

    onAccepted: confirmed = true
    onClosed: if (!confirmed) revert()

    Image {
        id: probe
        visible: false
        source: root.current && root.current.sourceNode > 0 ? "image://nodethumb/" + root.current.sourceNode : ""
    }

    component PlacementField: ColumnLayout {
        id: field
        property string label
        property real value: 0
        property real maximum: 8192
        signal edited(real v)
        Layout.fillWidth: true
        spacing: 2
        ThemedLabel { text: field.label }
        ThemedNumberField {
            Layout.fillWidth: true
            from: 0
            to: field.maximum
            value: field.value
            onEdited: v => field.edited(v)
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingLg
        implicitHeight: 560

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            ThemedLabel {
                Layout.fillWidth: true
                text: qsTr("Changes apply to the output in real time")
            }
            ThemedSwitch {
                text: qsTr("Custom canvas")
                checked: root.canvasCustom
                onToggled: root.setCanvasCustom(checked)
            }
            ThemedNumberField {
                Layout.preferredWidth: 84
                enabled: root.canvasCustom
                from: 16
                to: 8192
                value: root.canvasW
                onEdited: v => root.setCanvas(Math.round(v), root.canvasH)
            }
            ThemedLabel { text: "×" }
            ThemedNumberField {
                Layout.preferredWidth: 84
                enabled: root.canvasCustom
                from: 16
                to: 8192
                value: root.canvasH
                onEdited: v => root.setCanvas(root.canvasW, Math.round(v))
            }
            ThemedComboBox {
                Layout.preferredWidth: 140
                enabled: root.canvasCustom
                textRole: "label"
                model: root.presets
                currentIndex: 0
                onActivated: index => {
                    const p = root.presets[index]
                    if (p.w > 0)
                        root.setCanvas(p.w, p.h)
                    currentIndex = 0
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingLg

            LayoutCanvas {
                Layout.fillWidth: true
                Layout.fillHeight: true
                canvasW: root.canvasW
                canvasH: root.canvasH
                slots: root.slots
                selectedSlot: root.selected
                onSlotSelected: slot => root.selected = slot
                onRectEdited: (slot, x, y, w, h) => root.setRect(slot, { x: x, y: y, w: w, h: h })
            }

            ColumnLayout {
                Layout.preferredWidth: 256
                Layout.fillHeight: true
                spacing: Theme.spacingMd

                ThemedLabel {
                    text: qsTr("LAYERS (top to bottom)")
                    font.weight: Font.Medium
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: root.slots
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar { }

                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property bool active: modelData.slot === root.selected
                        width: ListView.view.width
                        height: 32
                        radius: Theme.radiusSm
                        color: active ? Theme.popoverHover : "transparent"
                        border.width: active ? Theme.borderWidth : 0
                        border.color: Theme.primary

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selected = row.modelData.slot
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: Theme.spacingMd

                            IconButton {
                                glyph: row.modelData.visible ? "eye" : "eye-off"
                                buttonSize: 22
                                tooltip: qsTr("Show / hide this layer")
                                onClicked: root.setVisible(row.modelData.slot, !row.modelData.visible)
                            }
                            Image {
                                Layout.preferredWidth: 38
                                Layout.preferredHeight: 22
                                fillMode: Image.PreserveAspectFit
                                source: row.modelData.sourceNode > 0 ? "image://nodethumb/" + row.modelData.sourceNode : ""
                            }
                            ThemedLabel {
                                Layout.fillWidth: true
                                tone: "default"
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                text: row.modelData.name
                            }
                        }
                    }
                }

                ThemedLabel {
                    text: qsTr("PLACEMENT")
                    font.weight: Font.Medium
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacingMd
                    rowSpacing: Theme.spacingMd
                    enabled: root.current !== null

                    PlacementField {
                        label: "X"
                        maximum: root.canvasW
                        value: root.current ? Math.round(root.current.x * root.canvasW) : 0
                        onEdited: v => root.setRect(root.selected, { x: v / root.canvasW, y: root.current.y, w: root.current.w, h: root.current.h })
                    }
                    PlacementField {
                        label: "Y"
                        maximum: root.canvasH
                        value: root.current ? Math.round(root.current.y * root.canvasH) : 0
                        onEdited: v => root.setRect(root.selected, { x: root.current.x, y: v / root.canvasH, w: root.current.w, h: root.current.h })
                    }
                    PlacementField {
                        label: "W"
                        maximum: root.canvasW
                        value: root.current ? Math.round(root.current.w * root.canvasW) : 0
                        onEdited: v => root.setRect(root.selected, { x: root.current.x, y: root.current.y, w: v / root.canvasW, h: root.current.h })
                    }
                    PlacementField {
                        label: "H"
                        maximum: root.canvasH
                        value: root.current ? Math.round(root.current.h * root.canvasH) : 0
                        onEdited: v => root.setRect(root.selected, { x: root.current.x, y: root.current.y, w: root.current.w, h: v / root.canvasH })
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMd
                    enabled: root.current !== null

                    ThemedButton {
                        Layout.fillWidth: true
                        text: qsTr("Fit")
                        tooltip: qsTr("Fit inside the canvas, keeping the source aspect")
                        onClicked: root.fit()
                    }
                    ThemedButton {
                        Layout.fillWidth: true
                        text: qsTr("Stretch")
                        tooltip: qsTr("Stretch to fill the whole canvas")
                        onClicked: root.setRect(root.selected, { x: 0, y: 0, w: 1, h: 1 })
                    }
                    ThemedButton {
                        Layout.fillWidth: true
                        text: qsTr("Center")
                        tooltip: qsTr("Center on the canvas, keeping the current size")
                        onClicked: root.setRect(root.selected, { x: (1 - root.current.w) / 2, y: (1 - root.current.h) / 2,
                                                                 w: root.current.w, h: root.current.h })
                    }
                }
            }
        }

        ThemedLabel {
            Layout.fillWidth: true
            text: qsTr("Drag to move, handles resize, Shift keeps aspect, Alt disables snapping, arrow keys nudge, double-click fills the canvas.")
        }
    }
}
