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
    property var linkForm: null
    property var input: null

    readonly property bool inputIsObject: input !== null && typeof input === "object"
    readonly property bool dirty: codeArea.text.trim() !== expression

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
    width: Math.min(480, parent ? parent.width - 2 * Theme.spacingXl : 480)
    padding: Theme.spacingXl
    closePolicy: Popup.CloseOnEscape

    onAboutToShow: {
        codeArea.text = expression
        refreshInput()
    }
    onOpened: codeArea.forceActiveFocus()

    Timer {
        interval: 100
        repeat: true
        running: root.visible
        onTriggered: root.refreshInput()
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
            visible: root.connected
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Input: %1 = %2").arg(root.sourceLabel).arg(root.inputIsObject
                  ? JSON.stringify(root.input) : root.fmt(root.input))
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
            placeholderText: qsTr("e.g. input * 100")
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
            text: qsTr("Variables")
            color: Theme.mutedForeground
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ThemedChip {
                text: "input"
                variant: "outline"
                tooltip: root.inputIsObject ? qsTr("The connected table") : qsTr("Value: %1").arg(root.fmt(root.input))
                onClicked: root.insertText("input")
            }

            Repeater {
                model: root.inputIsObject ? Object.keys(root.input) : []

                ThemedChip {
                    required property string modelData
                    text: "input." + modelData
                    variant: "outline"
                    tooltip: qsTr("Value: %1").arg(root.fmt(root.input[modelData]))
                    onClicked: root.insertText("input." + modelData)
                }
            }

            ThemedChip {
                text: "t"
                variant: "outline"
                tooltip: qsTr("Seconds")
                onClicked: root.insertText("t")
            }
            ThemedChip {
                text: "bpm"
                variant: "outline"
                onClicked: root.insertText("bpm")
            }
            ThemedChip {
                text: "beat"
                variant: "outline"
                onClicked: root.insertText("beat")
            }

            Repeater {
                model: AppController.globals

                ThemedChip {
                    required property string name
                    required property real value
                    text: "globals." + name
                    variant: "outline"
                    tooltip: qsTr("Value: %1").arg(root.fmt(value))
                    onClicked: root.insertText("globals." + name)
                }
            }
        }

        ThemedLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.mutedForeground
            text: qsTr("Result range: %1 – %2%3. Example: %4")
                  .arg(root.from).arg(root.to).arg(root.unit.length > 0 ? " " + root.unit : "")
                  .arg(root.connected ? "input * " + (root.to - root.from)
                                      : "0.5 + 0.5 * math.sin(t)")
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
