import QtQuick
import Prism

ThemedDialog {
    id: root

    title: qsTr("Add canvas")
    acceptText: qsTr("Add")
    preferredWidth: Theme.dialogWidthMd

    property color fillColor: "#ffffff"
    readonly property var presets: SourcePrompts.canvasPresets()
    readonly property bool custom: presetCombo.currentIndex >= presets.length

    onAccepted: {
        const w = custom ? widthField.value : presets[presetCombo.currentIndex].width
        const h = custom ? heightField.value : presets[presetCombo.currentIndex].height
        SourcePrompts.addCanvas(w, h, fillCombo.currentValue, fillColor)
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            text: qsTr("Aspect ratio")
        }

        ThemedComboBox {
            id: presetCombo
            width: parent.width
            model: root.presets.map(function (p) { return p.label }).concat([qsTr("Custom…")])
        }

        Row {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.custom

            ThemedNumberField {
                id: widthField
                width: (parent.width - parent.spacing) / 2
                from: 16
                to: 16384
                value: 1280
                text: "1280"
            }

            ThemedNumberField {
                id: heightField
                width: (parent.width - parent.spacing) / 2
                from: 16
                to: 16384
                value: 720
                text: "720"
            }
        }

        ThemedLabel {
            text: qsTr("Fill")
        }

        ThemedComboBox {
            id: fillCombo
            width: parent.width
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "checkered", label: qsTr("Checkered") },
                { id: "transparent", label: qsTr("Transparent") },
                { id: "color", label: qsTr("Color") }
            ]
        }

        ThemedButton {
            visible: fillCombo.currentValue === "color"
            text: qsTr("Pick color")
            onClicked: {
                colorDialog.selectedColor = root.fillColor
                colorDialog.open()
            }

            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 3
                color: root.fillColor
                border.width: 1
                border.color: Theme.border
            }
        }
    }

    ThemedColorDialog {
        id: colorDialog
        onAccepted: root.fillColor = selectedColor
    }
}
