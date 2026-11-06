import QtQuick
import QtQuick.Layouts
import Prism

// Base of the per-type property forms in the inspector. Edits apply to the node immediately.
ColumnLayout {
    required property var nodeId
    required property var params

    readonly property var graph: AppController.graph

    function set(patch, coalesce) {
        graph.setParams(nodeId, patch, coalesce === true)
    }

    Layout.fillWidth: true
    spacing: Theme.spacingMd
}
