import QtQuick
import QtQuick.Controls.Basic
import Prism
// One delegate type for both folder and asset entries — not a DelegateChooser. A DelegateChooser
// changes how the chosen delegate is parented and kills the drag: the Drag.active binding detects a
// loop the instant a drag begins. The handlers live on the thumbnail rectangle, not on the column
// that owns the Drag attached property, for the same reason (QDrag::exec() ungrabs the item the
// attached property sits on and the handler re-enters its own active binding).
Column {
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

    required property var host
    required property real cardSize

    readonly property bool selected: !isFolder && host.isAssetSelected(key)
    readonly property bool menusVisible: cardMenu.visible || folderMenu.visible

    GridView.onPooled: {
        cardMenu.close()
        folderMenu.close()
    }

    width: cardSize
    spacing: 4

    opacity: assetDrag.active ? 0.85 : 1
    scale: assetDrag.active ? 1.04 : 1.0

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Drag.active: !isFolder && assetDrag.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["text/uri-list", "application/x-prism-asset"]
    Drag.mimeData: root.isFolder ? ({}) : root.host.dragMimeData(root.key)
    // Default hotspot is the top-left corner; Wayland compositors then deliver the drop far from the grab.
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: cardSize * 9 / 32

    HoverHandler {
        id: cardHover
        cursorShape: root.isFolder ? Qt.PointingHandCursor
                     : (assetDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
    }

    ThemedToolTip {
        text: root.isFolder
              ? qsTr("%1 — %n item(s)", "", root.itemCount).arg(root.name)
              : [root.name, root.format, root.fileSize].filter(s => s.length > 0).join("\n")
        visible: cardHover.hovered && !root.menusVisible && !assetDrag.active
        y: parent.height + 4
    }

    Rectangle {
        id: frame
        width: root.cardSize
        height: root.cardSize * 9 / 16
        radius: Theme.c.mediaAssetsGridFrame.radius
        color: Theme.c.mediaAssetsGridFrame.background
        clip: true
        border.width: (!root.selected && (cardHover.hovered || folderDrop.containsDrag))
                      ? Theme.c.mediaAssetsGridFrame.borderWidth : 0
        border.color: Theme.c.mediaAssetsGridFrame.border
        scale: (!root.isFolder && cardHover.hovered) ? 1.03 : 1.0

        Behavior on scale {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on border.width {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        MediaAssetsThumb {
            anchors.fill: parent
            folder: root.isFolder
            kind: root.kind
            thumbKey: root.thumbKey
            thumbRev: root.thumbRev
            probing: root.probing
            fillMode: Image.PreserveAspectFit
        }

        MediaAssetsMissingBadge {
            z: 3
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingSm
            visible: !root.isFolder && root.missing
        }

        MediaAssetsDurationBadge {
            label: root.isFolder ? (root.itemCount > 0 ? qsTr("%n item(s)", "", root.itemCount) : "")
                                 : root.duration
        }

        AssetQuickAddButton {
            z: 5
            visible: !root.isFolder
            revealed: cardHover.hovered
            enabled: !root.missing
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            onClicked: root.host.addToGraph([root.key])
        }

        MediaAssetsSelectionRing {
            selected: root.selected
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

        TapHandler {
            id: leftTap
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            enabled: !assetDrag.active
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
            id: assetDrag
            // Without target: null the handler moves the card itself, clobbering the view's x/y.
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
                    folderMenu.popup()
                    return
                }
                root.host.forceActiveFocus()
                root.host.ensureAssetSelected(root.key)
                cardMenu.popup()
            }
        }
        TapHandler {
            acceptedDevices: PointerDevice.TouchScreen
            onTapped: {
                if (root.isFolder) {
                    root.host.bin.folderId = root.key
                } else {
                    root.host.ensureAssetSelected(root.key)
                    cardMenu.popup()
                }
            }
        }
    }

    Text {
        width: parent.width
        text: root.name
        color: Theme.c.mediaAssetsGridNameRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        elide: Text.ElideRight
    }

    MediaAssetsItemMenu {
        id: cardMenu
        host: root.host
        path: root.key
        missing: root.missing
    }

    MediaAssetsFolderMenu {
        id: folderMenu
        host: root.host
        folderId: root.key
        folderName: root.name
    }
}
