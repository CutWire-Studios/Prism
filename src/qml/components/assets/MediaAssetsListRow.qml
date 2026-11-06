import QtQuick
import QtQuick.Controls.Basic
import Prism
// List and tree rows. Same single-delegate rule as MediaAssetsGridCard; see the note there.
Rectangle {
    id: root

    required property string key
    required property string path
    required property string name
    required property bool isFolder
    required property string kind
    required property string duration
    required property string format
    required property string fileSize
    required property string thumbKey
    required property string thumbRev
    required property bool missing
    required property bool probing
    required property int itemCount
    required property int depth
    required property bool expanded
    required property bool expandable

    required property var host

    readonly property bool selected: !isFolder && host.isAssetSelected(key)
    readonly property real treeIndent: host.treeMode ? root.depth * Theme.spacing2xl : 0
    readonly property real disclosureSpace: host.treeMode ? Theme.iconButtonSize : 0

    ListView.onPooled: {
        rowMenu.close()
        folderRowMenu.close()
    }

    width: ListView.view ? ListView.view.width : 0
    height: 48
    radius: Theme.c.mediaAssetsListRow.radius
    color: (rowHover.hovered || folderDrop.containsDrag) ? Theme.c.mediaAssetsListRow.backgroundHover
                                                          : Theme.c.mediaAssetsListRow.background
    border.width: root.selected ? Theme.c.mediaAssetsListRow.borderWidth : 0
    border.color: Theme.c.mediaAssetsListRow.border
    opacity: rowDrag.active ? Theme.c.mediaAssetsListRow.dragOpacity : 1
    scale: rowDrag.active ? 1.02 : 1.0

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on border.width {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Drag.active: !isFolder && rowDrag.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["text/uri-list", "application/x-prism-asset"]
    Drag.mimeData: root.isFolder ? ({}) : root.host.dragMimeData(root.key)
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2

    HoverHandler {
        id: rowHover
        cursorShape: isFolder ? Qt.PointingHandCursor
                     : (rowDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
    }

    Row {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        anchors.leftMargin: Theme.spacingLg + root.treeIndent + root.disclosureSpace
        spacing: Theme.spacingLg + Theme.spacingXs

        Rectangle {
            id: thumbFrame
            width: 56
            height: 32
            radius: Theme.c.mediaAssetsListThumb.radius
            color: Theme.c.mediaAssetsListThumb.background
            clip: true

            MediaAssetsThumb {
                anchors.fill: parent
                folder: root.isFolder
                kind: root.kind
                thumbKey: root.thumbKey
                thumbRev: root.thumbRev
                probing: root.probing
                iconSize: Theme.iconSizeBase
                iconColor: Theme.c.mediaAssetsListThumb.icon
                fillMode: Image.PreserveAspectFit
            }

            MediaAssetsMissingBadge {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 1
                size: Theme.iconSizeSm + 2
                visible: !root.isFolder && root.missing
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, content.width - thumbFrame.width - content.spacing
                               - (root.isFolder ? 0 : quickAdd.width + Theme.spacingLg))

            Text {
                width: parent.width
                text: root.name
                color: Theme.c.mediaAssetsListLabels.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.isFolder
                      ? (root.itemCount > 0 ? qsTr("%n item(s)", "", root.itemCount) : qsTr("Empty"))
                      : root.missing ? qsTr("Missing file")
                      : [root.kind, root.duration, root.format].filter(s => s.length > 0).join(" · ")
                color: root.missing ? Theme.destructive : Theme.c.mediaAssetsListLabels.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
                elide: Text.ElideRight
            }
        }
    }

    ThemedToolTip {
        text: root.isFolder ? root.name : [root.name, root.fileSize].filter(s => s.length > 0).join("\n")
        visible: rowHover.hovered && !rowMenu.visible && !folderRowMenu.visible && !rowDrag.active
        y: parent.height + 4
    }

    DropArea {
        id: folderDrop
        anchors.fill: parent
        enabled: root.isFolder
        keys: ["application/x-prism-asset"]
        onDropped: (drop) => {
            root.host.dropOnFolder(root.key, drop.getDataAsString("application/x-prism-asset"))
            drop.accept()
        }
    }

    Item {
        anchors.fill: parent

        TapHandler {
            id: leftTap
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            enabled: !rowDrag.active
            onTapped: {
                if (root.isFolder)
                    return
                root.host.forceActiveFocus()
                root.host.clickAsset(root.key, leftTap.point.modifiers)
            }
            onDoubleTapped: {
                if (root.isFolder)
                    root.host.bin.folderId = root.key
                else if (!root.missing)
                    root.host.addToGraph([root.key])
            }
        }
        DragHandler {
            id: rowDrag
            target: null
            enabled: !root.isFolder
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            onActiveChanged: {
                if (active)
                    root.host.ensureAssetSelected(root.key)
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                if (root.isFolder) {
                    folderRowMenu.popup()
                    return
                }
                root.host.forceActiveFocus()
                root.host.ensureAssetSelected(root.key)
                rowMenu.popup()
            }
        }
        TapHandler {
            acceptedDevices: PointerDevice.TouchScreen
            onTapped: {
                if (root.isFolder) {
                    root.host.bin.folderId = root.key
                } else {
                    root.host.ensureAssetSelected(root.key)
                    rowMenu.popup()
                }
            }
        }
    }

    AssetQuickAddButton {
        id: quickAdd
        z: 5
        visible: !root.isFolder
        revealed: rowHover.hovered
        enabled: !root.missing
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: Theme.spacingLg
        onClicked: root.host.addToGraph([root.key])
    }

    // Declared after the tap overlay so it sits on top: the arrow has to take the press itself.
    IconButton {
        visible: root.host.treeMode && root.isFolder && root.expandable
        x: Theme.spacingLg + root.treeIndent
        anchors.verticalCenter: parent.verticalCenter
        buttonSize: Theme.iconButtonSize
        iconSize: Theme.iconSizeMd
        variant: "ghost"
        glyph: root.expanded ? Theme.icons.chevronDown : Theme.icons.chevronRight
        tooltip: root.expanded ? qsTr("Collapse folder") : qsTr("Expand folder")
        onClicked: root.host.bin.toggleExpanded(root.key)
    }

    MediaAssetsItemMenu {
        id: rowMenu
        host: root.host
        path: root.key
        missing: root.missing
    }

    MediaAssetsFolderMenu {
        id: folderRowMenu
        host: root.host
        folderId: root.key
        folderName: root.name
    }
}
