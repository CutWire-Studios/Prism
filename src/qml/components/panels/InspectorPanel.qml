pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

PanelFrame {
    id: root

    property var editors: null

    readonly property var graph: AppController.graph
    readonly property var selection: graph ? graph.selection : []
    readonly property var selectedId: selection.length === 1 ? Number(selection[0]) : 0

    EmptyState {
        anchors.centerIn: parent
        visible: root.selectedId === 0
        compact: true
        glyph: "sliders-horizontal"
        title: root.selection.length > 1 ? qsTr("%1 nodes selected").arg(root.selection.length)
                                         : qsTr("Nothing selected")
        hint: root.selection.length > 1 ? "" : qsTr("Select a node in the graph to inspect it")
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: column
            width: parent.width
            spacing: 0

            Repeater {
                model: root.graph ? root.graph.nodes : null

                delegate: Item {
                    id: del

                    required property var nodeId
                    required property int type
                    required property string title
                    required property var params

                    Layout.fillWidth: true
                    Layout.preferredHeight: loader.item ? loader.item.implicitHeight : 0
                    visible: Number(nodeId) === root.selectedId

                    Loader {
                        id: loader
                        width: parent.width
                        active: del.visible
                        sourceComponent: details
                    }

                    Component {
                        id: details

                        ColumnLayout {
                            id: body

                            readonly property var nt: NodePalette.nodeType
                            readonly property var meta: NodePalette.meta[del.type]
                            readonly property bool hasEditor: (del.type === nt.Process || del.type === nt.AudioEffect)
                                                              ? NodeCatalog.effectHasEditor(del.type, del.params.effect)
                                                              : true
                            readonly property string editKind: NodePalette.editorKind(del.type, hasEditor)
                            readonly property bool hasSlots: del.type === nt.Layer || del.type === nt.AbSelect
                                                             || del.type === nt.AudioMixer
                            readonly property var slots: del.params.slots ? del.params.slots : []

                            function patchSlot(i, patch) {
                                const next = slots.slice()
                                next[i] = Object.assign({}, next[i], patch)
                                root.graph.setParams(del.nodeId, { slots: next })
                            }

                            spacing: Theme.spacingMd

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMd

                                IconGlyph {
                                    glyph: body.meta ? body.meta.icon : ""
                                    iconColor: body.meta ? body.meta.hue : Theme.c.iconGlyph.icon
                                }
                                ThemedLabel {
                                    Layout.fillWidth: true
                                    tone: "default"
                                    size: "base"
                                    text: (body.meta ? body.meta.name : "") + "  #" + del.nodeId
                                }
                                IconButton {
                                    glyph: "trash-2"
                                    tooltip: qsTr("Delete node")
                                    onClicked: root.graph.removeNodes([del.nodeId])
                                }
                            }

                            ThemedLabel { text: qsTr("Name") }
                            ThemedTextField {
                                Layout.fillWidth: true
                                text: del.title
                                onEditingFinished: {
                                    if (text !== del.title)
                                        AppController.renameNode(del.nodeId, text)
                                }
                            }

                            ThemedLabel {
                                Layout.fillWidth: true
                                visible: del.type === body.nt.Input && !!del.params.source
                                text: del.params.source ? del.params.source.path || del.params.source.displayName || "" : ""
                                elide: Text.ElideMiddle
                                wrapMode: Text.NoWrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                visible: del.type === body.nt.Input
                                spacing: Theme.spacingSm

                                ThemedButton {
                                    Layout.fillWidth: true
                                    text: qsTr("Send to Staging")
                                    onClicked: AppController.stageNode(del.nodeId)
                                }
                            }

                            ThemedLabel {
                                visible: del.type === body.nt.Input && !!del.params.hasAudio
                                text: qsTr("Has audio")
                            }

                            ThemedButton {
                                Layout.fillWidth: true
                                visible: body.editKind.length > 0
                                text: qsTr("Edit...")
                                onClicked: {
                                    if (root.editors)
                                        root.editors.open(del.nodeId, body.editKind)
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: del.type === body.nt.MasterAudioInput || del.type === body.nt.MasterAudioCapture
                                spacing: Theme.spacingSm

                                ThemedLabel {
                                    text: del.params.deviceId && del.params.deviceId.length > 0
                                          ? del.params.deviceLabel : qsTr("Default device")
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingSm

                                    ThemedSlider {
                                        id: masterVolume
                                        Layout.fillWidth: true
                                        label: qsTr("Volume")
                                        from: 0
                                        to: 100
                                        stepSize: 1
                                        valueFormatter: v => Math.round(v) + "%"
                                        onMoved: {
                                            if (!pressed)
                                                root.graph.setParams(del.nodeId, { volume: Math.round(value) })
                                        }
                                        onPressedChanged: {
                                            if (!pressed)
                                                root.graph.setParams(del.nodeId, { volume: Math.round(value) })
                                        }
                                        Binding {
                                            target: masterVolume
                                            property: "value"
                                            value: del.params.volume
                                            when: !masterVolume.pressed
                                        }
                                    }
                                    ThemedSwitch {
                                        text: qsTr("Mute")
                                        checked: !!del.params.muted
                                        onToggled: root.graph.setParams(del.nodeId, { muted: checked })
                                    }
                                }
                            }

                            Repeater {
                                model: body.hasSlots ? body.slots : []

                                delegate: ColumnLayout {
                                    id: slotRow

                                    required property var modelData
                                    required property int index

                                    Layout.fillWidth: true
                                    spacing: Theme.spacingXs

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacingSm

                                        ThemedLabel {
                                            Layout.fillWidth: true
                                            tone: "default"
                                            elide: Text.ElideRight
                                            wrapMode: Text.NoWrap
                                            text: slotRow.modelData.name && slotRow.modelData.name.length > 0
                                                  ? slotRow.modelData.name
                                                  : qsTr("Slot %1").arg(slotRow.index + 1)
                                        }
                                        ThemedButton {
                                            visible: del.type === body.nt.AbSelect
                                            text: qsTr("Stage")
                                            onClicked: AppController.stageAbSlot(del.nodeId, slotRow.index, false)
                                        }
                                        ThemedButton {
                                            visible: del.type === body.nt.AbSelect
                                            text: qsTr("Cut to Live")
                                            onClicked: AppController.stageAbSlot(del.nodeId, slotRow.index, true)
                                        }
                                        ThemedSwitch {
                                            visible: del.type === body.nt.AudioMixer
                                            text: qsTr("Mute")
                                            checked: !!slotRow.modelData.muted
                                            onToggled: body.patchSlot(slotRow.index, { muted: checked })
                                        }
                                    }
                                    ThemedSlider {
                                        id: slotVolume
                                        Layout.fillWidth: true
                                        visible: del.type === body.nt.AudioMixer
                                        label: qsTr("Volume")
                                        from: 0
                                        to: 100
                                        stepSize: 1
                                        valueFormatter: v => Math.round(v) + "%"
                                        onPressedChanged: {
                                            if (!pressed)
                                                body.patchSlot(slotRow.index, { volume: Math.round(value) })
                                        }
                                        onMoved: {
                                            if (!pressed)
                                                body.patchSlot(slotRow.index, { volume: Math.round(value) })
                                        }
                                        Binding {
                                            target: slotVolume
                                            property: "value"
                                            value: slotRow.modelData.volume
                                            when: !slotVolume.pressed
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
