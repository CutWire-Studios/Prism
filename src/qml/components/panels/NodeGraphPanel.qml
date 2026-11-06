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
            const id = AppController.graph.addNode(NodePalette.nodeType.Input, x, y)
            if (id !== 0 && root.editors)
                root.editors.open(id, "source")
        }
        onOutputWindowRequested: AppController.showOutputWindow()
        onFilesDropped: (urls, x, y) => AppController.importFiles(urls.map(u => decodeURIComponent(u.toString().replace(/^file:\/\//, ""))))
    }
}
