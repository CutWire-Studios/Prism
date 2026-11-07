pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

Popup {
    id: root

    property string label: ""
    property var nodeId: 0
    property string paramName: ""
    property string expression: ""
    property string expressionError: ""
    property string sourceLabel: ""
    property bool connected: false
    property real from: 0
    property real to: 1
    property string unit: ""
    // Raw parameter units per displayed unit: the expression must return outFrom..outTo.
    property real exprScale: 1
    property bool integer: false
    property var linkForm: null
    property var input: null
    property var inputRange: ({})
    property int sourceType: -1
    // Smallest and largest value seen per variable while the popup is open: {name: [lo, hi]}.
    property var seen: ({})
    // Rebuilt only when the set of names changes, so rows are not recreated on every refresh.
    property var inputNames: []

    readonly property bool inputIsObject: input !== null && typeof input === "object"
    readonly property bool dirty: codeArea.text.trim() !== expression
    readonly property bool fromAnalyzer: sourceType === NodePalette.nodeType.AudioAnalyzer
    readonly property real outFrom: from * exprScale
    readonly property real outTo: to * exprScale

    // The variable the map() snippet reads: the analyzer's level, else the input itself.
    readonly property string mapSource: fromAnalyzer ? "input.level"
                                       : inputIsObject ? "input." + (Object.keys(input)[0] || "") : "input"
    readonly property var mapSourceRange: {
        const r = rangeOf(mapSource)
        return r ? r : [0, 1]
    }
    readonly property string mapSnippet: "map(%1, %2, %3, %4, %5)".arg(mapSource)
        .arg(fmt(mapSourceRange[0])).arg(fmt(mapSourceRange[1])).arg(fmt(outFrom)).arg(fmt(outTo))

    readonly property var analyzerHints: ({
        low: qsTr("Bass: kick drums, bass lines"),
        mid: qsTr("Mids: vocals, guitars, snares"),
        high: qsTr("Treble: hi-hats, cymbals"),
        level: qsTr("Overall loudness"),
        beat: qsTr("Jumps to 1 on each beat of the song's tempo, falls back to 0 in about ⅓ s"),
        kick: qsTr("Jumps to 1 on each kick drum hit, falls back to 0 in about ⅓ s"),
        tempo: qsTr("Song tempo in BPM, 0 until locked on"),
        hasAudio: qsTr("true while audio is playing")
    })

    signal applied(string text)
    signal linkRequested()

    function fmt(v) {
        if (v === null || v === undefined)
            return "—"
        if (typeof v === "number")
            return String(Math.round(v * 1000) / 1000)
        return String(v)
    }

    function refreshInput() {
        input = AppController.expressionInput(nodeId, paramName)
        inputRange = AppController.expressionInputRange(nodeId, paramName)
        const next = Object.assign({}, seen)
        const note = (name, v) => {
            if (typeof v !== "number" || !isFinite(v))
                return
            const r = next[name]
            next[name] = r ? [Math.min(r[0], v), Math.max(r[1], v)] : [v, v]
        }
        if (inputIsObject) {
            for (const k of Object.keys(input))
                note("input." + k, input[k])
        } else {
            note("input", input)
        }
        seen = next
        const names = !connected ? [] : ["input"].concat(inputIsObject ? Object.keys(input).map(k => "input." + k) : [])
        if (names.join(",") !== inputNames.join(","))
            inputNames = names
    }

    function inputValue(name) {
        if (name === "input")
            return inputIsObject ? null : input
        return inputIsObject ? input[name.slice(6)] : null
    }

    // Declared range of a variable, or null when only observed values are known.
    function rangeOf(name) {
        if (name === "input" && inputRange.min !== undefined)
            return [inputRange.min, inputRange.max]
        if (name.startsWith("input.") && inputRange.fields) {
            const f = inputRange.fields[name.slice(6)]
            if (f)
                return [f.min, f.max]
        }
        return null
    }

    function rangeText(name) {
        const r = rangeOf(name)
        if (r)
            return qsTr("range %1 to %2").arg(fmt(r[0])).arg(fmt(r[1]))
        const s = seen[name]
        if (s)
            return qsTr("seen %1 to %2 so far").arg(fmt(s[0])).arg(fmt(s[1]))
        return ""
    }

    function inputHint(name) {
        if (name === "input")
            return inputIsObject ? qsTr("The connected table; use the fields below")
                                 : qsTr("Value from %1").arg(sourceLabel)
        const key = name.slice(6)
        if (fromAnalyzer && analyzerHints[key] !== undefined)
            return analyzerHints[key]
        return ""
    }


    function insertText(s) {
        codeArea.insert(codeArea.cursorPosition, s)
        codeArea.forceActiveFocus()
    }

    function apply() {
        applied(codeArea.text.trim())
        close()
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(560, parent ? parent.width - 2 * Theme.spacingXl : 560)
    padding: Theme.spacingXl
    closePolicy: Popup.CloseOnEscape

    onAboutToShow: {
        codeArea.text = expression
        seen = ({})
        refreshInput()
    }
    onOpened: codeArea.forceActiveFocus()

    Timer {
        interval: 100
        repeat: true
        running: root.visible
        onTriggered: root.refreshInput()
    }

    component VariableRow: RowLayout {
        id: row

        property string varName
        property var varValue: null
        property string range: ""
        property string hint: ""

        Layout.fillWidth: true
        spacing: Theme.spacingMd

        Item {
            Layout.preferredWidth: 150
            Layout.preferredHeight: rowChip.implicitHeight
            Layout.alignment: Qt.AlignTop

            ThemedChip {
                id: rowChip
                width: Math.min(implicitWidth, parent.width)
                text: row.varName
                variant: "outline"
                tooltip: row.varName
                onClicked: root.insertText(row.varName)
            }
        }

        ThemedLabel {
            Layout.preferredWidth: 56
            Layout.alignment: Qt.AlignTop
            font.family: Theme.monoFontFamily
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            text: row.varValue === null || row.varValue === undefined || typeof row.varValue === "object"
                  ? "" : root.fmt(row.varValue)
        }

        ThemedLabel {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            wrapMode: Text.Wrap
            color: Theme.mutedForeground
            text: [row.range, row.hint].filter(s => s.length > 0).join(" · ")
        }
    }

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.panelAccent
        border.width: 1
        border.color: Theme.panelBorder
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMd

        ThemedLabel {
            Layout.fillWidth: true
            text: qsTr("Drive %1").arg(root.label)
            font.weight: Font.DemiBold
            font.pixelSize: Theme.fontSizeBase
        }

        ThemedLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Output: the expression must return a number from %1 to %2")
                  .arg(root.fmt(root.outFrom)).arg(root.fmt(root.outTo))
                  + (root.exprScale !== 1 || root.unit.length > 0
                     ? qsTr(" (shown as %1 to %2%3)").arg(root.fmt(root.from)).arg(root.fmt(root.to))
                       .arg(root.unit.length > 0 ? " " + root.unit : "")
                     : "")
                  + (root.integer ? qsTr(", rounded to whole numbers") : "")
                  + qsTr(". Values outside are not clamped.")
        }

        RowLayout {
            visible: !root.connected
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            ThemedLabel {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.warning
                text: qsTr("Nothing connected. Wire a modulator, analyzer or script into the %1 socket on the node, or").arg(root.label)
            }

            ThemedButton {
                text: qsTr("Link…")
                visible: root.linkForm !== null
                onClicked: root.linkRequested()
            }
        }

        ThemedTextArea {
            id: codeArea
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            font.family: Theme.monoFontFamily
            wrapMode: TextArea.Wrap
            placeholderText: qsTr("e.g. %1").arg(root.mapSnippet)
            errorText: root.expressionError
            Keys.onPressed: event => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        && (event.modifiers & Qt.ControlModifier)) {
                    root.apply()
                    event.accepted = true
                }
            }

            CodeHighlighterAttacher {
                textDocument: codeArea.textDocument
                language: "lua"
            }
        }

        ThemedLabel {
            visible: root.expressionError.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.destructive
            text: root.expressionError
        }

        ThemedLabel {
            text: qsTr("Variables (click to insert)")
            color: Theme.mutedForeground
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Repeater {
                model: root.inputNames

                delegate: VariableRow {
                    required property string modelData
                    varName: modelData
                    varValue: root.inputValue(modelData)
                    range: root.rangeText(modelData)
                    hint: root.inputHint(modelData)
                }
            }

            VariableRow {
                varName: "t"
                hint: qsTr("Seconds since the scene started; keeps growing")
            }
            VariableRow {
                varName: "bpm"
                hint: qsTr("Tempo in beats per minute, e.g. 120")
            }
            VariableRow {
                varName: "beat"
                hint: qsTr("Beat count; keeps growing. beat % 1 goes 0 to 1 every beat")
            }

            Repeater {
                model: AppController.globals

                delegate: VariableRow {
                    required property string name
                    required property real value
                    required property real min
                    required property real max
                    varName: "globals." + name
                    varValue: value
                    range: qsTr("range %1 to %2").arg(root.fmt(min)).arg(root.fmt(max))
                    hint: qsTr("Project global")
                }
            }
        }

        ThemedLabel {
            text: qsTr("Helpers (click to insert)")
            color: Theme.mutedForeground
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ThemedChip {
                text: root.mapSnippet
                variant: "outline"
                tooltip: qsTr("map(x, inMin, inMax, outMin, outMax): rescale x from the input range to this parameter's range, clamped")
                onClicked: root.insertText(root.mapSnippet)
            }
            ThemedChip {
                text: "clamp(x, lo, hi)"
                variant: "outline"
                tooltip: qsTr("Keep x between lo and hi")
                onClicked: root.insertText("clamp(, %1, %2)".arg(root.fmt(root.outFrom)).arg(root.fmt(root.outTo)))
            }
            ThemedChip {
                text: "lerp(a, b, t)"
                variant: "outline"
                tooltip: qsTr("Blend from a (t = 0) to b (t = 1)")
                onClicked: root.insertText("lerp(%1, %2, )".arg(root.fmt(root.outFrom)).arg(root.fmt(root.outTo)))
            }
            ThemedChip {
                text: "invlerp(a, b, x)"
                variant: "outline"
                tooltip: qsTr("Where x sits between a and b: 0 at a, 1 at b")
                onClicked: root.insertText("invlerp(, , )")
            }
            ThemedChip {
                text: "smoothstep(e0, e1, x)"
                variant: "outline"
                tooltip: qsTr("Eased 0 to 1 as x goes from e0 to e1, clamped")
                onClicked: root.insertText("smoothstep(, , )")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            ThemedButton {
                text: qsTr("Remove")
                variant: "destructive"
                onClicked: {
                    root.applied("")
                    root.close()
                }
            }

            Item { Layout.fillWidth: true }

            ThemedButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }

            ThemedButton {
                text: qsTr("Apply")
                variant: "primary"
                onClicked: root.apply()
            }
        }
    }
}
