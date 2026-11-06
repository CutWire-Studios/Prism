pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Prism

Rectangle {
    id: root

    property var entry
    property var sources: []
    property var host

    implicitWidth: 248
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
            ThemedLabel {
                tone: "default"
                size: "sm"
                font.weight: Font.Medium
                wrapMode: Text.NoWrap
                text: root.entry.label
            }
            ThemedLabel {
                Layout.fillWidth: true
                wrapMode: Text.NoWrap
                text: root.entry.id
            }
            IconButton {
                glyph: "trash-2"
                buttonSize: 22
                tooltip: qsTr("Remove")
                onClicked: root.host.pb.removeModulator(root.entry.id)
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            Repeater {
                model: root.entry.knobs

                PedalKnob {
                    required property var modelData
                    Layout.fillWidth: true
                    knob: modelData
                    onValueEdited: v => root.host.pb.setModKnob(root.entry.id, modelData.id, v)
                }
            }
        }

        Row {
            visible: root.entry.type === "steps"
            spacing: 2

            Repeater {
                model: root.entry.steps

                Rectangle {
                    id: bar
                    required property real modelData
                    required property int index
                    property real level: modelData
                    width: 16
                    height: 70
                    color: Theme.sliderTrack
                    radius: 2

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.height * bar.level
                        radius: 2
                        color: Theme.primary
                    }

                    MouseArea {
                        anchors.fill: parent
                        function apply(y) {
                            bar.level = Math.min(1, Math.max(0, 1 - y / bar.height))
                            root.host.pb.setStep(root.entry.id, bar.index, bar.level)
                        }
                        onPressed: (mouse) => apply(mouse.y)
                        onPositionChanged: (mouse) => { if (pressed) apply(mouse.y) }
                    }
                }
            }
        }

        RowLayout {
            visible: root.entry.type === "envelope"
            Layout.fillWidth: true
            ThemedLabel { text: qsTr("Source") }
            ThemedComboBox {
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "id"
                model: root.sources
                currentIndex: {
                    for (let i = 0; i < root.sources.length; ++i)
                        if (root.sources[i].id === root.entry.source)
                            return i
                    return 0
                }
                onActivated: index => root.host.pb.setModSource(root.entry.id, root.sources[index].id)
            }
        }

        ThemedLabel {
            text: qsTr("Targets")
            font.weight: Font.Medium
        }

        ThemedLabel {
            visible: root.entry.routes.length === 0
            text: qsTr("Right-click a knob to target it")
        }

        Repeater {
            model: root.entry.routes

            ColumnLayout {
                id: route
                required property var modelData
                Layout.fillWidth: true
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    ThemedLabel {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        text: route.modelData.label
                    }
                    IconButton {
                        glyph: "x"
                        buttonSize: 20
                        tooltip: qsTr("Remove route")
                        onClicked: root.host.pb.removeRoute(route.modelData.id)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ThemedSlider {
                        id: depth
                        Layout.fillWidth: true
                        label: qsTr("Depth")
                        showValueTooltip: false
                        from: -100
                        to: 100
                        stepSize: 1
                        value: Math.round(route.modelData.depth * 100)
                        onMoved: root.host.pb.setRouteDepth(route.modelData.id, value / 100)
                    }
                    ThemedLabel {
                        Layout.preferredWidth: 40
                        horizontalAlignment: Text.AlignRight
                        tone: "default"
                        text: Math.round(depth.value) + "%"
                    }
                }
            }
        }
    }
}
