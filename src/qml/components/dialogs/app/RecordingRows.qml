import QtQuick
import Prism

Column {
    id: root

    property var rows: []
    property bool audio: false

    width: parent ? parent.width : 400

    Repeater {
        model: root.rows

        Item {
            id: row
            required property int index
            required property var modelData
            width: root.width
            height: 40

            ThemedLabel {
                anchors.left: parent.left
                anchors.right: elapsed.left
                anchors.rightMargin: Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                tone: "default"
                size: "sm"
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                text: row.modelData.label
            }

            Text {
                id: elapsed
                anchors.right: toggle.left
                anchors.rightMargin: Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.elapsed
                color: row.modelData.active ? Theme.destructive : Theme.mutedForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                id: toggle
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 76
                variant: row.modelData.active ? "destructive" : "secondary"
                text: row.modelData.active ? qsTr("Stop") : qsTr("Record")
                onClicked: RecordingPanel.toggle(root.audio, row.index, !row.modelData.active)
            }
        }
    }
}
