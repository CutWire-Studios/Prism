import QtQuick
import QtQuick.Layouts
import Prism

ColumnLayout {
    id: root

    property bool deckA: true

    readonly property string deckName: deckA ? AppController.deckNameA : AppController.deckNameB
    readonly property bool hasTransport: deckA ? AppController.deckHasTransportA : AppController.deckHasTransportB
    readonly property bool hasSpeed: deckA ? AppController.deckHasSpeedA : AppController.deckHasSpeedB
    readonly property bool playing: deckA ? AppController.deckPlayingA : AppController.deckPlayingB
    readonly property real time: deckA ? AppController.deckTimeA : AppController.deckTimeB
    readonly property real duration: deckA ? AppController.deckDurationA : AppController.deckDurationB
    readonly property real speed: deckA ? AppController.deckSpeedA : AppController.deckSpeedB
    readonly property bool onAir: AppController.onAirDeck === (deckA ? 0 : 1)

    function fmt(secs) {
        const s = Math.max(0, Math.floor(secs))
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }

    spacing: Theme.spacingSm

    Item {
        id: monitorCell

        readonly property real aspect: AppController.programWidth / Math.max(1, AppController.programHeight)

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 40

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height * monitorCell.aspect)
            height: width / monitorCell.aspect
            color: "black"
            radius: Theme.radiusSm
            border.width: root.onAir ? 2 : 1
            border.color: root.onAir ? Theme.destructive : Theme.panelBorder
            clip: true

            PreviewItem {
                anchors.fill: parent
                anchors.margins: 1
                compositor: AppController.compositor
                source: root.deckA ? PreviewItem.DeckA : PreviewItem.DeckB
            }

            Rectangle {
                x: Theme.spacingSm
                y: Theme.spacingSm
                width: badge.implicitWidth + Theme.spacingLg
                height: badge.implicitHeight + Theme.spacingXs * 2
                radius: Theme.radiusSm
                color: root.onAir ? Theme.destructive : Qt.rgba(0, 0, 0, 0.6)

                ThemedLabel {
                    id: badge
                    anchors.centerIn: parent
                    tone: "default"
                    text: (root.deckA ? qsTr("A") : qsTr("B")) + (root.onAir ? "  " + qsTr("ON AIR") : "")
                }
            }

            ThemedLabel {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.spacingSm
                visible: root.deckName.length > 0
                tone: "default"
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                text: root.deckName
                style: Text.Outline
                styleColor: "black"
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.hasTransport
        spacing: Theme.spacingSm

        IconButton {
            glyph: root.playing ? "pause" : "play"
            tooltip: root.playing ? qsTr("Pause") : qsTr("Play")
            onClicked: root.deckA ? AppController.togglePlayA() : AppController.togglePlayB()
        }
        ThemedSlider {
            id: seek
            Layout.fillWidth: true
            label: qsTr("Position")
            showValueTooltip: false
            onMoved: AppController.seekDeckFraction(root.deckA, value)
            Binding {
                target: seek
                property: "value"
                value: root.duration > 0 ? Math.min(1, root.time / root.duration) : 0
                when: !seek.pressed
            }
        }
        ThemedLabel {
            font.family: Theme.monoFontFamily
            text: root.fmt(root.time) + " / " + root.fmt(root.duration)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.hasSpeed
        spacing: Theme.spacingSm

        ThemedLabel { text: qsTr("Speed") }
        ThemedSlider {
            id: speedSlider
            Layout.fillWidth: true
            label: qsTr("Speed")
            from: 0.25
            to: 4
            valueFormatter: v => Number(v).toFixed(2) + "x"
            onMoved: AppController.setDeckSpeedValue(root.deckA, value)
            Binding {
                target: speedSlider
                property: "value"
                value: root.speed
                when: !speedSlider.pressed
            }
        }
        ThemedLabel {
            Layout.preferredWidth: 40
            font.family: Theme.monoFontFamily
            horizontalAlignment: Text.AlignRight
            text: root.speed.toFixed(2) + "x"
        }
    }
}
