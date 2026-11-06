pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// FFT settings of an Audio Script node.
ThemedDialog {
    id: root

    title: qsTr("Audio FFT Settings")
    preferredWidth: Theme.dialogWidthMd

    property var nodeId: 0
    property int fftSize: 1024
    property int binCount: 64
    property real smoothing: 0.5
    property real beatSensitivity: 1
    property real lowMidHz: 250
    property real midHighHz: 4000

    readonly property var fftSizes: [256, 512, 1024, 2048, 4096, 8192]

    function openFor(id, kind) {
        nodeId = id
        const p = graphParams.params(id)
        fftSize = p.fftSize
        binCount = p.binCount
        smoothing = p.smoothing
        beatSensitivity = p.beatSensitivity
        lowMidHz = p.lowMidHz
        midHighHz = p.midHighHz
        fftCombo.currentIndex = Math.max(0, fftSizes.indexOf(fftSize))
        open()
    }

    onAccepted: {
        AppController.graph.setParams(root.nodeId, {
            fftSize: root.fftSizes[fftCombo.currentIndex],
            binCount: root.binCount,
            smoothing: root.smoothing,
            beatSensitivity: root.beatSensitivity,
            lowMidHz: root.lowMidHz,
            midHighHz: root.midHighHz
        })
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
                Layout.preferredWidth: 130
                text: qsTr("FFT size:")
                tone: "default"
                size: "sm"
            }

            ThemedComboBox {
                id: fftCombo
                Layout.fillWidth: true
                model: root.fftSizes
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.preferredWidth: 130
                text: qsTr("Spectrum bins:")
                tone: "default"
                size: "sm"
            }

            ThemedNumberField {
                Layout.fillWidth: true
                value: root.binCount
                from: 8
                to: 256
                step: 1
                onEdited: v => root.binCount = v
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.preferredWidth: 130
                text: qsTr("Smoothing:")
                tone: "default"
                size: "sm"
            }

            ThemedNumberField {
                Layout.fillWidth: true
                value: root.smoothing
                from: 0
                to: 0.98
                step: 0.05
                decimals: 2
                onEdited: v => root.smoothing = v
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.preferredWidth: 130
                text: qsTr("Beat sensitivity:")
                tone: "default"
                size: "sm"
            }

            ThemedNumberField {
                Layout.fillWidth: true
                value: root.beatSensitivity
                from: 0.1
                to: 5
                step: 0.1
                decimals: 1
                onEdited: v => root.beatSensitivity = v
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.preferredWidth: 130
                text: qsTr("Low/Mid split:")
                tone: "default"
                size: "sm"
            }

            ThemedNumberField {
                Layout.fillWidth: true
                value: root.lowMidHz
                from: 20
                to: 20000
                step: 10
                unit: "Hz"
                onEdited: v => root.lowMidHz = v
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            ThemedLabel {
                Layout.preferredWidth: 130
                text: qsTr("Mid/High split:")
                tone: "default"
                size: "sm"
            }

            ThemedNumberField {
                Layout.fillWidth: true
                value: root.midHighHz
                from: 20
                to: 20000
                step: 10
                unit: "Hz"
                onEdited: v => root.midHighHz = v
            }
        }
    }
}
