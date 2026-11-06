import QtQuick
import Prism
import "workspaces/designer"

SettingsPage {
    id: page

    pageId: "workspaces"
    title: qsTr("Workspaces")

    Text {
        width: parent.width
        visible: page.query.length === 0
        wrapMode: Text.WordWrap
        text: qsTr("Arrange panels into layouts, optionally across several windows. Drag a panel onto an edge of another block to split it, or onto its middle to add a tab. Nothing changes in the editor until you save.")
        color: Theme.c.settingsRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    SettingsRow {
        stacked: true
        title: qsTr("Layout designer")
        description: qsTr("The built-in Live preset keeps a copy of the original, so Reset brings it back.")
        keywords: "workspace layout panel window monitor screen dock preset split tab"
        query: page.query

        WorkspaceDesigner {
            width: parent.width
            height: implicitHeight
        }
    }
}
