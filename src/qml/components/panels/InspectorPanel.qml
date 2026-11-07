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
    readonly property real selectedGroup: graph ? Number(graph.selectedGroup) : 0
    readonly property var selectedId: selectedGroup === 0 && selection.length === 1 ? Number(selection[0]) : 0

    readonly property bool showScene: root.selection.length === 0 && AppController.selectedSceneId.length > 0

    EmptyState {
        anchors.centerIn: parent
        visible: root.selectedId === 0 && root.selectedGroup === 0 && !root.showScene
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

            InspectorSceneForm {
                Layout.fillWidth: true
                visible: root.showScene
            }

            Repeater {
                model: root.graph ? root.graph.groups : null

                delegate: ColumnLayout {
                    id: groupForm

                    required property var groupId
                    required property string name
                    required property string color
                    required property bool collapsed
                    required property int memberCount

                    Layout.fillWidth: true
                    visible: Number(groupId) === root.selectedGroup
                    spacing: Theme.spacingMd

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMd

                        IconGlyph {
                            glyph: "group"
                            iconColor: groupForm.color.length > 0 ? groupForm.color : Theme.c.iconGlyph.icon
                        }
                        ThemedLabel {
                            Layout.fillWidth: true
                            tone: "default"
                            size: "base"
                            text: qsTr("Group  #%1").arg(groupForm.groupId)
                        }
                        IconButton {
                            glyph: "trash-2"
                            tooltip: qsTr("Delete group and its nodes")
                            onClicked: root.graph.removeNodes(root.graph.groupMembers(groupForm.groupId).map(m => m.id))
                        }
                    }

                    ThemedLabel { text: qsTr("Name") }
                    ThemedTextField {
                        Layout.fillWidth: true
                        text: groupForm.name
                        onEditingFinished: {
                            if (text !== groupForm.name)
                                root.graph.setGroupProps(groupForm.groupId, { name: text })
                        }
                    }

                    ThemedLabel { text: qsTr("Colour") }
                    Row {
                        spacing: Theme.spacingSm

                        Repeater {
                            model: NodePalette.frameColors

                            delegate: Rectangle {
                                required property string modelData

                                width: 20
                                height: 20
                                radius: 10
                                color: modelData.length > 0 ? modelData : "transparent"
                                border.width: groupForm.color === modelData ? 2 : 1
                                border.color: groupForm.color === modelData ? Theme.primary : Theme.panelBorder

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.graph.setGroupProps(groupForm.groupId, { color: parent.modelData })
                                }
                            }
                        }
                    }

                    ThemedSwitch {
                        text: qsTr("Collapsed")
                        checked: groupForm.collapsed
                        onToggled: root.graph.setGroupCollapsed(groupForm.groupId, checked)
                    }

                    ThemedLabel { text: qsTr("Members (%1)").arg(groupForm.memberCount) }
                    Repeater {
                        model: {
                            void root.graph.revision
                            return root.graph.groupMembers(groupForm.groupId)
                        }

                        delegate: Rectangle {
                            id: memberRow

                            required property var modelData

                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            radius: Theme.radiusSm
                            color: memberArea.containsMouse ? Theme.panelAccent : "transparent"

                            ThemedLabel {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacingSm
                                tone: "default"
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                verticalAlignment: Text.AlignVCenter
                                text: memberRow.modelData.title + "  #" + memberRow.modelData.id
                            }
                            MouseArea {
                                id: memberArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    root.graph.setGroupCollapsed(groupForm.groupId, false)
                                    root.graph.select([memberRow.modelData.id])
                                }
                            }
                        }
                    }
                }
            }

            Repeater {
                model: root.graph ? root.graph.nodes : null

                delegate: Item {
                    id: del

                    required property var nodeId
                    required property int type
                    required property string title
                    required property var params

                    Layout.fillWidth: true
                    Layout.preferredHeight: loader.implicitHeight
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
                            readonly property string editKind: NodePalette.editorKind(del.type, del.params)
                            readonly property bool hasSlots: del.type === nt.Layer
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
                                    glyph: del.type === body.nt.Input ? NodePalette.sourceIcon(del.params.source)
                                                                      : body.meta ? body.meta.icon : ""
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

                            Loader {
                                Layout.fillWidth: true
                                active: del.type === body.nt.Input
                                visible: active
                                sourceComponent: InspectorInputForm {
                                    nodeId: del.nodeId
                                    params: del.params
                                }
                            }

                            Loader {
                                Layout.fillWidth: true
                                active: del.type === body.nt.Process
                                visible: active
                                sourceComponent: del.params.effect >= 12 ? shaderParamForm : processForm
                            }

                            Component {
                                id: processForm
                                InspectorProcessForm {
                                    nodeId: del.nodeId
                                    params: del.params
                                }
                            }

                            Component {
                                id: shaderParamForm
                                ShaderParamForm {
                                    nodeId: del.nodeId
                                    params: del.params
                                }
                            }

                            Loader {
                                Layout.fillWidth: true
                                active: del.type === body.nt.Modulator
                                visible: active
                                sourceComponent: InspectorModulatorForm {
                                    nodeId: del.nodeId
                                    params: del.params
                                }
                            }

                            Loader {
                                Layout.fillWidth: true
                                active: del.type === body.nt.Script
                                visible: active
                                sourceComponent: InspectorScriptForm {
                                    nodeId: del.nodeId
                                    params: del.params
                                }
                            }

                            Loader {
                                Layout.fillWidth: true
                                active: del.type === body.nt.MasterAudioInput || del.type === body.nt.MasterAudioCapture
                                    || del.type === body.nt.AudioAnalyzer
                                visible: active
                                sourceComponent: InspectorAudioForm {
                                    isCapture: del.type === body.nt.MasterAudioCapture
                                    isAnalyzer: del.type === body.nt.AudioAnalyzer
                                    nodeId: del.nodeId
                                    params: del.params
                                }
                            }

                            Loader {
                                Layout.fillWidth: true
                                active: del.type === body.nt.MasterAudioInput || del.type === body.nt.MasterAudioCapture
                                visible: active
                                sourceComponent: InspectorVolumeRow {
                                    nodeId: del.nodeId
                                    params: del.params
                                }
                            }

                            Loader {
                                Layout.fillWidth: true
                                active: del.type === body.nt.MasterAudioOutput
                                visible: active
                                sourceComponent: InspectorOutputForm {
                                    nodeId: del.nodeId
                                    params: del.params
                                }
                            }

                            Loader {
                                Layout.fillWidth: true
                                active: del.type === body.nt.AudioEffect
                                visible: active
                                sourceComponent: InspectorPedalboardForm {
                                    nodeId: del.nodeId
                                    params: del.params
                                }
                            }

                            Loader {
                                Layout.fillWidth: true
                                active: del.type === body.nt.Layer
                                visible: active
                                sourceComponent: ColumnLayout {
                                    spacing: Theme.spacingMd

                                    ThemedSwitch {
                                        text: qsTr("Custom canvas size")
                                        checked: !!del.params.canvasCustom
                                        onToggled: root.graph.setParams(del.nodeId, { canvasCustom: checked })
                                    }
                                    StyleFieldRow {
                                        Layout.fillWidth: true
                                        visible: !!del.params.canvasCustom
                                        label: qsTr("Width")
                                        ThemedNumberField {
                                            Layout.fillWidth: true
                                            from: 1
                                            to: 7680
                                            unit: "px"
                                            value: del.params.canvasW
                                            onEdited: v => root.graph.setParams(del.nodeId, { canvasW: Math.round(v) })
                                        }
                                    }
                                    StyleFieldRow {
                                        Layout.fillWidth: true
                                        visible: !!del.params.canvasCustom
                                        label: qsTr("Height")
                                        ThemedNumberField {
                                            Layout.fillWidth: true
                                            from: 1
                                            to: 4320
                                            unit: "px"
                                            value: del.params.canvasH
                                            onEdited: v => root.graph.setParams(del.nodeId, { canvasH: Math.round(v) })
                                        }
                                    }
                                }
                            }

                            ThemedSwitch {
                                visible: del.type === body.nt.Process || del.type === body.nt.AudioEffect
                                text: qsTr("Bypass")
                                checked: !!del.params.bypass
                                onToggled: root.graph.setParams(del.nodeId, { bypass: checked })
                            }

                            ThemedButton {
                                Layout.fillWidth: true
                                visible: body.editKind.length > 0
                                text: qsTr("Open editor…")
                                onClicked: {
                                    if (root.editors)
                                        root.editors.open(del.nodeId, body.editKind)
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

                                    ThemedTextField {
                                        Layout.fillWidth: true
                                        visible: del.type === body.nt.AudioMixer
                                        text: slotRow.modelData.name || ""
                                        placeholderText: qsTr("in %1").arg(slotRow.index + 1)
                                        onEditingFinished: {
                                            if (text !== (slotRow.modelData.name || ""))
                                                body.patchSlot(slotRow.index, { name: text })
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacingSm

                                        ThemedLabel {
                                            Layout.fillWidth: true
                                            visible: del.type !== body.nt.AudioMixer
                                            tone: "default"
                                            elide: Text.ElideRight
                                            wrapMode: Text.NoWrap
                                            text: slotRow.modelData.name && slotRow.modelData.name.length > 0
                                                  ? slotRow.modelData.name
                                                  : qsTr("Slot %1").arg(slotRow.index + 1)
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
