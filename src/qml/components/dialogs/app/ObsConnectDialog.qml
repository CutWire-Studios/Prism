import QtQuick
import Prism

ThemedDialog {
    id: root

    title: qsTr("Connect to OBS")
    showAccept: false
    rejectText: qsTr("Close")
    preferredWidth: Theme.dialogWidthMd

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            width: parent.width
            visible: !ObsConnection.available
            color: Theme.destructive
            text: qsTr("OBS integration requires Qt WebSockets (install qt6-websockets and rebuild).")
        }

        ThemedLabel {
            text: qsTr("Host")
        }

        ThemedTextField {
            id: hostField
            width: parent.width
            text: ObsConnection.host
        }

        ThemedLabel {
            text: qsTr("Port")
        }

        ThemedNumberField {
            id: portField
            width: parent.width
            from: 1
            to: 65535
            value: ObsConnection.port
        }

        ThemedLabel {
            text: qsTr("Password")
        }

        ThemedTextField {
            id: passwordField
            width: parent.width
            echoMode: TextInput.Password
            placeholderText: qsTr("obs-websocket password (leave empty if disabled)")
            text: ObsConnection.password
        }

        ThemedLabel {
            width: parent.width
            tone: "default"
            text: ObsConnection.connected ? qsTr("Status: connected")
                  : ObsConnection.connecting ? qsTr("Status: connecting…")
                  : ObsConnection.error.length > 0 ? qsTr("Status: error, %1").arg(ObsConnection.error)
                  : qsTr("Status: disconnected")
        }

        Row {
            spacing: Theme.spacingMd

            ThemedButton {
                variant: "primary"
                text: qsTr("Connect")
                enabled: ObsConnection.available && !ObsConnection.connecting
                onClicked: ObsConnection.connectToObs(hostField.text, portField.value, passwordField.text)
            }

            ThemedButton {
                text: qsTr("Disconnect")
                enabled: ObsConnection.connected || ObsConnection.connecting
                onClicked: ObsConnection.disconnectFromObs()
            }
        }

        ThemedLabel {
            width: parent.width
            text: qsTr("Requires OBS Studio with obs-websocket enabled (Tools, WebSocket Server Settings).")
        }
    }
}
