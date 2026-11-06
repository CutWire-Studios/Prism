import QtQuick
import Prism

// Split button: the left half imports files, the chevron opens the folder variant. Both halves
// share one bordered box so the header reads as two actions, not three competing buttons.
Rectangle {
    id: root

    property bool importing: false

    signal importFilesRequested()
    signal importFolderRequested()

    width: importFilesHalf.width
           + (importMenuHalf.visible ? importSplitDivider.width + importMenuHalf.width : 0)
    height: Theme.controlHeight
    radius: Theme.c.assetsPanelImportSplit.radius
    color: "transparent"
    border.width: Theme.c.assetsPanelImportSplit.borderWidth
    border.color: Theme.c.assetsPanelImportSplit.border
    opacity: importing ? Theme.c.assetsPanelImportSplit.busyOpacity : 1

    Row {
        anchors.fill: parent
        spacing: 0

        ThemedButton {
            id: importFilesHalf
            text: qsTr("Import")
            variant: "ghost"
            flat: true
            radius: Theme.c.assetsPanelImportSplit.buttonRadius
            glyph: Theme.icons.upload
            tooltip: qsTr("Import video, audio or image files")
            enabled: !root.importing
            height: parent.height - Theme.c.assetsPanelImportSplit.borderWidth * 2
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.importFilesRequested()
        }

        Rectangle {
            id: importSplitDivider
            width: Theme.c.assetsPanelImportSplit.borderWidth
            height: parent.height - Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.c.assetsPanelImportSplit.divider
            visible: importMenuHalf.visible
        }

        ThemedButton {
            id: importMenuHalf
            variant: "ghost"
            flat: true
            radius: Theme.c.assetsPanelImportSplit.buttonRadius
            glyph: Theme.icons.chevronDown
            glyphSize: Theme.iconSizeSm
            leftPadding: Theme.spacingLg
            rightPadding: Theme.spacingLg
            tooltip: qsTr("More import options")
            enabled: !root.importing
            visible: !Theme.touchUi
            height: parent.height - Theme.c.assetsPanelImportSplit.borderWidth * 2
            anchors.verticalCenter: parent.verticalCenter
            onClicked: importMenu.popup(0, root.height + Theme.spacingSm)
        }
    }

    AssetsPanelImportMenu {
        id: importMenu
        onImportFilesRequested: root.importFilesRequested()
        onImportFolderRequested: root.importFolderRequested()
    }
}
