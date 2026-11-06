import QtQuick
import Prism

ThemedDialog {
    id: root

    title: qsTr("Add video URL")
    acceptText: qsTr("Add")
    preferredWidth: Theme.dialogWidthMd

    onAboutToShow: urlField.text = ""
    onOpened: urlField.forceActiveFocus()
    onAccepted: {
        const url = urlField.text.trim()
        if (url.length > 0)
            AppController.addVideoUrl(url)
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            text: qsTr("Stream or video URL")
        }

        ThemedTextField {
            id: urlField
            width: parent.width
            placeholderText: qsTr("https://…")
            onAccepted: root.accept()
        }
    }
}
