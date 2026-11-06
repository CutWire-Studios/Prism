pragma ComponentBehavior: Bound
import QtQuick
import Prism

// A row of pedals and splits. The top-level chain also draws the IN / OUT pills. Split cards are
// loaded by file because a split holds chains of its own.
Row {
    id: root

    property var items: []
    property string split: ""
    property int lane: 0
    property bool top: false
    property var host

    spacing: Theme.spacingMd

    component Pill: Rectangle {
        property alias text: pillText.text
        height: 20
        width: pillText.implicitWidth + 16
        radius: 4
        color: Theme.panelSecondaryBg
        border.width: Theme.borderWidth
        border.color: Theme.panelSecondaryBorder
        ThemedLabel {
            id: pillText
            anchors.centerIn: parent
            wrapMode: Text.NoWrap
        }
    }

    component Arrow: Item {
        width: 16
        height: 20
        ThemedLabel {
            anchors.centerIn: parent
            text: "▸"
        }
    }

    Item {
        visible: root.top
        width: visible ? pillIn.width : 0
        height: 20
        y: 13
        Pill { id: pillIn; text: qsTr("IN") }
    }

    Repeater {
        model: root.items

        Row {
            id: entry
            required property var modelData
            required property int index
            spacing: Theme.spacingMd

            Item {
                width: 16
                height: 20
                y: entry.modelData.kind === "split" ? 28 : 13
                Arrow { }
            }

            Loader {
                source: entry.modelData.kind === "split" ? "SplitCard.qml" : "PedalCard.qml"
                onLoaded: {
                    item.host = root.host
                    item.parentSplit = root.split
                    item.parentLane = root.lane
                    item.position = entry.index
                    item.count = root.items.length
                    item.entry = entry.modelData
                }
            }
        }
    }

    Item {
        visible: root.items.length === 0
        width: visible ? 196 : 0
        height: 56

        Arrow { y: 18 }
        Rectangle {
            x: 20
            width: 176
            height: 56
            radius: Theme.radiusMd
            color: "transparent"
            border.width: Theme.borderWidth
            border.color: Theme.panelSecondaryBorder
            ThemedLabel {
                anchors.centerIn: parent
                text: qsTr("Click a palette item to add")
            }
        }
    }

    Item {
        visible: root.top
        width: visible ? 16 + pillOut.width + root.spacing : 0
        height: 20
        y: 13
        Arrow { }
        Pill { id: pillOut; x: 16 + 8; text: qsTr("OUT") }
    }
}
