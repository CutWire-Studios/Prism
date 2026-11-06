pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

// Parameters of a Process node. Edits stay in `working` and are written to the node, as one
// undo step, when the dialog is accepted.
ThemedDialog {
    id: root

    property var nodeId: 0
    property int effect: -1
    property var working: ({})
    property var reference: 0

    readonly property bool wide: effect === 0 || effect === 10 || effect === 11
    readonly property string effectName: {
        const list = NodeCatalog.processEffects()
        for (const e of list)
            if (e.id === effect)
                return e.name
        return ""
    }

    title: qsTr("Edit %1").arg(effectName)
    preferredWidth: wide ? Theme.dialogWidthLg : Theme.dialogWidthMd
    acceptText: qsTr("OK")

    function openFor(id, kind) {
        const p = ClipEditTools.nodeParams(id)
        nodeId = id
        effect = p.effect !== undefined ? p.effect : -1
        working = Object.assign({}, p.params || {})
        reference = ClipEditTools.referenceInput(id)
        open()
    }

    function num(key, fallback) {
        const v = working[key]
        return v === undefined ? fallback : v
    }

    function setValues(values) {
        working = Object.assign({}, working, values)
    }

    function paneFor(id) {
        switch (id) {
        case 0: return cropPane
        case 4: return flipPane
        case 5: return rotatePane
        case 6: return opacityPane
        case 7: return chromaPane
        case 8: return blurPane
        case 9: return sharpenPane
        case 10: return keystonePane
        case 11: return maskPane
        }
        return null
    }

    onAccepted: AppController.graph.setParams(nodeId, { params: working })

    component SliderRow: RowLayout {
        id: row
        property string label
        property real from: 0
        property real to: 1
        property real stepSize: 0.01
        property real value: 0
        property int decimals: 2
        property string suffix: ""
        signal moved(real v)

        Layout.fillWidth: true
        spacing: Theme.spacingLg

        ThemedLabel {
            Layout.preferredWidth: 96
            text: row.label
        }
        ThemedSlider {
            Layout.fillWidth: true
            label: row.label
            from: row.from
            to: row.to
            stepSize: row.stepSize
            value: row.value
            valueFormatter: v => Number(v).toFixed(row.decimals) + row.suffix
            onMoved: row.moved(value)
        }
        ThemedLabel {
            Layout.preferredWidth: 64
            horizontalAlignment: Text.AlignRight
            tone: "default"
            text: Number(row.value).toFixed(row.decimals) + row.suffix
        }
    }

    contentItem: Loader {
        sourceComponent: root.paneFor(root.effect)
    }

    ThemedColorDialog {
        id: keyDialog
        onAccepted: root.setValues({
            r: Math.round(selectedColor.r * 255),
            g: Math.round(selectedColor.g * 255),
            b: Math.round(selectedColor.b * 255)
        })
    }

    Component {
        id: cropPane
        ColumnLayout {
            spacing: Theme.spacingLg

            ReferenceFrame {
                id: frame
                Layout.fillWidth: true
                Layout.preferredHeight: 380
                nodeId: root.reference

                CropOverlay {
                    anchors.fill: parent
                    frame: frame.frameRect
                    cx: root.num("x", 0)
                    cy: root.num("y", 0)
                    cw: root.num("w", 1)
                    ch: root.num("h", 1)
                    onCropEdited: (x, y, w, h) => root.setValues({ x: x, y: y, w: w, h: h })
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ThemedLabel {
                    Layout.fillWidth: true
                    text: qsTr("Drag inside to move, drag corners to resize, double-click to reset.")
                }
                ThemedButton {
                    text: qsTr("Reset")
                    onClicked: root.setValues({ x: 0, y: 0, w: 1, h: 1 })
                }
            }
        }
    }

    Component {
        id: flipPane
        ColumnLayout {
            spacing: Theme.spacingLg
            ThemedLabel { text: qsTr("Direction") }
            ThemedComboBox {
                Layout.fillWidth: true
                model: [qsTr("Horizontal"), qsTr("Vertical"), qsTr("Both")]
                currentIndex: Math.max(0, Math.min(2, root.num("direction", 1)))
                onActivated: index => root.setValues({ direction: index })
            }
        }
    }

    Component {
        id: rotatePane
        ColumnLayout {
            spacing: Theme.spacingLg
            ThemedLabel { text: qsTr("Angle") }
            ThemedComboBox {
                Layout.fillWidth: true
                model: ["0°", "90°", "180°", "270°"]
                currentIndex: Math.max(0, Math.min(3, Math.round(root.num("angle", 90) / 90)))
                onActivated: index => root.setValues({ angle: index * 90 })
            }
        }
    }

    Component {
        id: opacityPane
        ColumnLayout {
            spacing: Theme.spacingLg
            SliderRow {
                label: qsTr("Opacity")
                from: 0
                to: 100
                stepSize: 1
                decimals: 0
                suffix: "%"
                value: root.num("opacity", 1) * 100
                onMoved: v => root.setValues({ opacity: v / 100 })
            }
        }
    }

    Component {
        id: chromaPane
        ColumnLayout {
            spacing: Theme.spacingLg

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLg
                ThemedLabel {
                    Layout.preferredWidth: 96
                    text: qsTr("Key colour")
                }
                ThemedButton {
                    readonly property color keyColor: Qt.rgba(root.num("r", 0) / 255, root.num("g", 255) / 255,
                                                              root.num("b", 0) / 255, 1)
                    text: String(keyColor).toUpperCase()
                    onClicked: {
                        keyDialog.selectedColor = keyColor
                        keyDialog.open()
                    }
                }
            }
            SliderRow {
                label: qsTr("Threshold")
                from: 0
                to: 100
                stepSize: 1
                decimals: 0
                value: root.num("threshold", 0.25) * 100
                onMoved: v => root.setValues({ threshold: v / 100 })
            }
            SliderRow {
                label: qsTr("Smoothness")
                from: 0
                to: 100
                stepSize: 1
                decimals: 0
                value: root.num("smoothness", 0.1) * 100
                onMoved: v => root.setValues({ smoothness: v / 100 })
            }
        }
    }

    Component {
        id: blurPane
        ColumnLayout {
            spacing: Theme.spacingLg
            SliderRow {
                label: qsTr("Radius")
                from: 0
                to: 64
                stepSize: 1
                decimals: 0
                suffix: " px"
                value: root.num("radius", 6)
                onMoved: v => root.setValues({ radius: Math.round(v) })
            }
        }
    }

    Component {
        id: sharpenPane
        ColumnLayout {
            spacing: Theme.spacingLg
            SliderRow {
                label: qsTr("Amount")
                from: 0
                to: 3
                stepSize: 0.01
                decimals: 2
                value: root.num("amount", 1)
                onMoved: v => root.setValues({ amount: Math.round(v * 100) / 100 })
            }
        }
    }

    Component {
        id: keystonePane
        ColumnLayout {
            spacing: Theme.spacingLg

            ReferenceFrame {
                id: frame
                Layout.fillWidth: true
                Layout.preferredHeight: 420
                nodeId: root.reference
                overscan: 60
                warp: TransformTools.quadMatrix(frameRect.width, frameRect.height,
                                                root.num("corners", [[0, 0], [1, 0], [1, 1], [0, 1]]))

                QuadOverlay {
                    anchors.fill: parent
                    frame: frame.frameRect
                    corners: root.num("corners", [[0, 0], [1, 0], [1, 1], [0, 1]])
                    onCornersEdited: c => root.setValues({ corners: c })
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ThemedLabel {
                    Layout.fillWidth: true
                    text: qsTr("Drag the corners; the preview shows the warp.")
                }
                ThemedButton {
                    text: qsTr("Reset corners")
                    onClicked: root.setValues({ corners: [[0, 0], [1, 0], [1, 1], [0, 1]] })
                }
            }
        }
    }

    Component {
        id: maskPane
        ColumnLayout {
            spacing: Theme.spacingLg

            ReferenceFrame {
                id: frame
                Layout.fillWidth: true
                Layout.preferredHeight: 380
                nodeId: root.reference

                PolygonOverlay {
                    anchors.fill: parent
                    frame: frame.frameRect
                    points: root.num("points", [])
                    onPointsEdited: p => root.setValues({ points: p })
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ThemedCheckBox {
                    Layout.fillWidth: true
                    text: qsTr("Mask outside (invert)")
                    checked: root.num("invert", false)
                    onToggled: root.setValues({ invert: checked })
                }
                ThemedButton {
                    text: qsTr("Clear points")
                    onClicked: root.setValues({ points: [] })
                }
            }
        }
    }
}
