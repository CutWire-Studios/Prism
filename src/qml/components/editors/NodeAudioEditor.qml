import QtQuick
import QtQuick.Layouts
import Prism

// Audio settings of an Input node: when its audio plays and how far it is delayed.
ThemedDialog {
    id: root

    property var nodeId: 0
    property int mode: 0
    property int delayMs: 0

    title: qsTr("Audio Settings")
    preferredWidth: Theme.dialogWidthSm

    function openFor(id, kind) {
        const p = ClipEditTools.nodeParams(id)
        nodeId = id
        mode = p.audioPlaybackMode === 1 ? 1 : 0
        delayMs = p.audioDelayMs || 0
        open()
    }

    onAccepted: AppController.graph.setParams(nodeId, { audioPlaybackMode: mode, audioDelayMs: delayMs })

    contentItem: GridLayout {
        columns: 2
        columnSpacing: Theme.spacingLg
        rowSpacing: Theme.spacingLg

        ThemedLabel { text: qsTr("Playback mode") }
        ThemedComboBox {
            Layout.fillWidth: true
            model: [qsTr("Always play (while on either deck)"), qsTr("Play when its deck is active")]
            currentIndex: root.mode
            onActivated: index => root.mode = index
        }

        ThemedLabel { text: qsTr("Audio delay") }
        ThemedNumberField {
            Layout.fillWidth: true
            from: -5000
            to: 5000
            step: 10
            unit: "ms"
            value: root.delayMs
            onEdited: v => root.delayMs = Math.round(v)
        }
    }
}
