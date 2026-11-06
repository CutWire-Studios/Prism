pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

// Icon-mode gallery of {id, label, image} entries: style packs, looks, animations, shapes and SVG
// templates. activated(id) on click; contextRequested(id, x, y) on right-click, in this grid's
// coordinates.
GridView {
    id: root

    property var items: []
    property string currentId: ""
    property size thumbSize: Qt.size(120, 54)
    property bool scrollable: false

    signal activated(string id)
    signal contextRequested(string id, real x, real y)

    cellWidth: thumbSize.width + Theme.spacingLg
    cellHeight: thumbSize.height + Theme.spacingXl + Theme.spacingSm
    implicitHeight: count === 0 ? 0
                  : Math.ceil(count / Math.max(1, Math.floor(width / cellWidth))) * cellHeight
    interactive: root.scrollable
    clip: true
    ScrollBar.vertical: AppScrollBar { }
    model: root.items

    delegate: Item {
        id: cell

        required property var modelData

        readonly property bool selected: modelData.id === root.currentId

        width: root.cellWidth
        height: root.cellHeight

        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.spacingXs
            radius: Theme.radiusSm
            color: "transparent"
            border.width: cell.selected ? 2 : 0
            border.color: Theme.accentOnPanel
        }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingSm
            spacing: Theme.spacingXs

            EditorImageItem {
                width: root.thumbSize.width
                height: root.thumbSize.height
                image: cell.modelData.image
            }

            ThemedLabel {
                width: root.thumbSize.width
                horizontalAlignment: Text.AlignHCenter
                size: "xs"
                tone: "default"
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                text: cell.modelData.label
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: function (mouse) {
                if (mouse.button === Qt.RightButton) {
                    const p = cell.mapToItem(root, mouse.x, mouse.y)
                    root.contextRequested(cell.modelData.id, p.x, p.y)
                } else {
                    root.activated(cell.modelData.id)
                }
            }
        }
    }
}
