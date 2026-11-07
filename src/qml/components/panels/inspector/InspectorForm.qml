import QtQuick
import QtQuick.Layouts
import Prism

// Base of the per-type property forms in the inspector. Edits apply to the node immediately.
ColumnLayout {
    id: form

    required property var nodeId
    required property var params

    readonly property var graph: AppController.graph

    property int statusRev: 0
    readonly property var expressions: params.expressions ? params.expressions : ({})

    function expr(name) {
        return expressions[name] || ""
    }

    function exprError(name) {
        return statusRev >= 0 ? AppController.expressionError(nodeId, name) : ""
    }

    function setExpr(name, text) {
        const next = Object.assign({}, expressions)
        if (text.length > 0)
            next[name] = text
        else
            delete next[name]
        set({ expressions: next })
    }

    function bindModulator(name, modulatorId, lo, hi) {
        return graph.bindParam(nodeId, name, modulatorId, lo, hi)
    }

    function modulatorLabel(id) {
        void graph.revision
        const m = graph.modulators().find(x => String(x.id) === String(id))
        return m ? m.label : qsTr("Missing modulator")
    }

    Connections {
        target: AppController
        function onScriptStatusChanged() { form.statusRev++ }
    }

    function set(patch, coalesce) {
        graph.setParams(nodeId, patch, coalesce === true)
    }

    Layout.fillWidth: true
    spacing: Theme.spacingMd
}
