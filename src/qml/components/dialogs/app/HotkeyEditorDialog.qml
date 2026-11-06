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
            text: qsTr("Assign a key to each switcher input. The key stages the input; Shift plus the same key stages it and cuts to Live. Click a field, then press the key. Esc cancels, Backspace clears.")
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
                            anchors.right: cutLabel.left
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
                            id: cutLabel
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 90
                            horizontalAlignment: Text.AlignRight
                            wrapMode: Text.NoWrap
                            text: row.modelData.cutChord.length > 0
                                  ? qsTr("Take: %1").arg(Theme.shortcutDisplay(row.modelData.cutChord)) : ""
                        }
                    }
                }
            }
        }

        ThemedLabel {
            width: parent.width
            visible: HotkeyEditor.rows.length === 0
            text: qsTr("No cues yet. Add a cue to assign keys.")
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
