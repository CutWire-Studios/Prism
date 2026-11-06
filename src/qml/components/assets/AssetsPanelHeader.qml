import QtQuick
import Prism
// Panel title and the media import actions.
Rectangle {
    id: root

    property string title: ""
    property bool importing: false

    signal addSourceRequested(var anchorItem)
    signal newFolderRequested()
    signal importFilesRequested()
    signal importFolderRequested()

    color: Theme.c.assetsPanelHeader.background

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.c.assetsPanelHeader.borderWidth
        color: Theme.c.assetsPanelHeader.border
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.pagePadding
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: Theme.c.assetsPanelHeader.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        IconButton {
            id: addSourceButton
            glyph: Theme.icons.diamondPlus
            tooltip: qsTr("Add source")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.addSourceRequested(addSourceButton)
        }

        ThemedButton {
            text: qsTr("New Folder")
            variant: "ghost"
            glyph: Theme.icons.folder
            tooltip: qsTr("Create a new folder here")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.newFolderRequested()
        }

        AssetsPanelImportSplit {
            anchors.verticalCenter: parent.verticalCenter
            importing: root.importing
            onImportFilesRequested: root.importFilesRequested()
            onImportFolderRequested: root.importFolderRequested()
        }
    }
}
