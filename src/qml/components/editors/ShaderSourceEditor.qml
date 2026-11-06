pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// GLSL source editor for Input nodes with a shader source. The inspector opens it through the
// editor router with openFor(nodeId); saving writes the code into the node's source descriptor.
// openForNew() creates a new Input node on accept instead.
ThemedDialog {
    id: root

    onClosed: AppController.cancelPendingSourcePos()

    title: qsTr("Shader Editor")
    preferredWidth: Theme.dialogWidthLg
    acceptOnReturn: false

    property var nodeId: 0
    property bool _loadingPreset: false

    readonly property int presetCount: preview.presetNames.length

    function applyPreset(row) {
        const code = preview.presetCode(row)
        if (code.length > 0)
            codeArea.text = code
        preview.refresh(codeArea.text)
    }

    function openForNew() {
        openFor(0)
    }

    function openFor(id) {
        nodeId = id
        const code = id === 0 ? "" : graphParams.params(id).source.shaderCode || ""

        _loadingPreset = true
        if (code.length > 0) {
            const idx = preview.presetIndexFor(code)
            presetList.currentIndex = idx < 0 ? presetCount : idx
        } else {
            presetList.currentIndex = 0
        }
        _loadingPreset = false

        if (code.length > 0) {
            codeArea.text = code
            preview.refresh(code)
        } else {
            applyPreset(0)
        }
        open()
    }

    onAccepted: {
        const code = codeArea.text.trim()
        if (code.length === 0)
            return
        if (root.nodeId === 0) {
            SourcePrompts.addShader(code)
            return
        }
        const source = Object.assign({}, graphParams.params(root.nodeId).source, { shaderCode: code })
        AppController.graph.setSource(root.nodeId, source)
    }

    AudioGraphParams {
        id: graphParams
        controller: AppController.graph
    }

    CodeHighlighterAttacher {
        textDocument: codeArea.textDocument
        language: "glsl"
    }

    contentItem: RowLayout {
        width: parent ? parent.width : root.preferredWidth
        height: Math.min(600, root.availableContentHeight)
        spacing: Theme.spacingXl

        ColumnLayout {
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            spacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("Presets")
            }

            ListView {
                id: presetList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: preview.presetNames.concat([qsTr("Custom Shader")])
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                onCurrentIndexChanged: {
                    if (!root._loadingPreset)
                        root.applyPreset(currentIndex)
                }

                delegate: ItemDelegate {
                    id: presetRow
                    required property string modelData
                    required property int index

                    width: presetList.width
                    height: 32
                    leftPadding: Theme.spacingLg
                    rightPadding: Theme.spacingLg
                    highlighted: index === presetList.currentIndex
                    hoverEnabled: true
                    onClicked: presetList.currentIndex = index

                    background: Rectangle {
                        radius: Theme.c.themedMenuItem.radius
                        color: presetRow.highlighted ? Theme.c.themedMenuItem.backgroundHighlighted
                                                     : "transparent"
                    }

                    contentItem: Text {
                        text: presetRow.modelData
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        color: Theme.c.themedMenuItem.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: presetRow.highlighted ? Font.Medium : Font.Normal
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("GLSL Fragment Shader")
            }

            ThemedTextArea {
                id: codeArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                font.family: Theme.monoFontFamily
                tabStopDistance: 28
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLg

                ShaderPreview {
                    id: preview
                    Layout.preferredWidth: 320
                    Layout.preferredHeight: 180
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacingSm

                    ThemedButton {
                        text: qsTr("Refresh Preview")
                        variant: "secondary"
                        onClicked: preview.refresh(codeArea.text)
                    }

                    ThemedLabel {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        color: Theme.destructive
                        text: preview.error
                    }
                }
            }
        }
    }
}
