import QtQuick
import Prism

// Scene hotkeys: the key stages a scene, Shift plus the key stages it and takes it to Live.
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
        text: qsTr("Click a shortcut, then press the key. Esc cancels, Backspace clears. Shift plus the key stages the input and cuts to Live.")
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
        text: qsTr("No scenes yet. Add a scene to assign keys.")
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

    Text {
        width: parent.width
        visible: page.query.length === 0
        topPadding: Theme.spacingLg
        text: qsTr("App shortcuts")
        color: Theme.c.settingsRow.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeBase
        font.weight: Font.DemiBold
    }

    Repeater {
        model: HotkeyEditor.appRows

        SettingsRow {
            id: actionRow

            required property var modelData

            title: modelData.label
            description: modelData.category
            keywords: "shortcut key binding hotkey app action"
            query: page.query

            Row {
                spacing: Theme.spacingMd

                ShortcutCaptureField {
                    width: 160
                    actionId: actionRow.modelData.label
                    shortcut: actionRow.modelData.chord
                    onCaptured: (chord) => page.note = HotkeyEditor.setAppChord(actionRow.modelData.id, chord).message
                    onCleared: {
                        HotkeyEditor.clearAppChord(actionRow.modelData.id)
                        page.note = ""
                    }
                }

                ThemedButton {
                    text: qsTr("Clear")
                    variant: "ghost"
                    onClicked: {
                        HotkeyEditor.clearAppChord(actionRow.modelData.id)
                        page.note = ""
                    }
                }

                ThemedButton {
                    text: qsTr("Reset")
                    variant: "ghost"
                    enabled: actionRow.modelData.overridden
                    onClicked: page.note = HotkeyEditor.resetAppChord(actionRow.modelData.id).message
                }
            }
        }
    }

    ThemedButton {
        visible: page.query.length === 0
        text: qsTr("Reset all app shortcuts")
        onClicked: {
            HotkeyEditor.resetAllAppChords()
            page.note = ""
        }
    }

    HotkeyProfileButtons {
        visible: page.query.length === 0
        onMessage: (text) => page.note = text
    }
}
