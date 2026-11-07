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
            text: qsTr("Assign a key to each scene. The key stages the scene; Shift plus the same key stages it and takes it. Click a field, then press the key. Esc cancels, Backspace clears.")
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

                ThemedLabel {
                    width: rowsColumn.width
                    topPadding: Theme.spacingLg
                    tone: "default"
                    text: qsTr("App shortcuts")
                }

                Repeater {
                    model: HotkeyEditor.appRows

                    Item {
                        id: appRow
                        required property int index
                        required property var modelData
                        readonly property bool newCategory: index === 0
                            || HotkeyEditor.appRows[index - 1].category !== modelData.category
                        width: rowsColumn.width
                        height: 40 + (newCategory ? 20 : 0)

                        ThemedLabel {
                            visible: appRow.newCategory
                            anchors.left: parent.left
                            anchors.top: parent.top
                            size: "xs"
                            text: appRow.modelData.category
                        }

                        ThemedLabel {
                            anchors.left: parent.left
                            anchors.right: appCapture.left
                            anchors.rightMargin: Theme.spacingLg
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 10
                            tone: "default"
                            size: "sm"
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
                            text: appRow.modelData.label
                        }

                        ShortcutCaptureField {
                            id: appCapture
                            anchors.right: appClear.left
                            anchors.rightMargin: Theme.spacingMd
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 7
                            actionId: appRow.modelData.label
                            shortcut: appRow.modelData.chord
                            onCaptured: (chord) => root.note = HotkeyEditor.setAppChord(appRow.modelData.id, chord).message
                            onCleared: {
                                HotkeyEditor.clearAppChord(appRow.modelData.id)
                                root.note = ""
                            }
                        }

                        ThemedButton {
                            id: appClear
                            anchors.right: appReset.left
                            anchors.rightMargin: Theme.spacingMd
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 7
                            text: qsTr("Clear")
                            variant: "ghost"
                            onClicked: {
                                HotkeyEditor.clearAppChord(appRow.modelData.id)
                                root.note = ""
                            }
                        }

                        ThemedButton {
                            id: appReset
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 7
                            text: qsTr("Reset")
                            variant: "ghost"
                            enabled: appRow.modelData.overridden
                            onClicked: root.note = HotkeyEditor.resetAppChord(appRow.modelData.id).message
                        }
                    }
                }
            }
        }

        ThemedLabel {
            width: parent.width
            visible: HotkeyEditor.rows.length === 0
            text: qsTr("No scenes yet. Add a scene to assign keys.")
        }

        ThemedLabel {
            width: parent.width
            visible: root.note.length > 0
            tone: "default"
            text: root.note
        }

        ThemedButton {
            text: qsTr("Reset all app shortcuts")
            onClicked: {
                HotkeyEditor.resetAllAppChords()
                root.note = ""
            }
        }

        HotkeyProfileButtons {
            onMessage: (text) => root.note = text
        }
    }
}
