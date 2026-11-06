import QtQuick
import QtQuick.Dialogs
import Prism

ThemedDialog {
    id: root

    title: qsTr("Add slideshow")
    acceptText: qsTr("Add")
    preferredWidth: Theme.dialogWidthMd

    onAccepted: SourcePrompts.addSlideshow(folderField.text, intervalField.value)

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            text: qsTr("Image folder")
        }

        Row {
            width: parent.width
            spacing: Theme.spacingMd

            ThemedTextField {
                id: folderField
                width: parent.width - browse.width - parent.spacing
                placeholderText: qsTr("Select a folder of images")
            }

            ThemedButton {
                id: browse
                text: qsTr("Browse…")
                onClicked: folderDialog.open()
            }
        }

        ThemedLabel {
            text: qsTr("Seconds per slide")
        }

        ThemedNumberField {
            id: intervalField
            width: parent.width
            from: 1
            to: 60
            value: 3
            text: "3"
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Select image folder for slideshow")
        onAccepted: folderField.text = AppController.localPath(selectedFolder)
    }
}
