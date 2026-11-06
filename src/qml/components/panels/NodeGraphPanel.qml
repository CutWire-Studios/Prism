import QtQuick
import Prism

PanelFrame {
    id: root

    signal inspectorRequested()

    NodeEditorPanel {
        id: editor
        anchors.fill: parent
        controller: AppController.graph

        onInspectorRequested: root.inspectorRequested()
        onRunScriptRequested: nodeId => AppController.runScript(nodeId)
        onPulseTriggerRequested: nodeId => AppController.pulseTrigger(nodeId)
        onOutputWindowRequested: AppController.showOutputWindow()
        onFilesDropped: (urls, x, y) => AppController.importFiles(urls.map(u => AppController.localPath(u)), x, y)
    }
}
