import QtQuick
import Prism

ThemedDialog {
    id: root

    title: qsTr("OSC server")
    acceptText: qsTr("Apply")
    preferredWidth: Theme.dialogWidthMd

    function sync() {
        enabledBox.checked = OscSettings.enabled
        portField.value = OscSettings.port
        portField.text = portField.format(OscSettings.port)
        hostField.text = OscSettings.feedbackHost
        feedbackPortField.value = OscSettings.feedbackPort
        feedbackPortField.text = feedbackPortField.format(OscSettings.feedbackPort)
    }

    onAboutToShow: {
        OscSettings.load()
        sync()
    }
    onAccepted: OscSettings.apply()

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedCheckBox {
            id: enabledBox
            text: qsTr("Listen for OSC messages")
            onToggled: OscSettings.enabled = checked
        }

        ThemedLabel {
            text: qsTr("UDP port")
        }

        ThemedNumberField {
            id: portField
            width: parent.width
            from: 1
            to: 65535
            onEdited: (v) => OscSettings.port = v
        }

        ThemedLabel {
            text: qsTr("Feedback host")
        }

        ThemedTextField {
            id: hostField
            width: parent.width
            placeholderText: qsTr("Disabled")
            onTextEdited: OscSettings.feedbackHost = text
        }

        ThemedLabel {
            text: qsTr("Feedback port")
        }

        ThemedNumberField {
            id: feedbackPortField
            width: parent.width
            from: 1
            to: 65535
            onEdited: (v) => OscSettings.feedbackPort = v
        }

        ThemedLabel {
            width: parent.width
            text: qsTr("Starts automatically with Prism while enabled. When a feedback host is set, state changes are sent there as OSC messages.")
        }
    }
}
