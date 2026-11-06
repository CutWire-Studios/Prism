import QtQuick
import Prism

ThemedDialog {
    id: root

    title: qsTr("Add camera")
    acceptText: qsTr("Add")
    preferredWidth: Theme.dialogWidthMd

    property var labels: []

    onAboutToShow: labels = SourcePrompts.cameraLabels()
    onAccepted: SourcePrompts.addCamera(deviceCombo.currentIndex)

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            text: qsTr("Camera device")
        }

        ThemedComboBox {
            id: deviceCombo
            width: parent.width
            model: root.labels
        }

        ThemedLabel {
            width: parent.width
            visible: root.labels.length === 0
            text: qsTr("No camera devices were found.")
        }
    }
}
