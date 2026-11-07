import QtQuick
import Prism

PanelFrame {
    id: root

    signal inspectorRequested()

    EmptyState {
        anchors.centerIn: parent
        visible: !AppController.inCue
        glyph: "pencil"
        title: qsTr("No cue open")
        hint: qsTr("Press a cue's edit button to work on its graph")
    }

    NodeEditorPanel {
        id: editor
        anchors.fill: parent
        visible: AppController.inCue
        controller: AppController.graph

        onInspectorRequested: root.inspectorRequested()
        onRunScriptRequested: nodeId => AppController.runScript(nodeId)
        onOutputWindowRequested: AppController.showOutputWindow()
        onFilesDropped: (urls, x, y) => AppController.importFiles(urls.map(u => AppController.localPath(u)), x, y)
    }

    Rectangle {
        visible: AppController.editingLive
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
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
