import QtQuick
import Prism

// Shared ".panel" chrome: dark surface, hairline border, 5.6px radius.
Rectangle {
    id: root
    default property alias content: contentItem.data

    // Fill lives in the slot so a theme can make the panel translucent. The border
    // stays on this Rectangle: PropertiesPanel assigns border.width for sheet mode.
    color: "transparent"
    border.color: Theme.c.panelFrame.border
    border.width: Theme.c.panelFrame.borderWidth
    radius: Theme.c.panelFrame.radius
    clip: true

    ThemeSlot {
        anchors.fill: parent
        z: -1
        slot: "PanelFrameBackground"
        control: root
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.panelFrame.background
                border.color: root.border.color
                border.width: root.border.width
                radius: root.radius
            }
        }
    }

    Item {
        id: contentItem
        anchors.fill: parent
    }
}
