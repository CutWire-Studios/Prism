import QtQuick
import QtQuick.Controls.Basic
import Prism

// QML overrides run with the app's permissions. JSON colors do not, so the
// theme can still be selected with the overrides left unloaded.
ThemedDialog {
    id: root

    title: qsTr("Trust theme overrides?")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    acceptOnReturn: false
    closePolicy: Popup.CloseOnEscape

    property string themeId: ""
    property var slots: []

    signal trustChosen()
    signal skipChosen()

    function prompt(id) {
        themeId = id
        slots = ThemeManager.overrideSlots(id)
        open()
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            size: "sm"
            color: Theme.c.themeTrustDialog.foreground
            text: qsTr("“%1” includes QML files that replace controls. Those files run with Prism’s permissions. Trust a theme only after you have read it.")
                  .arg(root.themeId)
        }

        ThemedLabel {
            width: parent.width
            visible: root.slots.length === 0
            wrapMode: Text.WordWrap
            color: Theme.c.themeTrustDialog.foregroundMuted
            text: qsTr("No override files were found.")
        }

        Repeater {
            model: root.slots
            delegate: ThemedLabel {
                required property var modelData
                width: parent.width
                text: modelData
                color: Theme.c.themeTrustDialog.foregroundMuted
            }
        }

        Flow {
            width: parent.width
            spacing: Theme.spacingMd

            ThemedButton {
                text: qsTr("Trust and use overrides")
                variant: "primary"
                onClicked: {
                    root.trustChosen()
                    root.close()
                }
            }

            ThemedButton {
                text: qsTr("Use without overrides")
                variant: "secondary"
                onClicked: {
                    root.skipChosen()
                    root.close()
                }
            }

            ThemedButton {
                text: qsTr("Cancel")
                variant: "secondary"
                onClicked: root.reject()
            }
        }
    }
}
