import QtQuick
import QtQuick.Layouts
import Prism

// Canvas size of a Layer node.
ThemedDialog {
    id: root

    property var nodeId: 0
    property int canvasW: 1280
    property int canvasH: 720
    property bool canvasCustom: false

    readonly property var presets: [
        { label: qsTr("Custom"), w: 0, h: 0 },
        { label: qsTr("16:9 (1280×720)"), w: 1280, h: 720 },
        { label: qsTr("16:9 (1920×1080)"), w: 1920, h: 1080 },
        { label: qsTr("4:3 (1024×768)"), w: 1024, h: 768 }
    ]

    title: qsTr("Layer Canvas")
    preferredWidth: Theme.dialogWidthSm

    function openFor(id, kind) {
        const size = TransformTools.canvasSize(id)
        nodeId = id
        canvasW = size.width
        canvasH = size.height
        canvasCustom = TransformTools.canvasCustom(id)
        open()
    }

    onAccepted: AppController.graph.setParams(nodeId, canvasCustom
                                              ? { canvasCustom: true, canvasW: canvasW, canvasH: canvasH }
                                              : { canvasCustom: false })

    contentItem: GridLayout {
        columns: 2
        columnSpacing: Theme.spacingLg
        rowSpacing: Theme.spacingLg

        ThemedLabel { text: qsTr("Custom size") }
        ThemedSwitch {
            checked: root.canvasCustom
            onToggled: {
                root.canvasCustom = checked
                if (!checked) {
                    root.canvasW = AppController.programWidth
                    root.canvasH = AppController.programHeight
                }
            }
        }

        ThemedLabel { text: qsTr("Preset") }
        ThemedComboBox {
            Layout.fillWidth: true
            enabled: root.canvasCustom
            textRole: "label"
            model: root.presets
            currentIndex: {
                for (let i = 1; i < root.presets.length; ++i)
                    if (root.presets[i].w === root.canvasW && root.presets[i].h === root.canvasH)
                        return i
                return 0
            }
            onActivated: index => {
                const p = root.presets[index]
                if (p.w > 0) {
                    root.canvasW = p.w
                    root.canvasH = p.h
                }
            }
        }

        ThemedLabel { text: qsTr("Width") }
        ThemedNumberField {
            Layout.fillWidth: true
            enabled: root.canvasCustom
            from: 1
            to: 7680
            unit: "px"
            value: root.canvasW
            onEdited: v => root.canvasW = Math.round(v)
        }

        ThemedLabel { text: qsTr("Height") }
        ThemedNumberField {
            Layout.fillWidth: true
            enabled: root.canvasCustom
            from: 1
            to: 4320
            unit: "px"
            value: root.canvasH
            onEdited: v => root.canvasH = Math.round(v)
        }
    }
}
