import QtQuick
import Prism

ThemedDialog {
    id: root

    title: qsTr("Remote control server")
    showAccept: false
    rejectText: qsTr("Close")
    preferredWidth: Theme.dialogWidthMd

    onAboutToShow: RemoteServerSettings.refresh()

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            width: parent.width
            tone: "default"
            size: "sm"
            text: qsTr("Network binding")
        }

        ThemedCheckBox {
            text: qsTr("Localhost only (127.0.0.1). Leave off to allow access from a phone on your network.")
            enabled: !RemoteServerSettings.running
            checked: RemoteServerSettings.localhostOnly
            onToggled: RemoteServerSettings.localhostOnly = checked
        }

        ThemedLabel {
            width: parent.width
            text: qsTr("Server port: %1").arg(RemoteServerSettings.port)
        }

        Row {
            width: parent.width
            spacing: Theme.spacingLg
            visible: RemoteServerSettings.running && !RemoteServerSettings.localhostOnly

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("QR network")
            }

            ThemedComboBox {
                width: parent.width - 100
                enabled: RemoteServerSettings.interfaces.length > 1
                model: RemoteServerSettings.interfaces
                currentIndex: RemoteServerSettings.interfaceIndex
                onActivated: RemoteServerSettings.interfaceIndex = currentIndex
            }
        }

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: RemoteServerSettings.qrImage.length > 0
            source: RemoteServerSettings.qrImage
            width: 200
            height: 200
            fillMode: Image.PreserveAspectFit
            smooth: false
        }

        ThemedLabel {
            width: parent.width
            visible: RemoteServerSettings.running
            horizontalAlignment: Text.AlignHCenter
            tone: "default"
            text: RemoteServerSettings.qrUrl.length > 0
                  ? RemoteServerSettings.qrUrl
                  : qsTr("No network interface available for QR code.")
        }

        ThemedLabel {
            width: parent.width
            visible: RemoteServerSettings.running
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Scan with your phone camera to open the remote console.")
        }

        ThemedLabel {
            width: parent.width
            tone: "default"
            text: RemoteServerSettings.running
                  ? qsTr("Server is running. Reachable at:\n%1").arg(RemoteServerSettings.accessUrls.join("\n"))
                  : qsTr("Server is stopped.")
        }

        ThemedLabel {
            width: parent.width
            visible: RemoteServerSettings.error.length > 0
            color: Theme.destructive
            text: RemoteServerSettings.error
        }

        ThemedButton {
            variant: RemoteServerSettings.running ? "destructive" : "primary"
            text: RemoteServerSettings.running ? qsTr("Stop server") : qsTr("Start server")
            onClicked: RemoteServerSettings.toggle()
        }
    }
}
