pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// Device picker for Mic Input nodes (kind "mic") and Audio Capture nodes (kind "capture").
ThemedDialog {
    id: root

    title: root.isCapture ? qsTr("Audio Capture") : qsTr("Mic Input")
    preferredWidth: Theme.dialogWidthMd

    property var nodeId: 0
    property string kind: "mic"
    property var devices: []

    readonly property bool isCapture: kind === "capture"
    readonly property var deviceLabels: devices.map(d => d.label)
    readonly property var selected: devices.length > 0
                                    ? devices[Math.max(0, deviceCombo.currentIndex)]
                                    : ({ id: "", label: "" })

    function openFor(id, kind) {
        nodeId = id
        root.kind = kind
        devices = kind === "capture" ? catalog.playbackDevices() : catalog.inputDevices()
        const current = graphParams.params(id).deviceId || ""
        const idx = devices.findIndex(d => d.id === current)
        deviceCombo.currentIndex = idx < 0 ? 0 : idx
        open()
    }

    onAccepted: AppController.graph.setParams(root.nodeId, {
        deviceId: root.selected.id,
        deviceLabel: root.selected.label
    })

    AudioDeviceCatalog {
        id: catalog
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
                text: root.isCapture ? qsTr("Playback Device:") : qsTr("Input Device:")
                tone: "default"
                size: "sm"
            }

            ThemedComboBox {
                id: deviceCombo
                Layout.fillWidth: true
                model: root.deviceLabels
            }
        }

        ThemedLabel {
            Layout.fillWidth: true
            visible: root.isCapture
            text: catalog.captureHint(root.selected.id)
        }
    }
}
