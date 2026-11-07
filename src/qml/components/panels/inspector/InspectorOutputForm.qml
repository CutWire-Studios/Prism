pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

// Audio Output: one row per logical port. The device chosen for a port is stored in this
// venue's device map, so the same show plays on whatever hardware the venue has.
InspectorForm {
    id: root

    readonly property var ports: params.devicePorts ? params.devicePorts : []
    property int revision: 0
    readonly property var devices: catalog.playbackDevices()

    function deviceIndex(logical) {
        void revision
        const id = AppController.audioDeviceFor(logical)
        return Math.max(0, devices.findIndex(d => d.id === id))
    }

    function addPort() {
        const names = ports.map(p => p.deviceId)
        let n = ports.length + 1
        while (names.indexOf(qsTr("Output %1").arg(n)) >= 0)
            n++
        const name = qsTr("Output %1").arg(n)
        set({ devicePorts: ports.concat([{ deviceId: name, deviceLabel: name }]) })
    }

    function removePort(i) {
        AppController.clearAudioDevice(ports[i].deviceId)
        set({ devicePorts: ports.filter((p, k) => k !== i) })
    }

    AudioDeviceCatalog {
        id: catalog
    }

    Connections {
        target: AppController
        function onAudioDevicesChanged() { root.revision++ }
    }

    Repeater {
        model: root.ports

        delegate: StyleFieldRow {
            id: portRow

            required property var modelData
            required property int index

            Layout.fillWidth: true
            label: portRow.modelData.deviceLabel

            ThemedComboBox {
                Layout.fillWidth: true
                textRole: "label"
                model: root.devices
                currentIndex: root.deviceIndex(portRow.modelData.deviceId)
                onActivated: i => AppController.setAudioDevice(portRow.modelData.deviceId, root.devices[i].id)
            }
            IconButton {
                glyph: "trash-2"
                tooltip: qsTr("Remove output")
                enabled: root.ports.length > 1
                onClicked: root.removePort(portRow.index)
            }
        }
    }

    ThemedButton {
        Layout.fillWidth: true
        text: qsTr("Add output")
        onClicked: root.addPort()
    }
}
