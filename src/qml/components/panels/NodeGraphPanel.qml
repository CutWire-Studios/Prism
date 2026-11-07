import QtQuick
import Prism

PanelFrame {
    id: root

    signal inspectorRequested()

    readonly property bool showTabs: {
        Workspaces.workspaces
        return Workspaces.workspace(Workspaces.activeId).sceneTabs !== false
    }

    SceneTabBar {
        id: tabBar
        visible: root.showTabs
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: visible ? implicitHeight : 0
    }

    Item {
        id: body
        anchors.top: tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }

    EmptyState {
        anchors.centerIn: body
        visible: !AppController.inScene
        glyph: "pencil"
        title: qsTr("No scene open")
        hint: qsTr("Pick a scene tab to work on its graph")
    }

    NodeEditorPanel {
        id: editor
        anchors.fill: body
        visible: AppController.inScene
        controller: AppController.graph

        onInspectorRequested: root.inspectorRequested()
        onRunScriptRequested: nodeId => AppController.runScript(nodeId)
        onOutputWindowRequested: AppController.showOutputWindow()
        onFilesDropped: (urls, x, y) => AppController.importFiles(urls.map(u => AppController.localPath(u)), x, y)
    }

    Rectangle {
        visible: AppController.editingLive
        anchors.top: body.top
        anchors.horizontalCenter: body.horizontalCenter
        anchors.topMargin: Theme.spacingSm
        implicitWidth: onAirRow.implicitWidth + 2 * Theme.spacingMd
        implicitHeight: onAirRow.implicitHeight + Theme.spacingSm
        radius: Theme.radiusSm
        color: Theme.destructive

        ThemedLabel {
            id: onAirRow
            anchors.centerIn: parent
            tone: "default"
            text: qsTr("ON AIR - edits go to the program output")
        }
    }
}
