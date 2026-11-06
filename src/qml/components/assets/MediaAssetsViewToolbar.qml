import QtQuick
import Prism
// Grid, list, tree and sort toggles that share the media search row.
Row {
    id: root

    required property var bin

    spacing: Theme.spacingSm

    IconButton {
        glyph: Theme.icons.layoutGrid
        variant: "ghost"
        tooltip: qsTr("Grid view")
        active: root.bin.viewMode === "grid"
        onClicked: root.bin.viewMode = "grid"
    }

    IconButton {
        glyph: Theme.icons.list
        variant: "ghost"
        tooltip: qsTr("List view")
        active: root.bin.viewMode === "list"
        onClicked: root.bin.viewMode = "list"
    }

    IconButton {
        glyph: Theme.icons.listTree
        variant: "ghost"
        tooltip: qsTr("Tree view")
        active: root.bin.viewMode === "tree"
        onClicked: root.bin.viewMode = "tree"
    }

    IconButton {
        id: sortButton
        glyph: root.bin.sortMode === "name" ? Theme.icons.sortByName : Theme.icons.sortByKind
        variant: "ghost"
        tooltip: qsTr("Sort")
        onClicked: sortMenu.popup(sortButton, 0, sortButton.height)
    }

    ThemedContextMenu {
        id: sortMenu

        ThemedMenuItem {
            text: qsTr("Name")
            icon.name: root.bin.sortMode === "name" ? Theme.icons.check : ""
            onTriggered: root.bin.sortMode = "name"
        }
        ThemedMenuItem {
            text: qsTr("Type")
            icon.name: root.bin.sortMode === "type" ? Theme.icons.check : ""
            onTriggered: root.bin.sortMode = "type"
        }
        ThemedMenuItem {
            text: qsTr("Date added")
            icon.name: root.bin.sortMode === "added" ? Theme.icons.check : ""
            onTriggered: root.bin.sortMode = "added"
        }
    }
}
