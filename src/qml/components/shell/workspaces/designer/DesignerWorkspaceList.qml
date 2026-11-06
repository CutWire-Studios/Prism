pragma ComponentBehavior: Bound
import QtQuick
import Prism

// Workspace picker with its management buttons. Anything that would replace the draft goes
// through designer.guard() so unsaved edits are never dropped silently.
Column {
    id: root

    required property var designer
    property bool confirmDelete: false

    readonly property var current: designer.creating ? null : Workspaces.workspace(designer.selectedId)
    readonly property bool saved: !designer.creating && !!current && !!current.id

    spacing: Theme.spacingMd

    Timer {
        id: confirmTimer
        interval: 3000
        onTriggered: root.confirmDelete = false
    }

    Column {
        width: parent.width
        spacing: Theme.spacingXs

        Repeater {
            model: Workspaces.workspaces

            Rectangle {
                id: row

                required property var modelData
                readonly property bool selected: !root.designer.creating && modelData.id === root.designer.selectedId

                width: parent.width
                height: rowText.implicitHeight + Theme.spacingMd * 2
                radius: Theme.radiusMd
                color: selected ? Theme.c.settingsPane.selectedBackground
                    : rowArea.containsMouse ? Theme.c.settingsPane.hoverBackground : "transparent"

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!row.selected || root.designer.creating)
                            root.designer.guard(() => root.designer.load(row.modelData.id))
                    }
                }

                Column {
                    id: rowText
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: 2

                    Text {
                        width: parent.width
                        visible: !(row.selected && root.designer.renaming)
                        elide: Text.ElideRight
                        text: row.selected && root.designer.draft ? root.designer.draft.name : row.modelData.name
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: row.selected ? Font.Medium : Font.Normal
                    }

                    ThemedTextField {
                        id: renameField
                        width: parent.width
                        visible: row.selected && root.designer.renaming
                        onVisibleChanged: if (visible) {
                            text = root.designer.draft.name
                            forceActiveFocus()
                            selectAll()
                        }
                        onAccepted: root.designer.setName(text)
                        Keys.onEscapePressed: root.designer.renaming = false
                    }

                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: {
                            const tags = []
                            if (row.modelData.builtin)
                                tags.push(qsTr("Built-in"))
                            if (row.modelData.modified)
                                tags.push(qsTr("Modified"))
                            if (row.modelData.id === Workspaces.activeId)
                                tags.push(qsTr("Active"))
                            return tags.join(" · ")
                        }
                        visible: text.length > 0
                        color: row.modelData.id === Workspaces.activeId ? Theme.accent : Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            visible: root.designer.creating
            height: newText.implicitHeight + Theme.spacingMd * 2
            radius: Theme.radiusMd
            color: Theme.c.settingsPane.selectedBackground

            Column {
                id: newText
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: 2

                Text {
                    width: parent.width
                    visible: !root.designer.renaming
                    elide: Text.ElideRight
                    text: root.designer.draft ? root.designer.draft.name : ""
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                }

                ThemedTextField {
                    width: parent.width
                    visible: root.designer.renaming
                    onVisibleChanged: if (visible) {
                        text = root.designer.draft.name
                        forceActiveFocus()
                        selectAll()
                    }
                    onAccepted: root.designer.setName(text)
                    Keys.onEscapePressed: root.designer.renaming = false
                }

                Text {
                    width: parent.width
                    text: qsTr("Not saved yet")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }

    Flow {
        width: parent.width
        spacing: Theme.spacingSm

        ThemedButton {
            variant: "secondary"
            text: qsTr("New")
            glyph: Theme.icons.plus
            onClicked: root.designer.guard(() => root.designer.startNew())
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Duplicate")
            glyph: Theme.icons.copy
            enabled: root.saved
            onClicked: root.designer.guard(() => {
                const id = Workspaces.duplicate(root.designer.selectedId)
                if (id.length > 0)
                    root.designer.load(id)
            })
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Rename")
            glyph: Theme.icons.pencil
            enabled: root.designer.draft !== null
            onClicked: root.designer.renaming = !root.designer.renaming
        }

        ThemedButton {
            variant: "destructive"
            text: root.confirmDelete ? qsTr("Confirm delete") : qsTr("Delete")
            glyph: Theme.icons.trash
            visible: root.saved && !root.current.builtin
            onClicked: {
                if (!root.confirmDelete) {
                    root.confirmDelete = true
                    confirmTimer.restart()
                    return
                }
                confirmTimer.stop()
                root.confirmDelete = false
                Workspaces.remove(root.designer.selectedId)
                root.designer.cancelDrag()
                root.designer.selectFirst()
            }
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset")
            glyph: Theme.icons.reset
            visible: root.saved && root.current.builtin && !!root.current.modified
            onClicked: Workspaces.resetPreset(root.designer.selectedId)
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Set active")
            enabled: root.saved && root.designer.selectedId !== Workspaces.activeId
            onClicked: Workspaces.activeId = root.designer.selectedId
        }
    }
}
