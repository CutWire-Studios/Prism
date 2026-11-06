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
        onPulseTriggerRequested: nodeId => AppController.pulseTrigger(nodeId)
        onOutputWindowRequested: AppController.showOutputWindow()
        onFilesDropped: (urls, x, y) => AppController.importFiles(urls.map(u => AppController.localPath(u)), x, y)
    }
}
