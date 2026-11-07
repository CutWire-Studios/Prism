import QtQuick
import QtQuick.Layouts
import Prism

// Run-on settings, last error and print log of a Script node. The code itself is edited in the
// script editor ("Open editor").
InspectorForm {
    id: root

    property string error: ""
    property var logLines: []

    function refresh() {
        error = AppController.scriptError(nodeId)
        logLines = AppController.scriptLog(nodeId)
    }

    Component.onCompleted: refresh()
    onNodeIdChanged: refresh()

    Connections {
        target: AppController
        function onScriptStatusChanged(id) {
            if (id === 0 || id == root.nodeId)
                root.refresh()
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        label: qsTr("Run on")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Input change"), qsTr("Interval"), qsTr("Time of day"), qsTr("Manual")]
            currentIndex: root.params.runOn
            onActivated: index => root.set({ runOn: index })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.params.runOn === 1
        label: qsTr("Interval")
        ThemedNumberField {
            Layout.fillWidth: true
            from: 20
            to: 3600000
            step: 100
            unit: "ms"
            value: root.params.intervalMs
            onEdited: v => root.set({ intervalMs: Math.round(v) })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.params.runOn === 2
        label: qsTr("Time of day")
        ThemedTextField {
            Layout.fillWidth: true
            text: root.params.runTime
            placeholderText: qsTr("HH:mm:ss")
            validator: RegularExpressionValidator {
                regularExpression: /^([01]\d|2[0-3]):[0-5]\d:[0-5]\d$/
            }
            onEditingFinished: if (acceptableInput && text !== root.params.runTime) root.set({ runTime: text })
        }
    }

    ThemedButton {
        Layout.fillWidth: true
        text: qsTr("Run now")
        onClicked: AppController.runScript(root.nodeId)
    }

    ThemedLabel {
        Layout.fillWidth: true
        visible: root.error.length > 0
        color: Theme.destructive
        wrapMode: Text.Wrap
        text: root.error
    }

    ThemedLabel {
        visible: root.logLines.length > 0
        text: qsTr("Log")
    }

    ThemedTextArea {
        Layout.fillWidth: true
        Layout.preferredHeight: 110
        visible: root.logLines.length > 0
        readOnly: true
        font.family: Theme.monoFontFamily
        text: root.logLines.join("\n")
        onTextChanged: cursorPosition = length
    }
}
