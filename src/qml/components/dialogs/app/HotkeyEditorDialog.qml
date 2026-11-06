import QtQuick
import QtQuick.Controls.Basic
import Prism

ThemedDialog {
    id: root

    title: qsTr("Edit hotkeys")
    showAccept: false
    rejectText: qsTr("Close")
    rejectVariant: "primary"
    preferredWidth: Theme.dialogWidthLg

    property string note: ""

    onAboutToShow: {
        HotkeyEditor.refresh()
        note = ""
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthLg

        ThemedLabel {
            width: parent.width
            text: qsTr("Assign a key to each A/B switcher input. The key sends the input to Deck A; Shift plus the same key sends it to Deck B. Click a field, then press the key. Esc cancels, Backspace clears.")
        }

        Flickable {
            id: flick
            width: parent.width
            height: Math.min(rowsColumn.implicitHeight, root.availableContentHeight - 160)
            contentHeight: rowsColumn.implicitHeight
            clip: true
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: rowsColumn
                width: flick.width

                Repeater {
                    model: HotkeyEditor.rows

                    Item {
                        id: row
                        required property int index
                        required property var modelData
                        width: rowsColumn.width
                        height: 40

                        ThemedLabel {
                            anchors.left: parent.left
                            anchors.right: capture.left
                            anchors.rightMargin: Theme.spacingLg
                            anchors.verticalCenter: parent.verticalCenter
                            tone: "default"
                            size: "sm"
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
                            text: row.modelData.label
                        }

                        ShortcutCaptureField {
                            id: capture
                            anchors.right: deckB.left
                            anchors.rightMargin: Theme.spacingLg
                            anchors.verticalCenter: parent.verticalCenter
                            actionId: row.modelData.label
                            shortcut: row.modelData.chord
                            onCaptured: (chord) => root.note = HotkeyEditor.setChord(row.index, chord).message
                            onCleared: {
                                HotkeyEditor.clearRow(row.index)
                                root.note = ""
                            }
                        }

                        ThemedLabel {
                            id: deckB
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 90
                            horizontalAlignment: Text.AlignRight
                            wrapMode: Text.NoWrap
                            text: row.modelData.deckBChord.length > 0
                                  ? qsTr("B: %1").arg(Theme.shortcutDisplay(row.modelData.deckBChord)) : ""
                        }
                    }
                }
            }
        }

        ThemedLabel {
            width: parent.width
            visible: HotkeyEditor.rows.length === 0
            text: qsTr("No A/B switcher inputs yet. Connect sources to an A/B Select node to assign keys.")
        }

        ThemedLabel {
            width: parent.width
            visible: root.note.length > 0
            tone: "default"
            text: root.note
        }

        HotkeyProfileButtons {
            onMessage: (text) => root.note = text
        }
    }
}
