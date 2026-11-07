import QtQuick
import QtQuick.Layouts
import Prism

ColumnLayout {
    id: root

    property bool staging: false
    readonly property int deckIndex: staging ? 1 : 0

    readonly property string deckName: staging ? AppController.deckNameStaging : AppController.deckNameLive
    readonly property bool hasTransport: staging ? AppController.deckHasTransportStaging : AppController.deckHasTransportLive
    readonly property bool playing: staging ? AppController.deckPlayingStaging : AppController.deckPlayingLive
    readonly property real time: staging ? AppController.deckTimeStaging : AppController.deckTimeLive
    readonly property real duration: staging ? AppController.deckDurationStaging : AppController.deckDurationLive
    readonly property bool isLive: !staging
    readonly property bool ready: staging ? AppController.deckReadyStaging : AppController.deckReadyLive
    readonly property bool onAir: AppController.onAirDeck === (staging ? 1 : 0)

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
                source: root.staging ? PreviewItem.Staging : PreviewItem.Live
            }

            Item {
                id: probeView
                anchors.fill: parent
                anchors.margins: 1
                visible: root.staging && AppController.previewNodeId !== 0

                Rectangle {
                    anchors.fill: parent
                    color: "black"
                }
                Image {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    source: AppController.previewImageUrl
                }
                ThemedLabel {
                    anchors.centerIn: parent
                    visible: AppController.previewImageUrl.length === 0
                    tone: "default"
                    text: qsTr("No live preview for this node")
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.spacingSm
                    width: Math.min(parent.width - Theme.spacingLg * 2, bannerRow.implicitWidth + Theme.spacingLg)
                    height: bannerRow.implicitHeight + Theme.spacingXs * 2
                    radius: Theme.radiusSm
                    color: Qt.rgba(0, 0, 0, 0.7)

                    RowLayout {
                        id: bannerRow
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMd
                        spacing: Theme.spacingSm

                        ThemedLabel {
                            Layout.fillWidth: true
                            tone: "default"
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
                            text: qsTr("Previewing: %1").arg(AppController.previewNodeName)
                        }
                        IconButton {
                            glyph: "x"
                            tooltip: qsTr("Stop previewing")
                            onClicked: AppController.clearNodePreview()
                        }
                    }
                }
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
                    text: (root.isLive ? qsTr("Live") : qsTr("Staging")) + (root.onAir ? "  " + qsTr("ON AIR") : "")
                }
            }

            ThemedLabel {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingSm
                visible: !root.isLive && root.deckName.length > 0
                tone: "default"
                text: root.ready ? qsTr("READY") : qsTr("Loading…")
                style: Text.Outline
                styleColor: "black"
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
            onClicked: AppController.togglePlay(root.deckIndex)
        }
        ThemedSlider {
            id: seek
            Layout.fillWidth: true
            label: qsTr("Position")
            showValueTooltip: false
            onMoved: AppController.seekDeckFraction(root.deckIndex, value)
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
}
