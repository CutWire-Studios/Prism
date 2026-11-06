import QtQuick
import QtQuick.Layouts
import Prism

PanelFrame {
    id: root

    readonly property bool locked: AppController.singleStream

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60
            color: "black"
            radius: Theme.radiusSm
            border.width: 1
            border.color: Theme.panelBorder
            clip: true

            PreviewItem {
                anchors.fill: parent
                anchors.margins: 1
                compositor: AppController.compositor
                source: PreviewItem.Program
            }

            ThemedLabel {
                x: Theme.spacingSm
                y: Theme.spacingSm
                tone: "default"
                font.family: Theme.monoFontFamily
                style: Text.Outline
                styleColor: "black"
                text: AppController.programWidth + "x" + AppController.programHeight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("A") }
            ThemedSlider {
                id: fader
                Layout.fillWidth: true
                enabled: !root.locked
                label: qsTr("Fader")
                from: 0
                to: 100
                stepSize: 1
                valueFormatter: v => Math.round(v) + "%"
                onMoved: AppController.faderValue = Math.round(value)
                Binding {
                    target: fader
                    property: "value"
                    value: AppController.faderValue
                    when: !fader.pressed
                }
            }
            ThemedLabel { text: qsTr("B") }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ThemedButton {
                text: qsTr("Cut")
                tooltip: qsTr("Instant cut to opposite deck (Space)")
                enabled: !root.locked
                onClicked: AppController.cut()
            }
            ThemedButton {
                text: qsTr("Auto")
                variant: "primary"
                tooltip: qsTr("Smooth auto transition using selected mode (Enter)")
                enabled: !root.locked && !AppController.transitionRunning
                onClicked: AppController.autoTransition()
            }
            ThemedComboBox {
                Layout.fillWidth: true
                enabled: !root.locked
                model: AppController.transitionModeNames
                currentIndex: AppController.transitionMode
                onActivated: index => AppController.transitionMode = index
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("Time") }
            ThemedSlider {
                id: durSlider
                Layout.fillWidth: true
                enabled: !root.locked
                label: qsTr("Transition duration")
                from: 0.05
                to: 5
                valueFormatter: v => Number(v).toFixed(2) + " s"
                onMoved: AppController.transitionDuration = value
                Binding {
                    target: durSlider
                    property: "value"
                    value: AppController.transitionDuration
                    when: !durSlider.pressed
                }
            }
            ThemedLabel {
                Layout.preferredWidth: 44
                font.family: Theme.monoFontFamily
                horizontalAlignment: Text.AlignRight
                text: AppController.transitionDuration.toFixed(2) + " s"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ThemedToggleButton {
                text: qsTr("Blackout")
                tooltip: qsTr("Cut program output to black (B)")
                checkable: false
                checked: AppController.panicMode === "blackout"
                onClicked: AppController.togglePanic("blackout")
            }
            ThemedToggleButton {
                text: qsTr("Freeze")
                tooltip: qsTr("Freeze the program output on the current frame (P)")
                checkable: false
                checked: AppController.panicMode === "freeze"
                onClicked: AppController.togglePanic("freeze")
            }
            ThemedToggleButton {
                text: qsTr("Stay Tuned")
                tooltip: qsTr("Show a \"Stay Tuned\" holding screen on program output (F9)")
                checkable: false
                checked: AppController.panicMode === "stay_tuned"
                onClicked: AppController.togglePanic("stay_tuned")
            }
        }
    }
}
