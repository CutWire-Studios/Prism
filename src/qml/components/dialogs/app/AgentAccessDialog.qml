import QtQuick
import Prism

ThemedDialog {
    id: root

    title: qsTr("Agent access")
    showAccept: false
    rejectText: qsTr("Close")
    preferredWidth: Theme.dialogWidthLg

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthLg

        ThemedLabel {
            width: parent.width
            text: qsTr("Starts a localhost MCP server so Cursor, Claude Code, or other agents can drive this mixer. Not saved. Stops when Prism quits or you turn this off. Any local process with the session token has full mixer access.")
        }

        ThemedCheckBox {
            text: qsTr("Enable agent access for this session")
            checked: McpAccess.enabled
            onToggled: McpAccess.enabled = checked
        }

        ThemedLabel {
            width: parent.width
            visible: McpAccess.error.length > 0
            color: Theme.destructive
            text: McpAccess.error
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: McpAccess.enabled

            ThemedLabel {
                width: parent.width
                tone: "default"
                text: qsTr("Listening on %1").arg(McpAccess.url)
            }

            ThemedTextField {
                width: parent.width
                readOnly: true
                text: McpAccess.url
            }

            ThemedTextField {
                width: parent.width
                readOnly: true
                text: McpAccess.token
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingMd

                ThemedButton {
                    text: qsTr("Copy Cursor snippet")
                    onClicked: McpAccess.copyCursor()
                }

                ThemedButton {
                    text: qsTr("Copy Claude command")
                    onClicked: McpAccess.copyClaude()
                }

                ThemedButton {
                    text: qsTr("Copy stdio snippet")
                    onClicked: McpAccess.copyStdio()
                }

                ThemedButton {
                    text: qsTr("Copy agent guide")
                    onClicked: McpAccess.copyGuide()
                }
            }

            ThemedLabel {
                width: parent.width
                text: qsTr("Pinned endpoints: /mcp/sources, /mcp/decks, /mcp/transition, /mcp/panic, /mcp/output, /mcp/session.\nWorkflow: catalog, toolbox({name}), apply({ops}).")
            }
        }
    }
}
