import QtQuick
import Prism

PanelFrame {
    id: root

    property var editors: null

    NodeEditorPanel {
        id: editor
        anchors.fill: parent
        controller: AppController.graph

        onOpenEditorRequested: (nodeId, kind) => {
            if (root.editors)
                root.editors.open(nodeId, kind)
        }
        onAddInputNodeRequested: (x, y) => {
            sourceMenu.worldX = x
            sourceMenu.worldY = y
            sourceMenu.popup()
        }
        onRunScriptRequested: nodeId => AppController.runScript(nodeId)
        onPulseTriggerRequested: nodeId => AppController.pulseTrigger(nodeId)
        onOutputWindowRequested: AppController.showOutputWindow()
        onFilesDropped: (urls, x, y) => AppController.importFiles(urls.map(u => decodeURIComponent(u.toString().replace(/^file:\/\//, ""))))
    }

    AddSourceMenu {
        id: sourceMenu
    }
}
