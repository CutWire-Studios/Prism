import QtQuick
import QtQuick.Controls.Basic
import Prism
// Path trail back to the bin root for the folder currently being viewed. Each crumb also accepts
// dropped media, moving it into that folder.
Row {
    id: root

    required property var bin
    property string currentFolderId: ""
    signal navigate(string folderId)
    signal itemsDropped(string folderId, string payload)

    spacing: 4

    Repeater {
        model: root.bin.breadcrumbs
        delegate: Row {
            id: crumb
            required property var modelData
            required property int index
            spacing: 4

            IconGlyph {
                visible: crumb.index > 0
                anchors.verticalCenter: crumbLabel.verticalCenter
                glyph: Theme.icons.chevronRight
                iconSize: Theme.iconSizeSm
                iconColor: Theme.c.binBreadcrumb.icon
            }

            Text {
                id: crumbLabel
                text: crumb.modelData.name
                color: (crumb.modelData.id === root.currentFolderId && !crumbDrop.containsDrag)
                       ? Theme.c.binBreadcrumb.foreground : Theme.c.binBreadcrumb.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: crumb.modelData.id === root.currentFolderId ? Font.Medium : Font.Normal

                MouseArea {
                    anchors.fill: parent
                    cursorShape: crumb.modelData.id === root.currentFolderId
                                 ? Qt.ArrowCursor : Qt.PointingHandCursor
                    enabled: crumb.modelData.id !== root.currentFolderId
                    onClicked: {
                        Haptics.select()
                        root.navigate(crumb.modelData.id)
                    }
                }

                DropArea {
                    id: crumbDrop
                    anchors.fill: parent
                    anchors.margins: -4
                    keys: ["application/x-prism-asset"]
                    onDropped: (drop) => {
                        root.itemsDropped(crumb.modelData.id,
                                          drop.getDataAsString("application/x-prism-asset"))
                        drop.accept()
                    }
                }
            }
        }
    }
}
