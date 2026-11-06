import QtQuick
import Prism

// Lists the app's actions with their shortcuts. No action registry is ported yet, so the list is
// empty until one is wired in as `actions`.
SettingsPage {
    id: page

    pageId: "keymap"
    title: qsTr("Keyboard shortcuts")

    property var actions: []

    Text {
        width: parent.width
        visible: page.query.length === 0
        wrapMode: Text.WordWrap
        text: qsTr("Click a shortcut, then press the keys. Esc cancels, Backspace clears.")
        color: Theme.c.settingsRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Repeater {
        model: page.actions

        SettingsRow {
            required property var modelData

            title: modelData.label
            keywords: modelData.id + " " + (modelData.shortcut || "") + " shortcut key binding hotkey"
            query: page.query

            ShortcutCaptureField {
                width: 160
                actionId: modelData.id
                shortcut: modelData.shortcut
            }
        }
    }
}
