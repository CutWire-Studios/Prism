import QtQuick
import Prism

// A/B switcher hotkeys: the key sends an input to Deck A, Shift plus the key to Deck B.
// Edits apply and are saved immediately.
SettingsPage {
    id: page

    pageId: "keymap"
    title: qsTr("Keyboard shortcuts")

    property string note: ""

    Text {
        width: parent.width
        visible: page.query.length === 0
        wrapMode: Text.WordWrap
        text: qsTr("Click a shortcut, then press the key. Esc cancels, Backspace clears. Shift plus the key sends the input to Deck B.")
        color: Theme.c.settingsRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Text {
        width: parent.width
        visible: page.note.length > 0
        wrapMode: Text.WordWrap
        text: page.note
        color: Theme.c.settingsRow.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Text {
        width: parent.width
        visible: HotkeyEditor.rows.length === 0 && page.query.length === 0
        wrapMode: Text.WordWrap
        text: qsTr("No A/B switcher inputs yet. Connect sources to an A/B Select node to assign keys.")
        color: Theme.c.settingsRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Repeater {
        model: HotkeyEditor.rows

        SettingsRow {
            id: bindingRow

            required property int index
            required property var modelData

            title: modelData.label
            keywords: "shortcut key binding hotkey switcher deck"
            query: page.query

            ShortcutCaptureField {
                width: 160
                actionId: bindingRow.modelData.label
                shortcut: bindingRow.modelData.chord
                onCaptured: (chord) => page.note = HotkeyEditor.setChord(bindingRow.index, chord).message
                onCleared: {
                    HotkeyEditor.clearRow(bindingRow.index)
                    page.note = ""
                }
            }
        }
    }

    HotkeyProfileButtons {
        visible: page.query.length === 0
        onMessage: (text) => page.note = text
    }
}
