pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

PanelFrame {
    id: root

    readonly property var graph: AppController.graph
    readonly property var nt: NodePalette.nodeType
    property int channelCount: 0

    function isAudioNode(type, params) {
        return type === nt.AudioMixer || type === nt.MasterAudioInput || type === nt.MasterAudioCapture
               || type === nt.MasterAudioOutput || (type === nt.Input && !!params.hasAudio)
    }

    function hasStrip(type) {
        return type === nt.MasterAudioInput || type === nt.MasterAudioCapture || type === nt.Input
    }

    EmptyState {
        anchors.centerIn: parent
        visible: root.channelCount === 0
        compact: true
        glyph: "sliders-vertical"
        title: qsTr("No audio channels")
        hint: qsTr("Add a clip with audio, a Mic Input, Audio Capture or Audio Mixer node")
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
            spacing: Theme.spacingLg

            Repeater {
                model: root.graph ? root.graph.nodes : null

                delegate: ColumnLayout {
                    id: del

                    required property var nodeId
                    required property int type
                    required property string title
                    required property var params

                    readonly property bool relevant: root.isAudioNode(type, params)
                    readonly property string volumeKey: type === root.nt.Input ? "audioVolume" : "volume"
                    readonly property string muteKey: type === root.nt.Input ? "audioMuted" : "muted"
                    readonly property var slots: type === root.nt.AudioMixer && params.slots ? params.slots : []

                    Layout.fillWidth: true
                    visible: relevant
                    spacing: Theme.spacingSm

                    Component.onCompleted: if (relevant) root.channelCount++
                    Component.onDestruction: if (relevant) root.channelCount--

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        ThemedLabel {
                            tone: "default"
                            text: del.title
                        }
                        LevelMeter {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 4
                            level: AppController.audioLevels[String(del.nodeId)] || 0
                        }
                    }

                    ChannelStrip {
                        Layout.fillWidth: true
                        visible: root.hasStrip(del.type)
                        name: del.type === root.nt.Input ? qsTr("Clip")
                              : del.params.deviceId && del.params.deviceId.length > 0
                                ? del.params.deviceLabel : qsTr("Default device")
                        volume: del.params[del.volumeKey] !== undefined ? del.params[del.volumeKey] : 100
                        muted: !!del.params[del.muteKey]
                        onVolumeCommitted: v => root.graph.setParams(del.nodeId, { [del.volumeKey]: v })
                        onMuteToggled: m => root.graph.setParams(del.nodeId, { [del.muteKey]: m })
                    }

                    Repeater {
                        model: del.slots

                        delegate: ChannelStrip {
                            id: strip

                            required property var modelData
                            required property int index

                            function patch(p) {
                                const next = del.slots.slice()
                                next[index] = Object.assign({}, next[index], p)
                                root.graph.setParams(del.nodeId, { slots: next })
                            }

                            Layout.fillWidth: true
                            name: modelData.name && modelData.name.length > 0 ? modelData.name : qsTr("Input %1").arg(index + 1)
                            volume: modelData.volume !== undefined ? modelData.volume : 100
                            muted: !!modelData.muted
                            onVolumeCommitted: v => strip.patch({ volume: v })
                            onMuteToggled: m => strip.patch({ muted: m })
                        }
                    }
                }
            }
        }
    }
}
