import QtQuick
import QtQuick.Layouts
import Prism

// Timing of a Trigger node: fires on an interval, at a time of day, or only on a manual pulse.
InspectorForm {
    id: root

    StyleFieldRow {
        Layout.fillWidth: true
        label: qsTr("Mode")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Interval"), qsTr("At time of day"), qsTr("Manual pulse")]
            currentIndex: root.params.trigMode
            onActivated: index => root.set({ trigMode: index })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.params.trigMode === 0
        label: qsTr("Interval")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 20
            to: 3600000
            step: 100
            unit: "ms"
            value: root.params.trigInterval
            onEdited: v => root.set({ trigInterval: Math.round(v) })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.params.trigMode === 1
        label: qsTr("Time of day")
        ThemedTextField {
            Layout.fillWidth: true
            text: root.params.trigTime
            placeholderText: qsTr("HH:mm:ss")
            validator: RegularExpressionValidator {
                regularExpression: /^([01]\d|2[0-3]):[0-5]\d:[0-5]\d$/
            }
            onEditingFinished: if (acceptableInput && text !== root.params.trigTime) root.set({ trigTime: text })
        }
    }

    ThemedButton {
        Layout.fillWidth: true
        text: qsTr("Pulse now")
        onClicked: AppController.pulseTrigger(root.nodeId)
    }
}
