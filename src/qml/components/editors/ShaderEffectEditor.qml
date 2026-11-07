pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

// GLSL editor of a Shader process node. openFor(nodeId) loads the node's "code"; OK writes it back.
// The error line rechecks the code shortly after each edit.
ThemedDialog {
    id: root

    property var nodeId: 0
    property string error: ""

    title: qsTr("Shader Editor")
    preferredWidth: Theme.dialogWidthLg
    acceptOnReturn: false

    function openFor(id) {
        nodeId = id
        codeArea.text = ClipEditTools.nodeParams(id).code || ""
        error = NodeCatalog.shaderEffectError(codeArea.text)
        open()
    }

    onAccepted: {
        const code = codeArea.text.trim()
        if (code.length > 0)
            AppController.graph.setParams(nodeId, { code: code })
    }

    CodeHighlighterAttacher {
        textDocument: codeArea.textDocument
        language: "glsl"
    }

    Timer {
        id: checkTimer
        interval: 400
        onTriggered: root.error = NodeCatalog.shaderEffectError(codeArea.text)
    }

    contentItem: ColumnLayout {
        width: parent ? parent.width : root.preferredWidth
        height: Math.min(560, root.availableContentHeight)
        spacing: Theme.spacingSm

        ThemedLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Full GLSL fragment shader with its own main(). Declare u_input1..N for extra video inputs and controls in a /* @params [...] */ header.")
        }

        ThemedTextArea {
            id: codeArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            font.family: Theme.monoFontFamily
            tabStopDistance: 28
            onTextChanged: checkTimer.restart()
        }

        ThemedLabel {
            Layout.fillWidth: true
            visible: text.length > 0
            color: Theme.destructive
            wrapMode: Text.WordWrap
            text: root.error
        }
    }
}
