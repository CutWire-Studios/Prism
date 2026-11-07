pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

// Modulator node: a kind choice and only the parameters that kind uses.
// Kinds: 0 LFO, 1 Envelope, 2 Map range, 3 Smooth, 4 Random, 5 Trigger, 6 Counter, 7 Toggle, 8 Math, 9 Control In.
InspectorForm {
    id: root

    readonly property int kind: params.kind !== undefined ? params.kind : 0
    readonly property bool hasInput: kind !== 0 && kind !== 4
    readonly property bool timed: kind === 0 || kind === 4
    // Param key ("in" or "b") waiting for an OSC message to learn, "" when idle.
    property string learningKey: ""
    readonly property bool synced: !!params.sync
    readonly property var beatValues: [0.25, 0.5, 1, 2, 4, 8, 16, 32]
    readonly property var beatLabels: ["1/16", "1/8", "1/4", "1/2", qsTr("1 bar"), qsTr("2 bars"), qsTr("4 bars"), qsTr("8 bars")]
    readonly property var audioFields: [
        { text: qsTr("Level"), value: "level" }, { text: qsTr("Low"), value: "low" },
        { text: qsTr("Mid"), value: "mid" }, { text: qsTr("High"), value: "high" },
        { text: qsTr("Beat"), value: "beat" }, { text: qsTr("Kick"), value: "kick" }]
    readonly property var nt: NodePalette.nodeType

    function num(key, def) {
        const v = params[key]
        return v === undefined ? def : v
    }

    function patch(key, value) {
        const p = {}
        p[key] = value
        return p
    }

    Connections {
        target: AppController

        function onOscLearned(address) {
            if (root.learningKey.length === 0)
                return
            root.set(root.patch(root.learningKey, "osc:" + address))
            root.learningKey = ""
        }

        function onOscLearnTimedOut() {
            root.learningKey = ""
        }
    }

    Component.onDestruction: {
        if (root.learningKey.length > 0)
            AppController.cancelOscLearn()
    }

    component NumRow: StyleSliderRow {
        required property var form
        required property string key
        property real fallback: 0

        Layout.fillWidth: true
        value: form.num(key, fallback)
        expressionName: key
        expression: form.expr(key)
        expressionError: form.exprError(key)
        onEdited: v => form.set(form.patch(key, v), true)
        onExpressionEdited: (text, integer) => form.setExpr(key, text, integer)
    }

    component InputRow: ColumnLayout {
        id: row

        required property var form
        required property string key
        property string label: ""

        readonly property var source: {
            void form.graph.revision
            return form.graph.paramSource(form.nodeId, key)
        }
        readonly property string spec: String(form.params[key] === undefined ? "" : form.params[key])
        readonly property int mode: spec.startsWith("osc:") ? 2 : (spec.length > 0 ? 1 : 0)
        readonly property string fieldKey: key + "Field"
        readonly property string field: String(form.params[fieldKey] === undefined ? "" : form.params[fieldKey])
        readonly property int sourceType: source.connected ? source.type : -1

        Layout.fillWidth: true
        spacing: Theme.spacingXs

        StyleFieldRow {
            Layout.fillWidth: true
            label: row.label

            ThemedLabel {
                visible: row.source.connected
                Layout.fillWidth: true
                tone: "default"
                text: qsTr("From %1").arg(row.source.label)
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }
            IconButton {
                visible: row.source.connected
                glyph: "x"
                tooltip: qsTr("Disconnect")
                onClicked: row.form.graph.disconnectParam(row.form.nodeId, row.key)
            }

            ThemedComboBox {
                visible: !row.source.connected
                Layout.fillWidth: true
                model: [qsTr("None"), qsTr("Constant"), qsTr("OSC")]
                currentIndex: row.mode
                onActivated: index => row.form.set(row.form.patch(row.key, index === 0 ? "" : (index === 1 ? "0" : "osc:")))
            }
            ThemedTextField {
                visible: !row.source.connected && row.mode === 1
                Layout.preferredWidth: 70
                text: row.spec
                validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
                onEditingFinished: if (text !== row.spec) row.form.set(row.form.patch(row.key, text.length > 0 ? text : "0"))
            }
            ThemedButton {
                visible: !row.source.connected && row.mode === 2
                text: row.form.learningKey === row.key ? qsTr("Waiting for OSC\u2026") : qsTr("Learn")
                tooltip: row.spec.length > 4 ? row.spec.substring(4) : qsTr("Use the next incoming OSC message as the input")
                onClicked: {
                    if (row.form.learningKey === row.key) {
                        AppController.cancelOscLearn()
                        row.form.learningKey = ""
                    } else {
                        row.form.learningKey = row.key
                        AppController.startOscLearn()
                    }
                }
            }
        }

        StyleFieldRow {
            Layout.fillWidth: true
            visible: row.sourceType === row.form.nt.AudioAnalyzer
            label: qsTr("Field")
            ThemedComboBox {
                Layout.fillWidth: true
                model: row.form.audioFields.map(o => o.text)
                currentIndex: Math.max(0, row.form.audioFields.findIndex(o => o.value === row.field))
                onActivated: index => row.form.set(row.form.patch(row.fieldKey, row.form.audioFields[index].value))
            }
        }

        StyleFieldRow {
            Layout.fillWidth: true
            visible: row.sourceType === row.form.nt.Script
            label: qsTr("Field")
            ThemedTextField {
                Layout.fillWidth: true
                text: row.field
                placeholderText: qsTr("Field name")
                onEditingFinished: if (text !== row.field) row.form.set(row.form.patch(row.fieldKey, text))
            }
        }

        ThemedLabel {
            Layout.fillWidth: true
            visible: !row.source.connected && row.form.kind !== 9
            text: qsTr("or wire a node into the \"%1\" socket").arg(row.key)
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        label: qsTr("Kind")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("LFO"), qsTr("Envelope follower"), qsTr("Map range"), qsTr("Smooth / Lag"),
                    qsTr("Random"), qsTr("Trigger"), qsTr("Counter"), qsTr("Toggle"), qsTr("Math"), qsTr("Control In")]
            currentIndex: root.kind
            onActivated: index => root.set({ kind: index })
        }
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 0
        label: qsTr("Shape")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Sine"), qsTr("Triangle"), qsTr("Saw"), qsTr("Square"), qsTr("Pulse"), qsTr("Random steps")]
            currentIndex: root.num("shape", 0)
            onActivated: index => root.set({ shape: index })
        }
    }

    ThemedSwitch {
        visible: root.kind === 4
        text: qsTr("Smooth")
        checked: !!root.params.smooth
        onToggled: root.set({ smooth: checked })
    }

    ThemedSwitch {
        visible: root.timed
        text: qsTr("Sync to BPM")
        checked: root.synced
        onToggled: root.set({ sync: checked })
    }

    NumRow {
        form: root
        key: "rateHz"
        fallback: 1
        visible: root.timed && !root.synced
        label: qsTr("Rate")
        from: 0.01
        to: 20
        step: 0.01
        decimals: 2
        unit: "Hz"
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.timed && root.synced
        label: qsTr("Rate")
        ThemedComboBox {
            Layout.fillWidth: true
            model: root.beatLabels
            currentIndex: Math.max(0, root.beatValues.indexOf(root.num("rateBeats", 1)))
            onActivated: index => root.set({ rateBeats: root.beatValues[index] })
        }
    }

    NumRow {
        form: root
        key: "phase"
        visible: root.kind === 0
        label: qsTr("Phase")
        from: 0
        to: 1
        step: 0.01
        decimals: 2
    }

    NumRow {
        form: root
        key: "width"
        fallback: 0.5
        visible: root.kind === 0 && root.num("shape", 0) === 4
        label: qsTr("Width")
        from: 0
        to: 1
        step: 0.01
        decimals: 2
    }

    NumRow {
        form: root
        key: "seed"
        fallback: 1
        visible: root.kind === 4 || (root.kind === 0 && root.num("shape", 0) === 5)
        label: qsTr("Seed")
        from: 0
        to: 999
        step: 1
        decimals: 0
    }

    NumRow {
        form: root
        key: "min"
        visible: root.timed || root.kind === 9
        label: qsTr("Min")
        from: -1000
        to: 1000
        step: 0.01
        decimals: 2
    }

    NumRow {
        form: root
        key: "max"
        fallback: 1
        visible: root.timed || root.kind === 9
        label: qsTr("Max")
        from: -1000
        to: 1000
        step: 0.01
        decimals: 2
    }

    InputRow {
        form: root
        key: "in"
        visible: root.hasInput
        label: root.kind === 8 ? qsTr("A") : qsTr("Input")
    }

    InputRow {
        form: root
        key: "b"
        visible: root.kind === 8
        label: qsTr("B")
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 8
        label: qsTr("Operation")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Add"), qsTr("Subtract"), qsTr("Multiply"), qsTr("Divide"), qsTr("Min"),
                    qsTr("Max"), qsTr("Power"), qsTr("Abs(A)")]
            currentIndex: root.num("op", 0)
            onActivated: index => root.set({ op: index })
        }
    }

    NumRow {
        form: root
        key: "attackMs"
        fallback: 10
        visible: root.kind === 1
        label: qsTr("Attack")
        from: 0
        to: 2000
        step: 1
        decimals: 0
        unit: "ms"
    }

    NumRow {
        form: root
        key: "releaseMs"
        fallback: 200
        visible: root.kind === 1
        label: qsTr("Release")
        from: 0
        to: 5000
        step: 1
        decimals: 0
        unit: "ms"
    }

    NumRow {
        form: root
        key: "timeMs"
        fallback: 200
        visible: root.kind === 3
        label: qsTr("Time")
        from: 0
        to: 5000
        step: 1
        decimals: 0
        unit: "ms"
    }

    NumRow {
        form: root
        key: "inMin"
        visible: root.kind === 2
        label: qsTr("In min")
        from: -1000
        to: 1000
        step: 0.01
        decimals: 2
    }

    NumRow {
        form: root
        key: "inMax"
        fallback: 1
        visible: root.kind === 2
        label: qsTr("In max")
        from: -1000
        to: 1000
        step: 0.01
        decimals: 2
    }

    NumRow {
        form: root
        key: "outMin"
        visible: root.kind === 2
        label: qsTr("Out min")
        from: -1000
        to: 1000
        step: 0.01
        decimals: 2
    }

    NumRow {
        form: root
        key: "outMax"
        fallback: 1
        visible: root.kind === 2
        label: qsTr("Out max")
        from: -1000
        to: 1000
        step: 0.01
        decimals: 2
    }

    ThemedSwitch {
        visible: root.kind === 2 || root.kind === 9
        text: qsTr("Clamp")
        checked: root.params.clamp !== false
        onToggled: root.set({ clamp: checked })
    }

    StyleFieldRow {
        Layout.fillWidth: true
        visible: root.kind === 2
        label: qsTr("Curve")
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Linear"), qsTr("Ease in"), qsTr("Ease out")]
            currentIndex: root.num("curve", 0)
            onActivated: index => root.set({ curve: index })
        }
    }

    NumRow {
        form: root
        key: "threshold"
        fallback: 0.5
        visible: root.kind >= 5 && root.kind <= 7
        label: qsTr("Threshold")
        from: -1000
        to: 1000
        step: 0.01
        decimals: 2
    }

    NumRow {
        form: root
        key: "durationMs"
        fallback: 100
        visible: root.kind === 5
        label: qsTr("Pulse")
        from: 1
        to: 5000
        step: 1
        decimals: 0
        unit: "ms"
    }

    NumRow {
        form: root
        key: "steps"
        fallback: 4
        visible: root.kind === 6
        label: qsTr("Wrap at")
        from: 1
        to: 64
        step: 1
        decimals: 0
    }
}
