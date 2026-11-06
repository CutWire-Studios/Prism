import QtQuick
import Prism

// More import options hanging off the header's split button.
ThemedContextMenu {
    id: root

    signal importFilesRequested()
    signal importFolderRequested()

    implicitWidth: 220

    ThemedMenuItem {
        text: qsTr("Import Files…")
        icon.name: Theme.icons.upload
        onTriggered: root.importFilesRequested()
    }

    ThemedMenuItem {
        text: qsTr("Import Folder…")
        icon.name: Theme.icons.folderInput
        onTriggered: root.importFolderRequested()
    }
}
