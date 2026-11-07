pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

PanelFrame {
    id: root

    EmptyState {
        anchors.centerIn: parent
        visible: list.count === 0
        compact: true
        glyph: "sliders-horizontal"
        title: qsTr("No globals")
        hint: qsTr("Globals are project-wide numbers that any expression reads as globals.name")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSm
            boundsBehavior: Flickable.StopAtBounds
            model: AppController.globals
            ScrollBar.vertical: AppScrollBar {}

            delegate: RowLayout {
                id: del

                required property string name
                required property real value
                required property real min
                required property real max

                width: ListView.view.width
                spacing: Theme.spacingSm

                ThemedTextField {
                    id: nameField
                    Layout.preferredWidth: 120
                    text: del.name
                    onEditingFinished: {
                        if (text !== del.name && !AppController.renameGlobal(del.name, text))
                            text = del.name
                    }
                }
                ThemedSlider {
                    id: slider
                    Layout.fillWidth: true
                    label: del.name
                    from: del.min
                    to: del.max
                    onMoved: AppController.setGlobalValue(del.name, value)
                    valueFormatter: v => Number(v).toFixed(3)

                    Binding {
                        target: slider
                        property: "value"
                        value: del.value
                    }
                }
                ThemedNumberField {
                    Layout.preferredWidth: 72
                    decimals: 3
                    step: (del.max - del.min) / 100
                    from: del.min
                    to: del.max
                    value: del.value
                    onEdited: v => AppController.setGlobalValue(del.name, v)
                }
                ThemedNumberField {
                    Layout.preferredWidth: 64
                    decimals: 3
                    step: 0.1
                    value: del.min
                    onEdited: v => {
                        if (!AppController.setGlobalRange(del.name, v, del.max))
                            value = del.min
                    }
                }
                ThemedNumberField {
                    Layout.preferredWidth: 64
                    decimals: 3
                    step: 0.1
                    value: del.max
                    onEdited: v => {
                        if (!AppController.setGlobalRange(del.name, del.min, v))
                            value = del.max
                    }
                }
                IconButton {
                    glyph: "trash-2"
                    tooltip: qsTr("Delete global")
                    onClicked: AppController.removeGlobal(del.name)
                }
            }
        }

        ThemedButton {
            text: qsTr("Add global")
            glyph: "plus"
            onClicked: AppController.addGlobal("")
        }
    }
}
