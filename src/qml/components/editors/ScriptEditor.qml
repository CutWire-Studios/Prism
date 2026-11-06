pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// Lua script editor for Script nodes. The interval is kept as stored: periodic timing moved to
// Trigger nodes, so the field is not shown.
ThemedDialog {
    id: root

    title: qsTr("Lua Script Editor")
    preferredWidth: Theme.dialogWidthLg
    acceptOnReturn: false

    property var nodeId: 0
    property int intervalMs: 1000
    property bool _loadingPreset: false

    readonly property int presetCount: preview.presetNames.length

    function applyPreset(row) {
        const code = preview.presetCode(row)
        if (code.length > 0)
            codeArea.text = code
    }

    function openFor(id, kind) {
        nodeId = id
        const p = graphParams.params(id)
        intervalMs = p.intervalMs
        triggerCombo.currentIndex = p.triggerMode === 3 ? 0 : 1
        preview.clear()

        const code = p.luaCode || ""
        _loadingPreset = true
        if (code.length > 0) {
            const idx = preview.presetIndexFor(code)
            presetList.currentIndex = idx < 0 ? presetCount : idx
        } else {
            presetList.currentIndex = 0
        }
        _loadingPreset = false

        if (code.length > 0)
            codeArea.text = code
        else
            applyPreset(0)
        open()
    }

    onAccepted: {
        const code = codeArea.text.trim()
        if (code.length === 0)
            return
        AppController.graph.setParams(root.nodeId, {
            luaCode: code,
            triggerMode: triggerCombo.currentIndex === 0 ? 3 : 2,
            intervalMs: root.intervalMs
        })
    }

    AudioGraphParams {
        id: graphParams
        controller: AppController.graph
    }

    ScriptPreview {
        id: preview
    }

    CodeHighlighterAttacher {
        textDocument: codeArea.textDocument
        language: "lua"
    }

    contentItem: RowLayout {
        width: parent ? parent.width : root.preferredWidth
        height: Math.min(560, root.availableContentHeight)
        spacing: Theme.spacingXl

        ColumnLayout {
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            spacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("Examples")
            }

            ListView {
                id: presetList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: preview.presetNames.concat([qsTr("Custom Script")])
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
                text: qsTr("Lua Script")
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

                ThemedLabel {
                    text: qsTr("Trigger:")
                    tone: "default"
                    size: "sm"
                }

                ThemedComboBox {
                    id: triggerCombo
                    model: [qsTr("On input change"), qsTr("Manual")]
                }

                Item {
                    Layout.fillWidth: true
                }

                ThemedButton {
                    text: qsTr("Run now")
                    variant: "secondary"
                    onClicked: preview.run(codeArea.text)
                }
            }

            ThemedLabel {
                text: qsTr("Output preview")
            }

            ThemedTextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                readOnly: true
                font.family: Theme.monoFontFamily
                text: preview.output
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
