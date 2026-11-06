pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Prism

// Ordered shading layer stack (front-most layer on top) with the detail panel for the selected
// layer. Each edit emits edited(layers), the whole new array, which the owning editor stores.
ColumnLayout {
    id: root

    property var layers: []
    property bool scopeVisible: false
    property bool gradientSpaceVisible: false
    property int selectedIndex: -1

    signal edited(var layers)

    readonly property var layerKinds: StyleLayers.options("layerKind")
    readonly property var paintKinds: StyleLayers.options("paintKind")
    readonly property var blendModes: StyleLayers.options("blend")
    readonly property var strokeAligns: StyleLayers.options("strokeAlign")
    readonly property var dashes: StyleLayers.options("dash")
    readonly property var scopes: StyleLayers.options("scope")
    readonly property var effectOptions: StyleLayers.options("effect")

    readonly property var rows: {
        const out = []
        for (let i = root.layers.length - 1; i >= 0; --i)
            out.push({ index: i, layer: root.layers[i] })
        return out
    }

    readonly property var current: selectedIndex >= 0 && selectedIndex < layers.length ? layers[selectedIndex] : null
    readonly property bool hasSelection: current !== null
    readonly property var c: current || root._emptyLayer
    readonly property var _emptyLayer: ({
        kind: "", enabled: true, opacity: 1, blend: "normal", width: 0, offsetX: 0, offsetY: 0,
        blur: 0, spread: 0, dashOffset: 0, trimStart: 0, trimEnd: 1, sketchLength: 0,
        sketchDeviation: 0, sketchSeed: 0, extrudeSteps: 8, extrudeAngle: 45, extrudeDarken: 0.5,
        knockout: false, strokeAlign: "outside", dash: "solid", scope: "all",
        paint: { kind: "solid", color: "#ffffffff", gradient: ({}), texture: ({ offset: [0, 0] }), effect: ({ params: ({}) }) }
    })
    readonly property string kind: c.kind
    readonly property string paintKind: c.paint.kind
    readonly property bool painted: kind === "fill" || kind === "stroke"
    readonly property string shownPaint: painted ? paintKind : "solid"
    readonly property string effectId: c.paint.effect && c.paint.effect.id ? c.paint.effect.id : ""
    readonly property string texturePath: c.paint.texture && c.paint.texture.path ? c.paint.texture.path : ""

    spacing: Theme.spacingMd
    Layout.fillWidth: true

    function _keepSelection() {
        if (selectedIndex < 0 || selectedIndex >= layers.length)
            selectedIndex = layers.length - 1
    }

    onLayersChanged: _keepSelection()
    Component.onCompleted: _keepSelection()

    function patch(p) {
        root.edited(StyleLayers.patchAt(root.layers, root.selectedIndex, p))
    }

    function addLayer(kind) {
        const next = StyleLayers.add(root.layers, kind)
        root.selectedIndex = next.length - 1
        root.edited(next)
    }

    function duplicateSelected() {
        const from = root.selectedIndex
        if (from < 0)
            return
        const next = StyleLayers.duplicateAt(root.layers, from)
        root.selectedIndex = from + 1
        root.edited(next)
    }

    function removeSelected() {
        const from = root.selectedIndex
        if (from < 0)
            return
        const next = StyleLayers.removeAt(root.layers, from)
        root.selectedIndex = Math.min(from, next.length - 1)
        root.edited(next)
    }

    function moveSelected(delta) {
        const from = root.selectedIndex
        if (from < 0 || from + delta < 0 || from + delta >= root.layers.length)
            return
        root.selectedIndex = from + delta
        root.edited(StyleLayers.moveAt(root.layers, from, delta))
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        ThemedLabel {
            Layout.fillWidth: true
            size: "xs"
            tone: "default"
            text: qsTr("LAYERS")
            font.weight: Font.DemiBold
        }

        ThemedButton {
            id: addButton
            text: qsTr("Add layer")
            glyph: Theme.icons.plus
            onClicked: addMenu.popup(addButton, 0, height)
        }
    }

    ThemedContextMenu {
        id: addMenu

        Repeater {
            model: root.layerKinds

            delegate: ThemedMenuItem {
                id: addItem
                required property var modelData
                text: modelData.label
                onTriggered: root.addLayer(addItem.modelData.value)
            }
        }
    }

    ListView {
        id: list

        Layout.fillWidth: true
        Layout.preferredHeight: 136
        clip: true
        spacing: 2
        model: root.rows

        delegate: Rectangle {
            id: row

            required property var modelData

            readonly property bool selected: modelData.index === root.selectedIndex

            width: list.width
            height: 30
            radius: Theme.radiusSm
            color: selected ? Theme.panelSecondaryBg : "transparent"

            MouseArea {
                anchors.fill: parent
                z: -1
                onClicked: root.selectedIndex = row.modelData.index
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingSm
                anchors.rightMargin: Theme.spacingSm
                spacing: Theme.spacingSm

                ThemedCheckBox {
                    checked: row.modelData.layer.enabled
                    onClicked: root.edited(StyleLayers.patchAt(root.layers, row.modelData.index, { enabled: checked }))
                }

                ThemedLabel {
                    Layout.fillWidth: true
                    size: "sm"
                    tone: "default"
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    text: StyleLayers.layerLabel(row.modelData.layer)
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingXs

        ThemedButton {
            text: qsTr("Duplicate")
            variant: "secondary"
            enabled: root.hasSelection
            onClicked: root.duplicateSelected()
        }
        Item { Layout.fillWidth: true }
        ThemedButton {
            glyph: Theme.icons.chevronUp
            tooltip: qsTr("Bring forward")
            variant: "ghost"
            enabled: root.hasSelection && root.selectedIndex < root.layers.length - 1
            onClicked: root.moveSelected(1)
        }
        ThemedButton {
            glyph: Theme.icons.chevronDown
            tooltip: qsTr("Send backward")
            variant: "ghost"
            enabled: root.hasSelection && root.selectedIndex > 0
            onClicked: root.moveSelected(-1)
        }
        ThemedButton {
            glyph: Theme.icons.trash
            tooltip: qsTr("Delete layer")
            variant: "ghost"
            enabled: root.hasSelection
            onClicked: root.removeSelected()
        }
    }

    FileDialog {
        id: textureDialog
        title: qsTr("Choose image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"), qsTr("All files (*)")]
        onAccepted: root.patch({ paint: { texture: { path: StyleLayers.localPath(selectedFile) } } })
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: root.hasSelection
        spacing: Theme.spacingMd

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.painted
            spacing: Theme.spacingMd

            StyleFieldRow {
                Layout.fillWidth: true
                label: qsTr("Paint")

                ThemedComboBox {
                    Layout.fillWidth: true
                    model: root.paintKinds
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: indexOfValue(root.paintKind)
                    onActivated: root.edited(StyleLayers.setPaintKindAt(root.layers, root.selectedIndex, currentValue))
                }
            }

            StyleGradientEditor {
                Layout.fillWidth: true
                visible: root.paintKind === "gradient"
                gradient: root.c.paint.gradient
                spaceVisible: root.gradientSpaceVisible
                onPatched: function (p) { root.patch({ paint: { gradient: p } }) }
                onStopAdded: function (pos) { root.edited(StyleLayers.addGradientStopAt(root.layers, root.selectedIndex, pos)) }
                onStopRemoved: function (i) { root.edited(StyleLayers.removeGradientStopAt(root.layers, root.selectedIndex, i)) }
                onStopEdited: function (i, pos, color) {
                    root.edited(StyleLayers.setGradientStopAt(root.layers, root.selectedIndex, i, pos, color))
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: root.paintKind === "texture"
                spacing: Theme.spacingMd

                StyleFieldRow {
                    Layout.fillWidth: true
                    label: qsTr("Image")

                    ThemedButton {
                        Layout.fillWidth: true
                        variant: "secondary"
                        text: root.texturePath.length > 0 ? root.texturePath.split("/").pop() : qsTr("Choose image…")
                        onClicked: textureDialog.open()
                    }
                }
                StyleSliderRow {
                    Layout.fillWidth: true
                    label: qsTr("Scale")
                    from: 0.05
                    to: 8
                    step: 0.05
                    decimals: 2
                    value: root.c.paint.texture.scale || 1
                    onEdited: function (v) { root.patch({ paint: { texture: { scale: v } } }) }
                }
                StyleSliderRow {
                    Layout.fillWidth: true
                    label: qsTr("Angle")
                    from: -360
                    to: 360
                    step: 1
                    decimals: 0
                    unit: "°"
                    value: root.c.paint.texture.angle || 0
                    onEdited: function (v) { root.patch({ paint: { texture: { angle: v } } }) }
                }
                StyleSliderRow {
                    Layout.fillWidth: true
                    label: qsTr("Offset X")
                    from: -2000
                    to: 2000
                    step: 1
                    decimals: 0
                    unit: "px"
                    value: root.c.paint.texture.offset ? root.c.paint.texture.offset[0] : 0
                    onEdited: function (v) {
                        root.patch({ paint: { texture: { offset: [v, root.c.paint.texture.offset[1]] } } })
                    }
                }
                StyleSliderRow {
                    Layout.fillWidth: true
                    label: qsTr("Offset Y")
                    from: -2000
                    to: 2000
                    step: 1
                    decimals: 0
                    unit: "px"
                    value: root.c.paint.texture.offset ? root.c.paint.texture.offset[1] : 0
                    onEdited: function (v) {
                        root.patch({ paint: { texture: { offset: [root.c.paint.texture.offset[0], v] } } })
                    }
                }
                ThemedCheckBox {
                    text: qsTr("Tile")
                    checked: root.c.paint.texture.tile === true
                    onClicked: root.patch({ paint: { texture: { tile: checked } } })
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: root.paintKind === "effect"
                spacing: Theme.spacingMd

                StyleFieldRow {
                    Layout.fillWidth: true
                    label: qsTr("Effect")

                    ThemedComboBox {
                        Layout.fillWidth: true
                        model: root.effectOptions
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: indexOfValue(root.effectId)
                        onActivated: root.edited(StyleLayers.setEffectAt(root.layers, root.selectedIndex, currentValue))
                    }
                }
                StyleParamForm {
                    Layout.fillWidth: true
                    specs: StyleLayers.effectParams(root.effectId)
                    values: StyleLayers.effectValues(root.layers, root.selectedIndex)
                    onChanged: function (id, v) {
                        root.edited(StyleLayers.setEffectParamAt(root.layers, root.selectedIndex, id, v))
                    }
                }
            }
        }

        StyleFieldRow {
            Layout.fillWidth: true
            visible: root.shownPaint === "solid" || root.shownPaint === "texture" || root.shownPaint === "effect"
            label: root.shownPaint === "solid" ? qsTr("Colour") : qsTr("Tint")

            StyleColorButton {
                swatch: root.c.paint.color
                onPicked: function (col) { root.patch({ paint: { color: String(col) } }) }
            }
        }

        StyleSliderRow {
            Layout.fillWidth: true
            label: qsTr("Opacity")
            from: 0
            to: 1
            step: 0.01
            decimals: 2
            value: root.c.opacity
            onEdited: function (v) { root.patch({ opacity: v }) }
        }

        StyleFieldRow {
            Layout.fillWidth: true
            label: qsTr("Blend")

            ThemedComboBox {
                Layout.fillWidth: true
                model: root.blendModes
                textRole: "label"
                valueRole: "value"
                currentIndex: indexOfValue(root.c.blend)
                onActivated: root.patch({ blend: currentValue })
            }
        }

        ThemedCheckBox {
            Layout.fillWidth: true
            visible: root.kind === "fill"
            text: qsTr("Knockout (punch through layers beneath)")
            checked: root.c.knockout === true
            onClicked: root.patch({ knockout: checked })
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.kind === "stroke"
            spacing: Theme.spacingMd

            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Width")
                from: 0
                to: 100
                step: 0.5
                decimals: 1
                unit: "px"
                value: root.c.width
                onEdited: function (v) { root.patch({ width: v }) }
            }
            StyleFieldRow {
                Layout.fillWidth: true
                label: qsTr("Placement")

                ThemedComboBox {
                    Layout.fillWidth: true
                    model: root.strokeAligns
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: indexOfValue(root.c.strokeAlign)
                    onActivated: root.patch({ strokeAlign: currentValue })
                }
            }
            StyleFieldRow {
                Layout.fillWidth: true
                label: qsTr("Dash")

                ThemedComboBox {
                    Layout.fillWidth: true
                    model: root.dashes
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: indexOfValue(root.c.dash)
                    onActivated: root.patch({ dash: currentValue })
                }
            }
            StyleSliderRow {
                Layout.fillWidth: true
                visible: root.c.dash !== "solid"
                label: qsTr("Dash offset")
                from: 0
                to: 20
                step: 0.1
                decimals: 1
                value: root.c.dashOffset
                onEdited: function (v) { root.patch({ dashOffset: v }) }
            }
            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Trim start")
                from: 0
                to: 1
                step: 0.01
                decimals: 2
                value: root.c.trimStart
                onEdited: function (v) { root.patch({ trimStart: v }) }
            }
            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Trim end")
                from: 0
                to: 1
                step: 0.01
                decimals: 2
                value: root.c.trimEnd
                onEdited: function (v) { root.patch({ trimEnd: v }) }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.painted
            spacing: Theme.spacingMd

            ThemedLabel {
                size: "xs"
                tone: "default"
                text: qsTr("Sketchy")
                font.weight: Font.DemiBold
            }
            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Length")
                from: 0
                to: 60
                step: 0.5
                decimals: 1
                unit: "px"
                value: root.c.sketchLength
                onEdited: function (v) { root.patch({ sketchLength: v }) }
            }
            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Deviation")
                from: 0
                to: 30
                step: 0.5
                decimals: 1
                unit: "px"
                value: root.c.sketchDeviation
                onEdited: function (v) { root.patch({ sketchDeviation: v }) }
            }
            StyleFieldRow {
                Layout.fillWidth: true
                label: qsTr("Seed")

                ThemedNumberField {
                    Layout.fillWidth: true
                    from: 0
                    to: 9999
                    step: 1
                    decimals: 0
                    value: root.c.sketchSeed
                    onEdited: function (v) { root.patch({ sketchSeed: Math.round(v) }) }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.kind === "shadow"
            spacing: Theme.spacingMd

            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Offset X")
                from: -500
                to: 500
                step: 1
                decimals: 0
                unit: "px"
                value: root.c.offsetX
                onEdited: function (v) { root.patch({ offsetX: v }) }
            }
            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Offset Y")
                from: -500
                to: 500
                step: 1
                decimals: 0
                unit: "px"
                value: root.c.offsetY
                onEdited: function (v) { root.patch({ offsetY: v }) }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.kind === "shadow" || root.kind === "glow"
            spacing: Theme.spacingMd

            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Blur")
                from: 0
                to: 200
                step: 0.5
                decimals: 1
                unit: "px"
                value: root.c.blur
                onEdited: function (v) { root.patch({ blur: v }) }
            }
            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Spread")
                from: 0
                to: 100
                step: 0.5
                decimals: 1
                unit: "px"
                value: root.c.spread
                onEdited: function (v) { root.patch({ spread: v }) }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.kind === "extrude"
            spacing: Theme.spacingMd

            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Depth")
                from: 0
                to: 100
                step: 0.5
                decimals: 1
                unit: "px"
                value: root.c.width
                onEdited: function (v) { root.patch({ width: v }) }
            }
            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Angle")
                from: -360
                to: 360
                step: 5
                decimals: 0
                unit: "°"
                value: root.c.extrudeAngle
                onEdited: function (v) { root.patch({ extrudeAngle: v }) }
            }
            StyleFieldRow {
                Layout.fillWidth: true
                label: qsTr("Steps")

                ThemedNumberField {
                    Layout.fillWidth: true
                    from: 1
                    to: 64
                    step: 1
                    decimals: 0
                    value: root.c.extrudeSteps
                    onEdited: function (v) { root.patch({ extrudeSteps: Math.round(v) }) }
                }
            }
            StyleSliderRow {
                Layout.fillWidth: true
                label: qsTr("Darken")
                from: 0
                to: 1
                step: 0.01
                decimals: 2
                value: root.c.extrudeDarken
                onEdited: function (v) { root.patch({ extrudeDarken: v }) }
            }
        }

        StyleFieldRow {
            Layout.fillWidth: true
            visible: root.scopeVisible
            label: qsTr("Applies to")

            ThemedComboBox {
                Layout.fillWidth: true
                model: root.scopes
                textRole: "label"
                valueRole: "value"
                currentIndex: indexOfValue(root.c.scope)
                onActivated: root.patch({ scope: currentValue })
            }
        }
    }
}
