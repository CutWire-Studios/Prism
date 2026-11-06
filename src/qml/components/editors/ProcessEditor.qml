pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

// Canvas-overlay editors of a Process node (Crop, Keystone, Poly Mask); the other effects are
// edited in the inspector. Edits stay in `working` and are written to the node, as one undo
// step, when the dialog is accepted.
ThemedDialog {
    id: root

    property var nodeId: 0
    property int effect: -1
    property var working: ({})
    property var reference: 0

    readonly property string effectName: {
        const list = NodeCatalog.processEffects()
        for (const e of list)
            if (e.id === effect)
                return e.name
        return ""
    }

    title: qsTr("Edit %1").arg(effectName)
    preferredWidth: Theme.dialogWidthLg
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
        case 10: return keystonePane
        case 11: return maskPane
        }
        return null
    }

    onAccepted: AppController.graph.setParams(nodeId, { params: working })

    contentItem: Loader {
        sourceComponent: root.paneFor(root.effect)
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
