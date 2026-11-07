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

    // integer: the slider only takes whole numbers, so results are rounded; else they stay floats.
    function setExpr(name, text, integer) {
        const next = Object.assign({}, expressions)
        const ints = Object.assign({}, params.expressionInts || {})
        if (text.length > 0) {
            next[name] = text
            ints[name] = integer === true
        } else {
            delete next[name]
            delete ints[name]
        }
        set({ expressions: next, expressionInts: ints })
    }

    function bindModulator(name, modulatorId, lo, hi, newKind, integer) {
        return graph.bindParam(nodeId, name, modulatorId, lo, hi, newKind || 0, integer === true)
    }

    function setModulatorInput(modulatorId, spec) {
        graph.setParams(modulatorId, { "in": spec })
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
