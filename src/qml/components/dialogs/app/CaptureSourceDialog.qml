import QtQuick
import Prism

ThemedDialog {
    id: root

    property string preferredKind: "screen"
    property var screens: []
    property var windows: []

    title: qsTr("Select capture source")
    acceptText: qsTr("Add")
    preferredWidth: Theme.dialogWidthMd

    function show(kind) {
        preferredKind = kind
        open()
    }

    onAboutToShow: {
        screens = SourcePrompts.screenLabels()
        windows = SourcePrompts.windowLabels()
        kindCombo.currentIndex = (preferredKind === "window" && windows.length > 0) ? 1 : 0
    }

    onAccepted: {
        if (kindCombo.currentIndex === 0)
            SourcePrompts.addScreen(screenCombo.currentIndex)
        else
            SourcePrompts.addWindow(windowCombo.currentIndex)
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            text: qsTr("What do you want to capture?")
        }

        ThemedComboBox {
            id: kindCombo
            width: parent.width
            model: [qsTr("Entire screen"), qsTr("Application window")]
        }

        ThemedComboBox {
            id: screenCombo
            width: parent.width
            visible: kindCombo.currentIndex === 0
            model: root.screens
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: kindCombo.currentIndex === 1

            ThemedComboBox {
                id: windowCombo
                width: parent.width
                model: root.windows
            }

            ThemedButton {
                text: qsTr("Refresh window list")
                onClicked: root.windows = SourcePrompts.windowLabels()
            }
        }

        ThemedLabel {
            width: parent.width
            visible: root.screens.length === 0 && root.windows.length === 0
            text: qsTr("No screens or capturable windows were found.")
        }
    }
}
