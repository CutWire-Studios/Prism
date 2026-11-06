pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Prism

// Pedalboard for an Audio FX node. Edits reach the running audio live; OK keeps them as one undo
// step and Cancel puts the node back.
ThemedDialog {
    id: root

    property alias pb: board
    property string _irTarget: ""
    property string _search: ""

    title: qsTr("Audio FX")
    preferredWidth: 1100
    acceptOnReturn: false

    function openFor(nodeId, kind) {
        board.bind(nodeId)
        _search = ""
        searchField.text = ""
        open()
    }

    function localPath(url) {
        return AppController.localPath(url)
    }

    function chooseIr(pedalId) {
        _irTarget = pedalId
        irDialog.open()
    }

    function addFromRow(row) {
        if (row.kind === "pedal" && row.arg === "convolution") {
            _irTarget = ""
            irDialog.open()
            return
        }
        board.addFromPalette(row.kind, row.arg)
    }

    function openRouteMenu(pedalId, knobId) {
        const entries = []
        const mods = board.board.modulators || []
        if (mods.length === 0) {
            entries.push({ text: qsTr("(add a modulator first)"), enabled: false, run: null })
        } else {
            for (const m of mods) {
                const from = m.id
                entries.push({
                    text: qsTr("Modulate by %1 (%2)").arg(m.label).arg(from),
                    enabled: true,
                    run: () => board.addRoute(from, pedalId, knobId)
                })
            }
        }
        for (const m of mods) {
            for (const r of m.routes) {
                if (r.target === pedalId && r.knob === knobId) {
                    const routeId = r.id
                    entries.push({
                        text: qsTr("Remove modulation (%1)").arg(m.id),
                        enabled: true,
                        run: () => board.removeRoute(routeId)
                    })
                }
            }
        }
        routeMenu.entries = entries
        routeMenu.popup()
    }

    function groups(text) {
        const needle = text.trim().toLowerCase()
        const byTitle = {}
        const out = []
        for (const row of board.palette) {
            const hit = needle.length === 0 || row.group.toLowerCase().indexOf(needle) >= 0
                        || row.label.toLowerCase().indexOf(needle) >= 0
            if (!hit)
                continue
            if (!byTitle[row.group]) {
                byTitle[row.group] = { title: row.group, items: [] }
                out.push(byTitle[row.group])
            }
            byTitle[row.group].items.push(row)
        }
        return out
    }

    onAccepted: board.commit()
    onClosed: board.cancel()

    PedalboardModel {
        id: board
    }

    FileDialog {
        id: irDialog
        title: qsTr("Select impulse response")
        nameFilters: [qsTr("WAV files (*.wav)"), qsTr("All files (*)")]
        onAccepted: {
            const path = root.localPath(selectedFile)
            if (root._irTarget.length > 0)
                board.setPedalIr(root._irTarget, path)
            else
                board.addFromPalette("pedal", "convolution", path)
        }
    }

    ThemedContextMenu {
        id: routeMenu

        property var entries: []

        Instantiator {
            model: routeMenu.entries
            delegate: ThemedMenuItem {
                required property var modelData
                text: modelData.text
                enabled: modelData.enabled
                onTriggered: if (modelData.run) modelData.run()
            }
            onObjectAdded: (index, object) => routeMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => routeMenu.removeItem(object)
        }
    }

    contentItem: RowLayout {
        spacing: Theme.spacingXl
        implicitHeight: 520

        ColumnLayout {
            Layout.preferredWidth: 220
            Layout.maximumWidth: 220
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            ThemedTextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Search effects")
                onTextChanged: root._search = text
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: paletteColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                Column {
                    id: paletteColumn
                    width: parent.width - Theme.spacingLg
                    spacing: Theme.spacingSm

                    Repeater {
                        model: root.groups(root._search)

                        Column {
                            id: group
                            required property var modelData
                            width: paletteColumn.width

                            ThemedLabel {
                                text: group.modelData.title.toUpperCase()
                                font.weight: Font.Medium
                                topPadding: Theme.spacingMd
                            }

                            Repeater {
                                model: group.modelData.items

                                Rectangle {
                                    id: rowItem
                                    required property var modelData
                                    width: group.width
                                    height: Theme.controlHeightSm
                                    radius: Theme.radiusSm
                                    color: area.containsMouse ? Theme.popoverHover : "transparent"

                                    ThemedLabel {
                                        anchors.verticalCenter: parent.verticalCenter
                                        x: Theme.spacingMd
                                        width: parent.width - 2 * Theme.spacingMd
                                        tone: "default"
                                        wrapMode: Text.NoWrap
                                        elide: Text.ElideRight
                                        text: rowItem.modelData.label
                                    }

                                    MouseArea {
                                        id: area
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.addFromRow(rowItem.modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            ThemedLabel {
                text: qsTr("SIGNAL CHAIN")
                font.weight: Font.Medium
            }

            Flickable {
                id: boardFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 300
                clip: true
                contentWidth: chain.implicitWidth + 2 * Theme.spacingLg
                contentHeight: chain.implicitHeight + 2 * Theme.spacingLg
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: AppScrollBar { }
                ScrollBar.vertical: AppScrollBar { }

                Rectangle {
                    width: Math.max(boardFlick.width, boardFlick.contentWidth)
                    height: Math.max(boardFlick.height, boardFlick.contentHeight)
                    radius: Theme.radiusMd
                    color: "transparent"
                    border.width: board.targetSplit.length === 0 ? 2 : Theme.borderWidth
                    border.color: board.targetSplit.length === 0 ? Theme.primary : Theme.panelSecondaryBorder

                    MouseArea {
                        anchors.fill: parent
                        onClicked: board.setTarget("", 0)
                    }

                    PedalChain {
                        id: chain
                        x: Theme.spacingLg
                        y: Theme.spacingLg
                        top: true
                        items: board.board.items || []
                        host: root
                    }
                }
            }

            ThemedLabel {
                text: qsTr("MODULATORS")
                font.weight: Font.Medium
            }

            ThemedLabel {
                visible: (board.board.modulators || []).length === 0
                text: qsTr("No modulators. Add one from the palette, then right-click a knob to modulate it.")
            }

            Flickable {
                id: modFlick
                visible: (board.board.modulators || []).length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                clip: true
                contentWidth: modRow.implicitWidth
                contentHeight: modRow.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: AppScrollBar { }
                ScrollBar.vertical: AppScrollBar { }

                Row {
                    id: modRow
                    spacing: Theme.spacingLg

                    Repeater {
                        model: board.board.modulators || []

                        ModulatorCard {
                            required property var modelData
                            entry: modelData
                            sources: board.board.sources || []
                            host: root
                        }
                    }
                }
            }

            ThemedLabel {
                Layout.fillWidth: true
                visible: board.status.length > 0
                tone: "default"
                color: Theme.destructive
                text: board.status
            }
        }
    }
}
