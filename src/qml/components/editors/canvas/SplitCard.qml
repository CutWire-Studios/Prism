pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

Rectangle {
    id: root

    property var entry
    property string parentSplit: ""
    property int parentLane: 0
    property int position: 0
    property int count: 1
    property var host

    implicitWidth: body.implicitWidth + 2 * Theme.spacingLg
    implicitHeight: body.implicitHeight + 2 * Theme.spacingMd
    radius: Theme.radiusMd
    color: Theme.panelSecondaryBg
    border.width: Theme.borderWidth
    border.color: Theme.panelSecondaryBorder

    ColumnLayout {
        id: body
        x: Theme.spacingLg
        y: Theme.spacingMd
        width: parent.width - 2 * Theme.spacingLg
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            ThemedLabel {
                tone: "default"
                size: "sm"
                font.weight: Font.Medium
                wrapMode: Text.NoWrap
                text: root.entry && root.entry.bands ? qsTr("Band split") : qsTr("Parallel split")
            }

            PedalKnob {
                visible: !!root.entry && !root.entry.bands
                knob: ({ label: qsTr("Blend"), min: 0, max: 1, def: 0.5, scale: "linear", unit: "",
                         options: [], value: root.entry ? root.entry.blend : 0.5, modulated: false })
                onValueEdited: v => root.host.pb.setSplitBlend(root.entry.id, v)
            }

            ThemedCheckBox {
                visible: !!root.entry && !root.entry.bands
                text: qsTr("Crossfade")
                checked: root.entry ? root.entry.crossfade : false
                onToggled: root.host.pb.setSplitCrossfade(root.entry.id, checked)
            }

            Repeater {
                model: root.entry && root.entry.bands ? root.entry.crossovers : []

                PedalKnob {
                    required property var modelData
                    required property int index
                    knob: ({ label: modelData.label, min: 20, max: 20000, def: 1000, scale: "log", unit: "Hz",
                             options: [], value: modelData.value, modulated: false })
                    onValueEdited: v => root.host.pb.setCrossover(root.entry.id, index, v)
                }
            }

            Item { Layout.fillWidth: true }

            ThemedButton {
                text: qsTr("Lane")
                glyph: "plus"
                tooltip: qsTr("Add a lane")
                onClicked: root.host.pb.addLane(root.entry.id)
            }

            IconButton {
                glyph: "ellipsis"
                buttonSize: 22
                tooltip: qsTr("More")
                onClicked: menu.popup()

                ThemedContextMenu {
                    id: menu
                    ThemedMenuItem {
                        text: qsTr("Move left")
                        enabled: root.position > 0
                        onTriggered: root.host.pb.moveItem(root.entry.id, root.parentSplit, root.parentLane, root.position - 1)
                    }
                    ThemedMenuItem {
                        text: qsTr("Move right")
                        enabled: root.position < root.count - 1
                        onTriggered: root.host.pb.moveItem(root.entry.id, root.parentSplit, root.parentLane, root.position + 2)
                    }
                    ThemedMenuSeparator { }
                    ThemedMenuItem {
                        text: qsTr("Remove")
                        onTriggered: root.host.pb.removeItem(root.entry.id)
                    }
                }
            }
        }

        Repeater {
            model: root.entry ? root.entry.lanes : []

            Rectangle {
                id: laneRow
                required property var modelData
                required property int index
                readonly property bool targeted: root.host.pb.targetSplit === root.entry.id
                                                 && root.host.pb.targetLane === index

                Layout.fillWidth: true
                implicitWidth: laneContent.implicitWidth + 2 * Theme.spacingMd
                implicitHeight: laneContent.implicitHeight + 2 * Theme.spacingMd
                radius: Theme.radiusSm
                color: "transparent"
                border.width: targeted ? 2 : Theme.borderWidth
                border.color: targeted ? Theme.primary : Theme.panelSecondaryBorder

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.host.pb.setTarget(root.entry.id, laneRow.index)
                }

                RowLayout {
                    id: laneContent
                    x: Theme.spacingMd
                    y: Theme.spacingMd
                    spacing: Theme.spacingMd

                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 112
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            ThemedLabel {
                                Layout.fillWidth: true
                                text: qsTr("Lane %1").arg(laneRow.index + 1)
                            }
                            IconButton {
                                visible: root.entry.lanes.length > 2
                                glyph: "x"
                                buttonSize: 20
                                tooltip: qsTr("Remove lane")
                                onClicked: root.host.pb.removeLane(root.entry.id, laneRow.index)
                            }
                        }
                        PedalKnob {
                            Layout.fillWidth: true
                            knob: ({ label: qsTr("Gain"), min: 0, max: 4, def: 1, scale: "linear", unit: "x",
                                     options: [], value: laneRow.modelData.gain, modulated: false })
                            onValueEdited: v => root.host.pb.setLaneGain(root.entry.id, laneRow.index, v)
                        }
                    }

                    PedalChain {
                        Layout.alignment: Qt.AlignTop
                        items: laneRow.modelData.items
                        split: root.entry.id
                        lane: laneRow.index
                        host: root.host
                    }
                }
            }
        }
    }
}
