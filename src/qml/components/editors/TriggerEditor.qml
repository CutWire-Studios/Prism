pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// Timing of a Trigger node: fires on an interval, at a time of day, or only on a manual pulse.
// Uses its own footer so OK stays disabled while the time of day is not a valid HH:mm:ss.
ThemedDialog {
    id: root

    title: qsTr("Trigger Timing")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    acceptOnReturn: false

    property var nodeId: 0

    readonly property bool canSave: modeCombo.currentIndex !== 1 || timeField.acceptableInput

    function openFor(id, kind) {
        nodeId = id
        const p = graphParams.params(id)
        modeCombo.currentIndex = p.trigMode
        intervalField.value = p.trigInterval
        timeField.text = p.trigTime
        open()
    }

    function save() {
        AppController.graph.setParams(root.nodeId, {
            trigMode: modeCombo.currentIndex,
            trigInterval: intervalField.value,
            trigTime: timeField.text
        })
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.visible && saveButton.enabled
        onActivated: saveButton.clicked()
    }

    AudioGraphParams {
        id: graphParams
        controller: AppController.graph
    }

    contentItem: ColumnLayout {
        width: parent ? parent.width : root.preferredWidth
        spacing: Theme.spacingLg

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.preferredWidth: 110
                text: qsTr("Mode:")
                tone: "default"
                size: "sm"
            }

            ThemedComboBox {
                id: modeCombo
                Layout.fillWidth: true
                model: [qsTr("Interval"), qsTr("At time of day"), qsTr("Manual pulse")]
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.preferredWidth: 110
                text: qsTr("Interval:")
                tone: "default"
                size: "sm"
            }

            ThemedNumberField {
                id: intervalField
                Layout.fillWidth: true
                enabled: modeCombo.currentIndex === 0
                from: 20
                to: 3600000
                step: 100
                unit: "ms"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.preferredWidth: 110
                text: qsTr("Time of day:")
                tone: "default"
                size: "sm"
            }

            ThemedTextField {
                id: timeField
                Layout.fillWidth: true
                enabled: modeCombo.currentIndex === 1
                placeholderText: qsTr("HH:mm:ss")
                validator: RegularExpressionValidator {
                    regularExpression: /^([01]\d|2[0-3]):[0-5]\d:[0-5]\d$/
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingLg
            spacing: Theme.spacingLg

            Item {
                Layout.fillWidth: true
            }

            ThemedButton {
                text: qsTr("Cancel")
                variant: "secondary"
                onClicked: root.close()
            }

            ThemedButton {
                id: saveButton
                text: qsTr("OK")
                variant: "primary"
                enabled: root.canSave
                onClicked: {
                    root.save()
                    root.close()
                }
            }
        }
    }
}
