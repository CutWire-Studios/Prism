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

    readonly property var continuous: entry ? entry.knobs.filter(k => k.scale !== "toggle" && k.scale !== "choice") : []
    readonly property var options: entry ? entry.knobs.filter(k => k.scale === "toggle" || k.scale === "choice") : []
    property bool bypass: entry ? entry.bypass : false

    implicitWidth: body.implicitWidth + 2 * Theme.spacingLg
    implicitHeight: body.implicitHeight + 3 + Theme.spacingLg
    radius: Theme.radiusMd
    color: Theme.panelSecondaryBg
    border.width: Theme.borderWidth
    border.color: Theme.panelSecondaryBorder
    opacity: bypass ? 0.6 : 1

    function categoryIndex(name) {
        let sum = 0
        for (let i = 0; i < name.length; ++i)
            sum += name.charCodeAt(i)
        return sum
    }

    Rectangle {
        x: 1
        y: 1
        width: parent.width - 2
        height: 3
        radius: 1
        color: root.entry ? Theme.categoryColor(root.categoryIndex(root.entry.category)) : Theme.accent
    }

    ColumnLayout {
        id: body
        x: Theme.spacingLg
        y: 3 + Theme.spacingMd
        width: parent.width - 2 * Theme.spacingLg
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            Rectangle {
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                radius: 7
                color: root.bypass ? "transparent" : Theme.constructive
                border.width: Theme.borderWidth
                border.color: root.bypass ? Theme.mutedForeground : Theme.constructive

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.host.pb.setPedalBypass(root.entry.id, !root.bypass)
                        root.bypass = !root.bypass
                    }
                }
            }

            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                size: "sm"
                font.weight: Font.Medium
                wrapMode: Text.NoWrap
                text: root.entry ? root.entry.label : ""
            }

            IconButton {
                glyph: "ellipsis"
                buttonSize: 22
                tooltip: qsTr("More")
                onClicked: menu.popup()

                ThemedContextMenu {
                    id: menu
                    ThemedMenuItem {
                        visible: !!root.entry && root.entry.type === "convolution"
                        text: qsTr("Change IR…")
                        onTriggered: root.host.chooseIr(root.entry.id)
                    }
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

        ThemedLabel {
            visible: !!root.entry && root.entry.type === "convolution"
            Layout.maximumWidth: 224
            elide: Text.ElideMiddle
            wrapMode: Text.NoWrap
            text: root.entry && root.entry.irPath.length > 0
                  ? root.entry.irPath.split("/").pop() : qsTr("No IR")
        }

        GridLayout {
            Layout.fillWidth: true
            enabled: !root.bypass
            columns: root.continuous.length <= 4 ? 2 : 3
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            Repeater {
                model: root.continuous

                PedalKnob {
                    required property var modelData
                    Layout.preferredWidth: implicitWidth
                    knob: modelData
                    onValueEdited: v => root.host.pb.setPedalKnob(root.entry.id, modelData.id, v)
                    onContextRequested: root.host.openRouteMenu(root.entry.id, modelData.id)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            enabled: !root.bypass
            spacing: Theme.spacingSm

            Repeater {
                model: root.options

                PedalKnob {
                    required property var modelData
                    Layout.fillWidth: true
                    knob: modelData
                    onValueEdited: v => root.host.pb.setPedalKnob(root.entry.id, modelData.id, v)
                }
            }
        }
    }
}
