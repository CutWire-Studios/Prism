import QtQuick
import QtQuick.Layouts
import Prism

// Asks for one name: "Save style…" and "Rename…" on the style pack gallery.
ThemedDialog {
    id: root

    property string requestedTitle: ""

    signal nameChosen(string name)

    title: requestedTitle
    preferredWidth: Theme.dialogWidthSm
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false

    function openWith(titleText, text) {
        requestedTitle = titleText
        field.text = text
        open()
        field.forceActiveFocus()
        field.selectAll()
    }

    function submit() {
        const name = field.text.trim()
        if (name.length === 0)
            return
        root.close()
        root.nameChosen(name)
    }

    contentItem: ColumnLayout {
        width: parent ? parent.width : Theme.dialogWidthSm
        spacing: Theme.spacingLg

        ThemedTextField {
            id: field
            Layout.fillWidth: true
            placeholderText: qsTr("Name")
            onAccepted: root.submit()
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacingLg

            ThemedButton {
                text: qsTr("Cancel")
                variant: "secondary"
                onClicked: root.close()
            }
            ThemedButton {
                text: qsTr("OK")
                variant: "primary"
                enabled: field.text.trim().length > 0
                onClicked: root.submit()
            }
        }
    }
}
