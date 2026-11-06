pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

PanelFrame {
    id: root

    property string nameDialogMode: ""
    property string nameDialogTarget: ""

    AssetLibraryModel {
        id: assets
    }

    ThemedDialog {
        id: nameDialog
        title: root.nameDialogMode === "rename" ? qsTr("Rename Folder") : qsTr("New Folder")
        preferredWidth: Theme.dialogWidthSm
        onOpened: {
            nameField.text = ""
            nameField.forceActiveFocus()
        }
        onAccepted: {
            if (root.nameDialogMode === "rename")
                assets.renameFolder(root.nameDialogTarget, nameField.text)
            else
                assets.createFolder(nameField.text)
        }
        contentItem: ThemedTextField {
            id: nameField
            placeholderText: qsTr("Folder name")
            onAccepted: nameDialog.accept()
        }
    }

    ThemedContextMenu {
        id: itemMenu

        property string key: ""
        property string label: ""
        property bool isFolder: false

        ThemedMenuItem {
            visible: !itemMenu.isFolder
            text: qsTr("Add to graph")
            onTriggered: assets.addToGraph(itemMenu.key)
        }
        ThemedMenuItem {
            visible: !itemMenu.isFolder && assets.folderId.length > 0
            text: qsTr("Move out of folder")
            onTriggered: assets.moveClip(itemMenu.key, assets.parentFolderId)
        }
        ThemedMenuItem {
            visible: itemMenu.isFolder
            text: qsTr("Rename")
            onTriggered: {
                root.nameDialogMode = "rename"
                root.nameDialogTarget = itemMenu.key
                nameDialog.open()
            }
        }
        ThemedMenuItem {
            text: itemMenu.isFolder ? qsTr("Remove folder") : qsTr("Remove from library")
            onTriggered: itemMenu.isFolder ? assets.removeFolder(itemMenu.key) : assets.removeClip(itemMenu.key)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            ThemedTextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Search media")
                text: assets.search
                onTextChanged: assets.search = text
            }
            IconButton {
                glyph: "plus"
                tooltip: qsTr("Add media files")
                onClicked: AppController.requestAddFiles()
            }
            IconButton {
                glyph: "folder-open"
                tooltip: qsTr("Add media folder")
                onClicked: AppController.requestAddFolder()
            }
            IconButton {
                glyph: "folder-plus"
                tooltip: qsTr("New folder")
                onClicked: {
                    root.nameDialogMode = "create"
                    nameDialog.open()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: assets.folderId.length > 0 && assets.search.length === 0
            spacing: Theme.spacingXs

            IconButton {
                glyph: "chevron-left"
                tooltip: qsTr("Up one folder")
                onClicked: assets.folderId = assets.parentFolderId
            }
            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                elide: Text.ElideRight
                text: assets.folderName
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 1
            model: assets
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: AppScrollBar {}

            delegate: Rectangle {
                id: row

                required property string key
                required property string name
                required property bool isFolder
                required property string suffix
                required property bool isAudio

                width: ListView.view.width
                height: Theme.controlHeight
                radius: Theme.radiusSm
                color: rowMouse.containsMouse ? Qt.alpha(Theme.accent, 0.12) : "transparent"

                Drag.active: rowDrag.active
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction
                Drag.mimeData: ({ "text/uri-list": "file://" + row.key })

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMd
                    anchors.rightMargin: Theme.spacingMd
                    spacing: Theme.spacingMd

                    IconGlyph {
                        glyph: row.isFolder ? "folder" : (row.isAudio ? "music" : "film")
                    }
                    ThemedLabel {
                        Layout.fillWidth: true
                        tone: "default"
                        wrapMode: Text.NoWrap
                        elide: Text.ElideRight
                        text: row.name
                    }
                    ThemedLabel {
                        visible: !row.isFolder
                        text: row.suffix
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            itemMenu.key = row.key
                            itemMenu.isFolder = row.isFolder
                            itemMenu.popup()
                        }
                    }
                    onDoubleClicked: mouse => {
                        if (mouse.button !== Qt.LeftButton)
                            return
                        if (row.isFolder)
                            assets.folderId = row.key
                        else
                            assets.addToGraph(row.key)
                    }
                }

                DragHandler {
                    id: rowDrag
                    target: null
                    enabled: !row.isFolder
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: list.count === 0
                compact: true
                glyph: "film"
                title: assets.search.length > 0 ? qsTr("No matches") : qsTr("No media")
                hint: assets.search.length > 0 ? "" : qsTr("Add files or drop them here")
            }
        }
    }
}
