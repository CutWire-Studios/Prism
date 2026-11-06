pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Prism

PanelFrame {
    id: root

    property string nameMode: ""
    property string nameTarget: ""
    property var moveItemPaths: []
    property string moveFolderId: ""
    property string relinkPath: ""
    property string siblingFolder: ""
    property int siblingCount: 0

    MediaBinModel {
        id: library
    }

    function importMedia() {
        importDialog.open()
    }

    function importFolder() {
        importFolderDialog.open()
    }

    function openNameDialog(mode, target, initial) {
        root.nameMode = mode
        root.nameTarget = target
        nameField.text = initial
        nameDialog.open()
    }

    function openMovePicker(paths, folderId) {
        root.moveItemPaths = paths
        root.moveFolderId = folderId
        pickerList.model = library.moveTargets(paths, folderId)
        folderPickerDialog.open()
    }

    DropArea {
        id: importDrop
        anchors.fill: parent
        keys: ["text/uri-list"]

        property bool external: false

        onEntered: (drag) => {
            importDrop.external = drag.formats.indexOf("application/x-prism-asset") < 0
        }
        onExited: importDrop.external = false
        onDropped: (drop) => {
            if (importDrop.external && drop.hasUrls)
                library.importUrls(drop.urls)
            importDrop.external = false
            drop.accept()
        }
    }

    AssetsPanelHeader {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.controlHeight + Theme.spacingXl
        title: qsTr("Media")
        importing: library.importingFolder
        onAddSourceRequested: (anchorItem) => sourceMenu.popup(anchorItem, 0, anchorItem.height)
        onNewFolderRequested: root.openNameDialog("newFolder", "", "")
        onImportFilesRequested: root.importMedia()
        onImportFolderRequested: root.importFolder()
    }

    MediaAssetsTab {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        bin: library
        onRenameRequested: (path) => root.openNameDialog("renameItem", path, library.displayNameOf(path))
        onRelinkRequested: (path) => {
            root.relinkPath = path
            relinkDialog.open()
        }
        onSaveCopyRequested: (path) => {
            saveDialog.sourcePath = path
            saveDialog.currentFile = library.suggestedCopyUrl(path)
            saveDialog.open()
        }
        onMoveToFolderRequested: (paths) => root.openMovePicker(paths, "")
        onFolderRenameRequested: (folderId, folderName) => root.openNameDialog("renameFolder", folderId, folderName)
        onFolderMoveRequested: (folderId) => root.openMovePicker([], folderId)
        onImportRequested: root.importMedia()
        onImportFolderRequested: root.importFolder()
    }

    AssetsPanelDropOverlay {
        active: importDrop.containsDrag && importDrop.external
    }

    AssetsPanelImportOverlay {
        active: library.importingFolder
    }

    AddSourceMenu {
        id: sourceMenu
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import Media")
        fileMode: FileDialog.OpenFiles
        nameFilters: library.nameFilters()
        onAccepted: library.importUrls(selectedFiles)
    }

    FolderDialog {
        id: importFolderDialog
        title: qsTr("Import Folder")
        onAccepted: {
            if (!library.importFolder(selectedFolder))
                Toasts.error(qsTr("Couldn’t import that folder."))
        }
    }

    FileDialog {
        id: relinkDialog
        title: qsTr("Choose Media")
        fileMode: FileDialog.OpenFile
        nameFilters: library.nameFilters()
        onAccepted: {
            if (!library.relink(root.relinkPath, selectedFile))
                Toasts.warning(qsTr("Couldn’t use that file. It must be a media file not already in the library."))
        }
    }

    FileDialog {
        id: saveDialog
        property string sourcePath: ""
        title: qsTr("Save a Copy")
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (library.saveCopy(saveDialog.sourcePath, selectedFile))
                Toasts.info(qsTr("Saving a copy…"))
            else
                Toasts.error(qsTr("Couldn’t save a copy."))
        }
    }

    ThemedDialog {
        id: nameDialog
        title: root.nameMode === "newFolder" ? qsTr("New Folder")
               : root.nameMode === "renameFolder" ? qsTr("Rename Folder") : qsTr("Rename Media")
        preferredWidth: Theme.dialogWidthSm
        onOpened: {
            nameField.forceActiveFocus()
            nameField.selectAll()
        }
        onAccepted: {
            if (root.nameMode === "newFolder")
                library.createFolder(nameField.text)
            else if (root.nameMode === "renameFolder")
                library.renameFolder(root.nameTarget, nameField.text)
            else
                library.renameItem(root.nameTarget, nameField.text)
        }
        contentItem: ThemedTextField {
            id: nameField
            placeholderText: root.nameMode === "renameItem" ? qsTr("Media name") : qsTr("Folder name")
            onAccepted: nameDialog.accept()
        }
    }

    ThemedDialog {
        id: siblingRelinkDialog
        title: qsTr("Relink more files?")
        acceptText: qsTr("Relink")
        rejectText: qsTr("Not now")
        preferredWidth: Theme.dialogWidthSm
        onAccepted: library.relinkSiblings()
        contentItem: Text {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            text: qsTr("%n other missing file(s) from the same folder were found in “%1” with the same name. Relink them too?",
                       "", root.siblingCount).arg(root.siblingFolder)
        }
    }

    ThemedDialog {
        id: folderPickerDialog
        title: qsTr("Move to folder")
        showFooter: false
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm

            Rectangle {
                width: parent.width
                height: Math.min(pickerList.contentHeight + 2, 280)
                radius: Theme.c.assetsPanel.pickerRadius
                color: Theme.c.assetsPanel.pickerBackground
                border.width: Theme.c.assetsPanel.pickerBorderWidth
                border.color: Theme.c.assetsPanel.pickerBorder
                clip: true

                ListView {
                    id: pickerList
                    anchors.fill: parent
                    anchors.margins: Theme.c.assetsPanel.pickerBorderWidth
                    clip: true
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar { }

                    delegate: ItemDelegate {
                        id: optionRow
                        required property var modelData
                        width: pickerList.width
                        height: 40
                        hoverEnabled: true

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        background: Rectangle {
                            color: optionRow.hovered ? Theme.c.assetsPanel.optionBackgroundHover : "transparent"
                        }

                        contentItem: Text {
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 12
                            rightPadding: 12
                            text: optionRow.modelData.path
                            elide: Text.ElideRight
                            color: Theme.c.assetsPanel.optionForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }

                        onClicked: {
                            const targetId = optionRow.modelData.id
                            folderPickerDialog.close()
                            if (root.moveFolderId !== "")
                                library.moveFolder(root.moveFolderId, targetId)
                            else
                                library.moveItems(root.moveItemPaths, targetId)
                        }
                    }
                }
            }

            ThemedLabel {
                visible: pickerList.count === 0
                width: parent.width
                text: qsTr("There is nowhere else to move this.")
            }
        }
    }

    Connections {
        target: library

        function onImportFinished(added, skipped, limitReached) {
            if (limitReached)
                Toasts.warning(qsTr("The library is full (%1 items). Remove some media to add more.").arg(library.libraryLimit))
            else if (added > 0 && skipped > 0)
                Toasts.warning(qsTr("Imported %n file(s). %1 skipped: unsupported format.", "", added).arg(skipped))
            else if (added > 0)
                Toasts.success(qsTr("Imported %n file(s).", "", added))
            else if (skipped > 0)
                Toasts.warning(qsTr("%n file(s) skipped: unsupported format.", "", skipped))
            else
                Toasts.info(qsTr("Those files are already in the library."))
        }

        function onFolderImportFinished(folders, files, skipped, truncated) {
            if (folders === 0)
                Toasts.error(qsTr("Couldn’t import that folder."))
            else if (truncated)
                Toasts.warning(qsTr("Imported %n file(s) into %1 folder(s), as many as the library takes. Import the rest separately.", "", files).arg(folders))
            else if (skipped > 0)
                Toasts.warning(qsTr("Imported %n file(s) into %1 folder(s). %2 file(s) were skipped: unsupported format.", "", files).arg(folders).arg(skipped))
            else
                Toasts.success(qsTr("Imported %n file(s) into %1 folder(s).", "", files).arg(folders))
        }

        function onSiblingRelinkAvailable(count, folder) {
            root.siblingCount = count
            root.siblingFolder = folder
            siblingRelinkDialog.open()
        }

        function onSaveFinished(ok, name) {
            if (ok)
                Toasts.success(qsTr("Saved “%1”.").arg(name))
            else
                Toasts.error(qsTr("Couldn’t save “%1”.").arg(name))
        }
    }
}
