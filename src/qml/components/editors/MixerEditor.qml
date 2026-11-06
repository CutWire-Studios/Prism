pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Prism

// Channel strips of an Audio Mixer node: one per connected input, with mute, name and volume.
ThemedDialog {
    id: root

    title: qsTr("Audio Mixer")
    preferredWidth: Theme.dialogWidthMd

    property var nodeId: 0
    property var channels: []

    function dbText(volume, muted) {
        if (muted)
            return qsTr("Muted")
        if (volume <= 0)
            return qsTr("-∞ dB")
        return qsTr("%1 dB").arg((20 * Math.log10(volume / 100)).toFixed(1))
    }

    function openFor(id, kind) {
        nodeId = id
        channels = mixer.channels(id)
        open()
    }

    onAccepted: {
        const rows = []
        for (let i = 0; i < strips.count; ++i) {
            const strip = strips.itemAt(i)
            rows.push({
                index: strip.slot,
                name: strip.nameText,
                volume: strip.volumeValue,
                muted: strip.isMuted
            })
        }
        mixer.apply(root.nodeId, rows)
    }

    MixerChannels {
        id: mixer
        controller: AppController.graph
    }

    contentItem: ColumnLayout {
        width: parent ? parent.width : root.preferredWidth
        spacing: Theme.spacingLg

        ThemedLabel {
            Layout.fillWidth: true
            visible: root.channels.length === 0
            text: qsTr("Connect audio streams to the mixer first.")
        }

        Repeater {
            id: strips
            model: root.channels

            delegate: ColumnLayout {
                id: strip

                required property var modelData
                required property int index

                readonly property int slot: modelData.index
                readonly property string nameText: nameField.text.trim()
                readonly property int volumeValue: Math.round(fader.value)
                readonly property bool isMuted: muteButton.checked

                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    visible: strip.index > 0
                    color: Theme.panelBorder
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingLg

                    ThemedToggleButton {
                        id: muteButton
                        checkable: true
                        checked: strip.modelData.muted
                        glyph: checked ? Theme.icons.volumeOff : Theme.icons.volumeHigh
                        tooltip: qsTr("Mute")
                    }

                    ThemedTextField {
                        id: nameField
                        Layout.fillWidth: true
                        text: strip.modelData.name
                        placeholderText: qsTr("in %1").arg(strip.slot + 1)
                    }

                    ThemedLabel {
                        Layout.preferredWidth: 72
                        horizontalAlignment: Text.AlignRight
                        size: "sm"
                        tone: "default"
                        text: root.dbText(fader.value, muteButton.checked)
                    }
                }

                ThemedSlider {
                    id: fader
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    stepSize: 1
                    value: strip.modelData.volume
                    valueFormatter: v => Math.round(v) + "%"
                }
            }
        }
    }
}
