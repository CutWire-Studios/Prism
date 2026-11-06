import QtQuick
import Prism
// Context menu for one media item, shared by the grid card and the list row. Acts on the whole
// selection when the item is part of a multi-selection.
ThemedContextMenu {
    id: root

    required property var host
    required property string path
    required property bool missing

    readonly property int selectionCount: root.host.selectedKeys.length
    readonly property bool single: selectionCount <= 1

    ThemedMenuItem {
        text: root.selectionCount > 1 ? qsTr("Add %n items to graph", "", root.selectionCount)
                                      : qsTr("Add to graph")
        icon.name: Theme.icons.plus
        enabled: !root.missing || root.selectionCount > 1
        onTriggered: root.host.addToGraph(root.host.selectedKeys)
    }
    ThemedMenuItem {
        text: qsTr("Rename…")
        icon.name: Theme.icons.pencil
        visible: root.single
        onTriggered: root.host.renameRequested(root.path)
    }
    ThemedMenuItem {
        text: root.missing ? qsTr("Relink media…") : qsTr("Replace media…")
        icon.name: Theme.icons.refresh
        visible: root.single
        onTriggered: root.host.relinkRequested(root.path)
    }
    ThemedMenuItem {
        text: root.selectionCount > 1 ? qsTr("Move %n items to folder…", "", root.selectionCount)
                                      : qsTr("Move to folder…")
        icon.name: Theme.icons.folder
        visible: root.host.bin.folderCount > 0
        onTriggered: root.host.moveToFolderRequested(root.host.selectedKeys)
    }
    ThemedMenuItem {
        text: qsTr("Show in file manager")
        icon.name: Theme.icons.externalLink
        visible: root.single
        onTriggered: root.host.bin.revealInFileManager(root.path)
    }
    ThemedMenuItem {
        text: qsTr("Save a copy…")
        icon.name: Theme.icons.save
        visible: root.single
        enabled: !root.missing
        onTriggered: root.host.saveCopyRequested(root.path)
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: root.selectionCount > 1 ? qsTr("Remove %n items from library", "", root.selectionCount)
                                      : qsTr("Remove from library")
        icon.name: Theme.icons.trash
        onTriggered: root.host.removeSelected()
    }
}
