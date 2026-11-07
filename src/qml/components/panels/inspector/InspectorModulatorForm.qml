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
    readonly property var inputOptions: {
        void AppController.graph.revision
        const opts = [{ text: qsTr("None"), value: "" },
                      { text: qsTr("Audio low"), value: "low" },
                      { text: qsTr("Audio mid"), value: "mid" },
                      { text: qsTr("Audio high"), value: "high" },
                      { text: qsTr("Audio level"), value: "level" },
                      { text: qsTr("Audio beat"), value: "beat" }]
        for (const m of AppController.graph.modulators()) {
            if (m.id !== root.nodeId)
                opts.push({ text: qsTr("Modulator: %1").arg(m.label), value: "mod:" + m.id })
        }
        for (const c of ["0", "0.5", "1", "2"])
            opts.push({ text: qsTr("Constant %1").arg(c), value: c })
        for (const k of ["in", "b"]) {
            const cur = String(params[k] === undefined ? "" : params[k])
            if (cur.startsWith("osc:") && !opts.some(o => o.value === cur))
                opts.push({ text: qsTr("OSC %1").arg(cur.substring(4)), value: cur })
        }
        return opts
    }

    function num(key, def) {
        const v = params[key]
        return v === undefined ? def : v
    }

    function patch(key, value) {
        const p = {}
        p[key] = value
        return p
    }

    function optionIndex(key) {
        const cur = String(params[key] === undefined ? "" : params[key])
        return inputOptions.findIndex(o => o.value === cur)
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
        onExpressionEdited: text => form.setExpr(key, text)
    }

    component InputRow: StyleFieldRow {
        required property var form
        required property string key

        Layout.fillWidth: true
        ThemedComboBox {
            Layout.fillWidth: true
            model: form.inputOptions.map(o => o.text)
            currentIndex: form.optionIndex(key)
            onActivated: index => form.set(form.patch(key, form.inputOptions[index].value))
        }
        ThemedButton {
            text: form.learningKey === key ? qsTr("Waiting for OSC\u2026") : qsTr("Learn")
            tooltip: qsTr("Use the next incoming OSC message as the input")
            onClicked: {
                if (form.learningKey === key) {
                    AppController.cancelOscLearn()
                    form.learningKey = ""
                } else {
                    form.learningKey = key
                    AppController.startOscLearn()
                }
            }
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
